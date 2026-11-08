// Beat times -> tempo maps. Pure maths (no audio, no model), driven by synthetic beats in the tests.
//
// The detected beats are noisy (20 ms frames), so tempo is never read from single intervals. Instead:
//   1. repair missed / doubled beats and downbeats,
//   2. split the beat list into stretches with dynamic programming: each stretch is either a
//      constant tempo (a line through beat index vs time) or a ramp (a parabola), and every stretch
//      pays a penalty so noise doesn't make tempo changes,
//   3. refit ramps with a BPM that is exactly linear in the beat position,
//   4. nudge every stretch so the stretches meet at the same song times (no drift),
//   5. read bars off the downbeats (beats per bar, with changes).
#include "BeatDetect.h"
#include "../../Constants.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace Canvas::BeatDetect
{
	using namespace MikuMikuWorld;

	namespace
	{
		using ld = long double;

		// ------------------------------------------------------------------ repairing

		double localMedianInterval(const std::vector<double>& t, int i, int radius)
		{
			const int n = static_cast<int>(t.size());
			std::vector<double> d;
			for (int k = std::max(0, i - radius); k <= std::min(n - 2, i + radius); ++k)
				d.push_back(t[k + 1] - t[k]);
			if (d.empty())
				return 0.5;
			std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
			return d[d.size() / 2];
		}

		// Missed beats (an interval of 2-4 beats) get filled in, doubled ones (a very short interval) dropped.
		std::vector<double> repairBeats(const std::vector<double>& in)
		{
			std::vector<double> sorted = in;
			std::sort(sorted.begin(), sorted.end());
			if (sorted.size() < 3)
				return sorted;
			std::vector<double> out{ sorted[0] };
			for (size_t i = 1; i < sorted.size(); ++i)
			{
				const double m = localMedianInterval(sorted, static_cast<int>(i) - 1, 6);
				const double d = sorted[i] - out.back();
				const double r = d / m;
				if (r < 0.55)
					continue;
				if (r > 1.6)
				{
					const int k = static_cast<int>(std::lround(r));
					if (k >= 2 && k <= 4 && std::abs(d / k - m) < 0.25 * m)
					{
						const double base = out.back();
						for (int j = 1; j < k; ++j)
							out.push_back(base + d * j / k);
					}
				}
				out.push_back(sorted[i]);
			}

			// Octave slips: the network sometimes drops to half time (or doubles) for a few bars. Where the
			// local beat length is ~2x (~0.5x) the song's usual one, fill in (thin out) the beats.
			if (out.size() < 16)
				return out;
			std::vector<double> all;
			for (size_t i = 0; i + 1 < out.size(); ++i)
				all.push_back(out[i + 1] - out[i]);
			std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
			const double G = all[all.size() / 2];
			std::vector<double> fixed{ out[0] };
			for (size_t i = 0; i + 1 < out.size(); ++i)
			{
				const double q = localMedianInterval(out, static_cast<int>(i), 4) / G;
				const double d = out[i + 1] - out[i];
				if (q > 1.75 && q < 2.3 && d / G > 1.6 && d / G < 2.5)
					fixed.push_back(0.5 * (out[i] + out[i + 1]));
				else if (q < 0.57 && q > 0.43 && out[i + 1] - fixed.back() < 0.75 * G)
					continue;
				fixed.push_back(out[i + 1]);
			}
			return fixed;
		}

		// downbeat times -> beat indices
		std::vector<int> downbeatIndices(const std::vector<double>& beats, const std::vector<double>& downbeats)
		{
			std::vector<int> idx;
			for (double d : downbeats)
			{
				auto it = std::lower_bound(beats.begin(), beats.end(), d);
				int best = static_cast<int>(it - beats.begin());
				if (best >= static_cast<int>(beats.size()) || (best > 0 && std::abs(beats[best - 1] - d) < std::abs(beats[best] - d)))
					best = std::max(0, best - 1);
				if (std::abs(beats[best] - d) < 0.25)
					idx.push_back(best);
			}
			std::sort(idx.begin(), idx.end());
			idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
			return idx;
		}

		int modeLength(const std::vector<int>& down)
		{
			int counts[16]{};
			for (size_t i = 0; i + 1 < down.size(); ++i)
			{
				const int l = down[i + 1] - down[i];
				if (l >= 2 && l < 16)
					++counts[l];
			}
			int best = 4, bestCount = 0;
			for (int l = 2; l < 16; ++l)
				if (counts[l] > bestCount)
				{
					bestCount = counts[l];
					best = l;
				}
			return best;
		}

		// The network's downbeats are noisy (stray ones, missed ones, bursts in busy parts). Walk through
		// them with a bar length and a phase: a downbeat on the grid is kept (missed ones in between are
		// filled), one off the grid is ignored, unless a steady run of equal bars (>= 4 bars, >= 20 beats)
		// starts there: that is a real meter change (or a real shift of the bar line).
		std::vector<int> cleanDownbeats(const std::vector<int>& down, int beatCount)
		{
			std::vector<int> out;
			if (down.size() < 2)
			{
				for (int i = 0; i < beatCount; i += 4)
					out.push_back(i);
				return out;
			}
			const int n = static_cast<int>(down.size());
			// bars of equal length that start at j (count) and their length
			auto runAt = [&](int j, int& length) {
				if (j + 1 >= n)
					return 0;
				length = down[j + 1] - down[j];
				int count = 1;
				while (j + count + 1 < n && down[j + count + 1] - down[j + count] == length)
					++count;
				return count;
			};
			auto strong = [&](int j, int& length) {
				const int count = runAt(j, length);
				return length >= 2 && length <= 7 && count >= 4 && count * length >= 20;
			};

			int L = modeLength(down);
			for (int j = 0; j < n; ++j)
			{
				int length = 0;
				if (strong(j, length))
				{
					L = length;
					break;
				}
			}

			int A = down[0];
			out.push_back(A);
			for (int j = 1; j < n; ++j)
			{
				const int d = down[j];
				if (d <= A)
					continue;
				int newLength = 0;
				const bool steady = strong(j, newLength);
				const bool onGrid = (d - A) % L == 0;
				if (steady && newLength != L)
				{
					// the run starting here sets a new meter; whatever lies between is filled with the old one
					if (onGrid)
						for (int x = A + L; x < d; x += L)
							out.push_back(x);
					out.push_back(d);
					A = d;
					L = newLength;
				}
				else if (onGrid)
				{
					for (int x = A + L; x < d; x += L)
						out.push_back(x);
					out.push_back(d);
					A = d;
				}
				else if (steady)
				{
					// same meter, shifted bar line
					out.push_back(d);
					A = d;
				}
				// else: stray downbeat, ignored
			}
			return out;
		}

		// ------------------------------------------------------------------ the grid we fit

		struct Grid
		{
			std::vector<double> t;		// beat times, beat 0 = first downbeat
			std::vector<int> down;		// downbeat beat indices, down[0] == 0
		};

		bool makeGrid(const Beats& in, Grid& grid)
		{
			std::vector<double> beats = repairBeats(in.beats);
			if (beats.size() < 8)
				return false;
			std::vector<int> down = downbeatIndices(beats, in.downbeats);
			down = cleanDownbeats(down, static_cast<int>(beats.size()));
			if (down.empty())
				return false;
			const int first = down[0];
			grid.t.assign(beats.begin() + first, beats.end());
			grid.down.clear();
			for (int d : down)
				grid.down.push_back(d - first);
			return grid.t.size() >= 8;
		}

		// A variant with another beat unit. Half: every second beat (all bars must be even).
		bool halved(const Grid& in, Grid& out)
		{
			for (size_t i = 0; i + 1 < in.down.size(); ++i)
				if ((in.down[i + 1] - in.down[i]) % 2)
					return false;
			out = Grid{};
			for (size_t i = 0; i < in.t.size(); i += 2)
				out.t.push_back(in.t[i]);
			for (int d : in.down)
				out.down.push_back(d / 2);
			return out.t.size() >= 8;
		}

		Grid doubled(const Grid& in)
		{
			Grid out;
			for (size_t i = 0; i < in.t.size(); ++i)
			{
				out.t.push_back(in.t[i]);
				if (i + 1 < in.t.size())
					out.t.push_back(0.5 * (in.t[i] + in.t[i + 1]));
			}
			for (int d : in.down)
				out.down.push_back(d * 2);
			return out;
		}

		// ------------------------------------------------------------------ segment fitting

		// Time of beat `y` into a stretch whose BPM runs b0 -> b1 linearly over n beats.
		double tau(double y, double b0, double b1, double n)
		{
			const double s = (b1 - b0) / n;
			if (std::abs(s * y / b0) < 1e-9)
				return 60.0 * y / b0;
			return 60.0 / s * std::log(1.0 + s * y / b0);
		}

		struct Seg
		{
			int a{ 0 }, n{ 0 };
			double tc{ 0 };		// song time of its first beat
			double b0{ 120 }, b1{ 120 };
			bool ramp{ false };
			double sse{ 0 };
			double endTime() const { return tc + tau(n, b0, b1, n); }
			double duration() const { return tau(n, b0, b1, n); }
		};

		class Fitter
		{
		public:
			explicit Fitter(const std::vector<double>& times)
				: t(times), N(static_cast<int>(times.size()))
			{
				t0 = t[0];
				meanPeriod = (t[N - 1] - t[0]) / std::max(1, N - 1);
				U0.assign(N + 1, 0);
				U1.assign(N + 1, 0);
				U2.assign(N + 1, 0);
				R2.assign(N + 1, 0);
				for (int i = 0; i < N; ++i)
				{
					const ld r = t[i] - t0 - i * meanPeriod;
					const ld x = i;
					U0[i + 1] = U0[i] + r;
					U1[i + 1] = U1[i] + r * x;
					U2[i + 1] = U2[i] + r * x * x;
					R2[i + 1] = R2[i] + r * r;
				}
			}

			double period() const { return meanPeriod; }

			// r(x') = c0 + c1 x' for x' = 0..n-1; returns sse
			double fitConst(int a, int b, double& c0, double& c1) const
			{
				const int n = b - a;
				ld u0, u1, u2;
				moments(a, b, u0, u1, u2);
				const ld s0 = n, s1 = P1(n), s2 = P2(n);
				const ld det = s0 * s2 - s1 * s1;
				const ld k0 = (u0 * s2 - u1 * s1) / det, k1 = (s0 * u1 - s1 * u0) / det;
				c0 = static_cast<double>(k0);
				c1 = static_cast<double>(k1);
				const ld sse = (R2[b] - R2[a]) - k0 * u0 - k1 * u1;
				return sse > 0 ? static_cast<double>(sse) : 0.0;
			}

			// r(x') = c0 + c1 x' + c2 x'^2
			double fitQuad(int a, int b, double& c0, double& c1, double& c2) const
			{
				const int n = b - a;
				ld u[3];
				moments(a, b, u[0], u[1], u[2]);
				ld m[3][4] = {
					{ static_cast<ld>(n), P1(n), P2(n), u[0] },
					{ P1(n), P2(n), P3(n), u[1] },
					{ P2(n), P3(n), P4(n), u[2] },
				};
				for (int col = 0; col < 3; ++col)
				{
					int piv = col;
					for (int r = col + 1; r < 3; ++r)
						if (std::abs(m[r][col]) > std::abs(m[piv][col]))
							piv = r;
					if (piv != col)
						for (int k = 0; k < 4; ++k)
							std::swap(m[piv][k], m[col][k]);
					for (int r = col + 1; r < 3; ++r)
					{
						const ld f = m[r][col] / m[col][col];
						for (int k = col; k < 4; ++k)
							m[r][k] -= f * m[col][k];
					}
				}
				ld c[3];
				for (int r = 2; r >= 0; --r)
				{
					ld v = m[r][3];
					for (int k = r + 1; k < 3; ++k)
						v -= m[r][k] * c[k];
					c[r] = v / m[r][r];
				}
				c0 = static_cast<double>(c[0]);
				c1 = static_cast<double>(c[1]);
				c2 = static_cast<double>(c[2]);
				const ld sse = (R2[b] - R2[a]) - c[0] * u[0] - c[1] * u[1] - c[2] * u[2];
				return sse > 0 ? static_cast<double>(sse) : 0.0;
			}

			Seg constSeg(int a, int b) const
			{
				double c0, c1;
				Seg s;
				s.a = a;
				s.n = b - a;
				s.sse = fitConst(a, b, c0, c1);
				s.tc = t0 + a * meanPeriod + c0;
				s.b0 = s.b1 = 60.0 / (meanPeriod + c1);
				return s;
			}

			// Ramp whose BPM is exactly linear in the beat position: start from the parabola, then
			// a few damped Gauss-Newton steps on (b0, b1).
			Seg rampSeg(int a, int b) const
			{
				double c0, c1, c2;
				fitQuad(a, b, c0, c1, c2);
				const int n = b - a;
				Seg s;
				s.a = a;
				s.n = n;
				s.ramp = true;
				s.b0 = 60.0 / (meanPeriod + c1);
				s.b1 = 60.0 / (meanPeriod + c1 + 2.0 * c2 * n);
				auto evaluate = [&](double b0, double b1, double& tc) {
					double mean = 0;
					for (int i = a; i < b; ++i)
						mean += t[i] - tau(i - a, b0, b1, n);
					mean /= n;
					tc = mean;
					double sse = 0;
					for (int i = a; i < b; ++i)
					{
						const double e = t[i] - mean - tau(i - a, b0, b1, n);
						sse += e * e;
					}
					return sse;
				};
				double tc = 0;
				double sse = evaluate(s.b0, s.b1, tc);
				double lambda = 1e-3;
				for (int iter = 0; iter < 25; ++iter)
				{
					const double h0 = std::max(1e-3, s.b0 * 1e-4), h1 = std::max(1e-3, s.b1 * 1e-4);
					// numeric Jacobian of the residuals, via the normal equations
					double jtj[2][2]{}, jtr[2]{};
					double tc0 = 0, tcA = 0, tcB = 0;
					evaluate(s.b0, s.b1, tc0);
					evaluate(s.b0 + h0, s.b1, tcA);
					evaluate(s.b0, s.b1 + h1, tcB);
					for (int i = a; i < b; ++i)
					{
						const double base = t[i] - tc0 - tau(i - a, s.b0, s.b1, n);
						const double d0 = (tau(i - a, s.b0 + h0, s.b1, n) + tcA - tau(i - a, s.b0, s.b1, n) - tc0) / h0;
						const double d1 = (tau(i - a, s.b0, s.b1 + h1, n) + tcB - tau(i - a, s.b0, s.b1, n) - tc0) / h1;
						jtj[0][0] += d0 * d0;
						jtj[0][1] += d0 * d1;
						jtj[1][1] += d1 * d1;
						jtr[0] += d0 * base;
						jtr[1] += d1 * base;
					}
					jtj[1][0] = jtj[0][1];
					bool improved = false;
					for (int tryCount = 0; tryCount < 8 && !improved; ++tryCount)
					{
						const double m00 = jtj[0][0] * (1 + lambda), m11 = jtj[1][1] * (1 + lambda), m01 = jtj[0][1];
						const double det = m00 * m11 - m01 * m01;
						if (std::abs(det) < 1e-18)
							break;
						const double db0 = (jtr[0] * m11 - jtr[1] * m01) / det, db1 = (m00 * jtr[1] - m01 * jtr[0]) / det;
						const double nb0 = s.b0 + db0, nb1 = s.b1 + db1;
						if (nb0 < 20 || nb1 < 20 || nb0 > 600 || nb1 > 600)
						{
							lambda *= 10;
							continue;
						}
						double ntc = 0;
						const double nsse = evaluate(nb0, nb1, ntc);
						if (nsse < sse)
						{
							s.b0 = nb0;
							s.b1 = nb1;
							sse = nsse;
							lambda = std::max(1e-9, lambda / 5);
							improved = true;
						}
						else
							lambda *= 10;
					}
					if (!improved)
						break;
				}
				s.sse = evaluate(s.b0, s.b1, tc);
				s.tc = tc;
				return s;
			}

			// median |beat - model| in seconds: shrugs off a few stray beats where the sum of squares doesn't
			double medianResidual(const Seg& s) const
			{
				std::vector<double> r;
				for (int i = s.a; i < s.a + s.n; ++i)
					r.push_back(std::abs(t[i] - s.tc - tau(i - s.a, s.b0, s.b1, s.n)));
				std::nth_element(r.begin(), r.begin() + r.size() / 2, r.end());
				return r[r.size() / 2];
			}

			// cost used by the dynamic programme; infinite for stretches that can't be real tempos
			double quadCost(int a, int b, double& sse) const
			{
				double c0, c1, c2;
				sse = fitQuad(a, b, c0, c1, c2);
				const int n = b - a;
				const double p0 = meanPeriod + c1, p1 = meanPeriod + c1 + 2.0 * c2 * n;
				const double lo = 60.0 / 400.0, hi = 60.0 / 30.0;
				if (p0 < lo || p0 > hi || p1 < lo || p1 > hi)
					return -1;
				return 0;
			}

		private:
			const std::vector<double>& t;
			int N;
			double t0{ 0 }, meanPeriod{ 0 };
			std::vector<ld> U0, U1, U2, R2;

			void moments(int a, int b, ld& u0, ld& u1, ld& u2) const
			{
				const ld A = a;
				const ld w0 = U0[b] - U0[a], w1 = U1[b] - U1[a], w2 = U2[b] - U2[a];
				u0 = w0;
				u1 = w1 - A * w0;
				u2 = w2 - 2 * A * w1 + A * A * w0;
			}

			static ld P1(int n) { return static_cast<ld>(n) * (n - 1) / 2; }
			static ld P2(int n) { return static_cast<ld>(n - 1) * n * (2 * static_cast<ld>(n) - 1) / 6; }
			static ld P3(int n) { const ld p = P1(n); return p * p; }
			static ld P4(int n)
			{
				const ld m = n - 1;
				return m * (m + 1) * (2 * m + 1) * (3 * m * m + 3 * m - 1) / 30;
			}
		};

		// ------------------------------------------------------------------ segmentation

		std::vector<Seg> segment(const Grid& grid, const BuildOptions& opt, bool allowChanges)
		{
			const int N = static_cast<int>(grid.t.size());
			Fitter fit(grid.t);
			std::vector<Seg> segs;
			if (!allowChanges || N < 2 * opt.minConstBeats)
			{
				segs.push_back(fit.constSeg(0, N));
				return segs;
			}

			const double inf = std::numeric_limits<double>::infinity();
			const double invVar = 1.0 / (opt.sigma * opt.sigma);
			std::vector<double> best(N + 1, inf);
			std::vector<int> from(N + 1, -1);
			std::vector<char> isRamp(N + 1, 0);
			best[0] = 0;
			for (int j = opt.minConstBeats; j <= N; ++j)
			{
				for (int i = 0; i + opt.minConstBeats <= j; ++i)
				{
					if (best[i] == inf)
						continue;
					// the tail must stay long enough to be a stretch of its own
					if (j != N && N - j < opt.minConstBeats)
						continue;
					double c0, c1;
					const double sseC = fit.fitConst(i, j, c0, c1);
					double cost = best[i] + sseC * invVar + opt.penConst;
					if (cost < best[j])
					{
						best[j] = cost;
						from[j] = i;
						isRamp[j] = 0;
					}
					if (j - i >= opt.minRampBeats)
					{
						double sseQ = 0;
						if (fit.quadCost(i, j, sseQ) < 0)
							continue;
						cost = best[i] + sseQ * invVar + opt.penRamp;
						if (cost < best[j])
						{
							best[j] = cost;
							from[j] = i;
							isRamp[j] = 1;
						}
					}
				}
			}

			std::vector<std::pair<int, int>> ranges;
			std::vector<char> ramps;
			for (int j = N; j > 0 && from[j] >= 0;)
			{
				ranges.push_back({ from[j], j });
				ramps.push_back(isRamp[j]);
				j = from[j];
			}
			std::reverse(ranges.begin(), ranges.end());
			std::reverse(ramps.begin(), ramps.end());
			if (ranges.empty() || ranges.front().first != 0)
			{
				segs.push_back(fit.constSeg(0, N));
				return segs;
			}

			for (size_t k = 0; k < ranges.size(); ++k)
			{
				Seg s = ramps[k] ? fit.rampSeg(ranges[k].first, ranges[k].second) : fit.constSeg(ranges[k].first, ranges[k].second);
				if (s.ramp)
				{
					const double mean = 0.5 * (s.b0 + s.b1);
					if (std::abs(s.b1 - s.b0) / mean * 100.0 < opt.minRampPercent || std::abs(s.b1 - s.b0) < 3.0)
						s = fit.constSeg(s.a, s.a + s.n);
				}
				segs.push_back(s);
			}

			// The dynamic programme stops a ramp a few beats early (its first and last beats barely differ
			// from the neighbouring constant tempo). Slide each boundary next to a ramp to where the two
			// refitted stretches explain the beats best.
			auto refit = [&](const Seg& like, int a, int b) { return like.ramp ? fit.rampSeg(a, b) : fit.constSeg(a, b); };
			for (int pass = 0; pass < 2; ++pass)
				for (size_t k = 0; k + 1 < segs.size(); ++k)
				{
					if (!segs[k].ramp && !segs[k + 1].ramp)
						continue;
					const int a = segs[k].a, mid = segs[k + 1].a, b = segs[k + 1].a + segs[k + 1].n;
					const int minLeft = segs[k].ramp ? opt.minRampBeats : opt.minConstBeats;
					const int minRight = segs[k + 1].ramp ? opt.minRampBeats : opt.minConstBeats;
					double bestSse = segs[k].sse + segs[k + 1].sse;
					int bestMid = mid;
					for (int shift = -10; shift <= 10; ++shift)
					{
						const int m = mid + shift;
						if (shift == 0 || m - a < minLeft || b - m < minRight)
							continue;
						const double sse = refit(segs[k], a, m).sse + refit(segs[k + 1], m, b).sse;
						if (sse < bestSse - 1e-9)
						{
							bestSse = sse;
							bestMid = m;
						}
					}
					if (bestMid != mid)
					{
						segs[k] = refit(segs[k], a, bestMid);
						segs[k + 1] = refit(segs[k + 1], bestMid, b);
					}
				}

			// Neighbours that one steady tempo explains about as well (most beats within a few ms, only a
			// handful of strays) are one stretch: a burst of bad beats must not become a tempo change.
			for (size_t k = 0; k + 1 < segs.size();)
			{
				const Seg merged = fit.constSeg(segs[k].a, segs[k + 1].a + segs[k + 1].n);
				const bool same = !segs[k].ramp && !segs[k + 1].ramp && std::abs(segs[k].b0 - segs[k + 1].b0) / segs[k].b0 < 0.004;
				if (same || fit.medianResidual(merged) < opt.mergeMedianSec)
				{
					segs[k] = merged;
					segs.erase(segs.begin() + k + 1);
					k = 0;
				}
				else
					++k;
			}
			return segs;
		}

		// ------------------------------------------------------------------ candidate assembly

		int numeratorMode(const std::vector<int>& down)
		{
			if (down.size() < 2)
				return 4;
			return std::clamp(modeLength(down), 1, 12);
		}

		// bars -> time signature changes (measure 0-based from beat 0)
		std::vector<SigChange> signatures(const std::vector<int>& down, int beatCount, bool singleMeter)
		{
			std::vector<SigChange> out;
			if (singleMeter || down.size() < 2)
			{
				out.push_back({ 0, numeratorMode(down) });
				return out;
			}
			int last = -1;
			for (size_t j = 0; j + 1 < down.size(); ++j)
			{
				int len = down[j + 1] - down[j];
				len = std::clamp(len, 1, 12);
				if (len != last)
				{
					out.push_back({ static_cast<int>(j), len });
					last = len;
				}
			}
			if (out.empty())
				out.push_back({ 0, 4 });
			(void)beatCount;
			return out;
		}

		double rmsFor(const std::vector<double>& t, double bpm)
		{
			const double period = 60.0 / bpm;
			double mean = 0;
			for (size_t i = 0; i < t.size(); ++i)
				mean += t[i] - i * period;
			mean /= t.size();
			double sum = 0;
			for (size_t i = 0; i < t.size(); ++i)
			{
				const double e = t[i] - mean - i * period;
				sum += e * e;
			}
			return std::sqrt(sum / t.size());
		}

		std::string describe(const Candidate& c)
		{
			char text[160];
			if (c.pieces.size() == 1 && !c.pieces[0].ramp())
				std::snprintf(text, sizeof(text), "%.2f BPM, %d beats per bar", c.firstBpm, c.sigs.front().numerator);
			else
				std::snprintf(text, sizeof(text), "%.1f-%.1f BPM, %d tempo change(s), %d ramp(s), %d meter change(s)", c.minBpm, c.maxBpm,
					c.tempoChanges, c.ramps, c.sigChanges);
			return text;
		}

		void finish(Candidate& c, const Grid& grid, const BuildOptions& opt)
		{
			// lead-in: empty bars before the first downbeat, as far as the song allows
			const int firstBar = c.sigs.empty() ? 4 : c.sigs.front().numerator;
			const double bpm0 = c.pieces.front().bpmStart;
			int bars = 0;
			for (int k = opt.leadInBars; k >= 1; --k)
				if (c.startSec - k * firstBar * 60.0 / bpm0 >= 0.0)
				{
					bars = k;
					break;
				}
			const int lead = bars * firstBar;
			if (lead > 0)
			{
				c.startSec -= lead * 60.0 / bpm0;
				if (c.pieces.front().ramp())
				{
					for (auto& p : c.pieces)
						p.startBeat += lead;
					c.pieces.insert(c.pieces.begin(), TempoPiece{ 0, lead, bpm0, bpm0 });
				}
				else
				{
					c.pieces.front().beats += lead;
					for (size_t i = 1; i < c.pieces.size(); ++i)
						c.pieces[i].startBeat += lead;
				}
				for (auto& s : c.sigs)
					s.measure += bars;
				c.sigs.front().measure = 0;
			}
			c.totalBeats = c.pieces.back().startBeat + c.pieces.back().beats;

			c.minBpm = 1e9;
			c.maxBpm = 0;
			c.ramps = 0;
			for (const auto& p : c.pieces)
			{
				c.minBpm = std::min({ c.minBpm, p.bpmStart, p.bpmEnd });
				c.maxBpm = std::max({ c.maxBpm, p.bpmStart, p.bpmEnd });
				c.ramps += p.ramp() ? 1 : 0;
			}
			c.firstBpm = c.pieces.front().bpmStart;
			c.tempoChanges = static_cast<int>(c.pieces.size()) - 1;
			c.sigChanges = static_cast<int>(c.sigs.size()) - 1;

			// how well the written chart follows the detected beats
			const std::vector<double> times = gridTimes(c);
			double sum = 0, worst = 0;
			int counted = 0;
			for (size_t i = 0; i < grid.t.size() && lead + i < times.size(); ++i)
			{
				const double e = times[lead + i] - grid.t[i];
				sum += e * e;
				worst = std::max(worst, std::abs(e));
				++counted;
			}
			c.rmsMs = counted ? std::sqrt(sum / counted) * 1000.0 : 0;
			c.maxMs = worst * 1000.0;
			c.description = describe(c);
		}

		Candidate build(const Grid& grid, const BuildOptions& opt, Kind kind)
		{
			Candidate c;
			c.kind = kind;
			const int N = static_cast<int>(grid.t.size());
			std::vector<Seg> segs;

			if (kind == Kind::Simple)
			{
				Fitter fit(grid.t);
				Seg s = fit.constSeg(0, N);
				const double exact = s.b0;
				const double exactRms = rmsFor(grid.t, exact);
				double chosen = std::round(exact * 100.0) / 100.0;
				for (double step : { 1.0, 0.5, 0.25, 0.1 })
				{
					const double cand = std::round(exact / step) * step;
					if (cand > 0 && rmsFor(grid.t, cand) <= std::max(exactRms * 1.15, exactRms + 0.0015))
					{
						chosen = cand;
						break;
					}
				}
				const double period = 60.0 / chosen;
				double mean = 0;
				for (int i = 0; i < N; ++i)
					mean += grid.t[i] - i * period;
				s.tc = mean / N;
				s.b0 = s.b1 = chosen;
				segs.push_back(s);
			}
			else
				segs = segment(grid, opt, true);

			// stretches meet at one song time; scale each so it lasts exactly until the next one starts
			std::vector<double> boundary(segs.size() + 1);
			boundary[0] = segs[0].tc;
			for (size_t k = 1; k < segs.size(); ++k)
				boundary[k] = 0.5 * (segs[k].tc + segs[k - 1].endTime());
			boundary[segs.size()] = segs.back().endTime();
			for (size_t k = 0; k + 1 < segs.size(); ++k)
			{
				const double want = boundary[k + 1] - boundary[k];
				const double have = segs[k].duration();
				if (want > 0 && have > 0)
				{
					const double f = have / want;
					if (f > 0.8 && f < 1.25)
					{
						segs[k].b0 *= f;
						segs[k].b1 *= f;
					}
				}
			}

			for (const auto& s : segs)
			{
				TempoPiece p;
				p.startBeat = s.a;
				p.beats = s.n;
				p.bpmStart = std::round(s.b0 * 1000.0) / 1000.0;
				p.bpmEnd = s.ramp ? std::round(s.b1 * 1000.0) / 1000.0 : p.bpmStart;
				c.pieces.push_back(p);
			}
			c.startSec = boundary[0];
			c.sigs = signatures(grid.down, N, kind == Kind::Simple);
			finish(c, grid, opt);
			return c;
		}

		bool sameTiming(const Candidate& a, const Candidate& b)
		{
			if (a.pieces.size() != b.pieces.size() || a.sigs.size() != b.sigs.size())
				return false;
			for (size_t i = 0; i < a.pieces.size(); ++i)
				if (std::abs(a.pieces[i].bpmStart - b.pieces[i].bpmStart) > 0.005 || std::abs(a.pieces[i].bpmEnd - b.pieces[i].bpmEnd) > 0.005 ||
					a.pieces[i].startBeat != b.pieces[i].startBeat)
					return false;
			for (size_t i = 0; i < a.sigs.size(); ++i)
				if (a.sigs[i].numerator != b.sigs[i].numerator || a.sigs[i].measure != b.sigs[i].measure)
					return false;
			return true;
		}
	}

	std::vector<Candidate> buildCandidates(const Beats& beats, const BuildOptions& options)
	{
		std::vector<Candidate> out;
		Grid grid;
		if (!makeGrid(beats, grid))
			return out;

		Candidate best = build(grid, options, Kind::Best);
		out.push_back(best);

		// the other beat unit: half time for fast songs, double time for slow ones
		Candidate other;
		bool haveOther = false;
		auto tryHalf = [&] {
			Grid g;
			if (halved(grid, g))
			{
				other = build(g, options, Kind::HalfTime);
				haveOther = true;
			}
		};
		auto tryDouble = [&] {
			other = build(doubled(grid), options, Kind::DoubleTime);
			haveOther = true;
		};
		if (best.firstBpm >= 125.0)
		{
			tryHalf();
			if (!haveOther)
				tryDouble();
		}
		else
			tryDouble();
		if (haveOther)
			out.push_back(other);

		Candidate simple = build(grid, options, Kind::Simple);
		if (sameTiming(simple, best))
		{
			// the best guess is already steady: offer the other direction instead
			Candidate alt;
			bool haveAlt = false;
			if (other.kind == Kind::HalfTime)
			{
				alt = build(doubled(grid), options, Kind::DoubleTime);
				haveAlt = true;
			}
			else
			{
				Grid g;
				if (halved(grid, g))
				{
					alt = build(g, options, Kind::HalfTime);
					haveAlt = true;
				}
			}
			// no point in a 350 BPM version of a fast song
			if (haveAlt && alt.firstBpm >= 40.0 && alt.firstBpm <= 320.0)
				out.push_back(alt);
		}
		else
			out.push_back(simple);
		return out;
	}

	Timing toTiming(const Candidate& c, int rampStepTicks)
	{
		Timing timing;
		rampStepTicks = std::max(1, rampStepTicks);
		auto round3 = [](double v) { return static_cast<float>(std::round(v * 1000.0) / 1000.0); };
		for (const auto& p : c.pieces)
		{
			const int a = p.startBeat * TICKS_PER_BEAT, e = (p.startBeat + p.beats) * TICKS_PER_BEAT;
			if (!p.ramp())
			{
				if (timing.tempos.empty() || timing.tempos.back().bpm != round3(p.bpmStart))
					timing.tempos.push_back(Tempo(a, round3(p.bpmStart)));
				continue;
			}
			// longer ramps get coarser steps so the chart doesn't fill up with tempo changes
			const int step = p.beats > 64 ? rampStepTicks * 2 : rampStepTicks;
			for (int tick = a; tick < e; tick += step)
			{
				const int hi = std::min(tick + step, e);
				const double u = (0.5 * (tick + hi) - a) / static_cast<double>(e - a);
				const float bpm = round3(p.bpmStart + (p.bpmEnd - p.bpmStart) * u);
				if (timing.tempos.empty() || timing.tempos.back().bpm != bpm)
					timing.tempos.push_back(Tempo(tick, bpm));
			}
		}
		if (timing.tempos.empty())
			timing.tempos.push_back(Tempo(0, 120.0f));
		timing.tempos.front().tick = 0;

		for (const auto& s : c.sigs)
			timing.signatures[s.measure] = TimeSignature{ s.measure, s.numerator, 4 };
		if (timing.signatures.empty())
			timing.signatures[0] = TimeSignature{ 0, 4, 4 };

		timing.musicOffsetMs = c.startSec == 0.0 ? 0.0 : -c.startSec * 1000.0;
		return timing;
	}

	std::vector<double> gridTimes(const Candidate& c)
	{
		const Timing timing = toTiming(c);
		std::vector<double> out;
		out.reserve(static_cast<size_t>(c.totalBeats) + 1);
		double seconds = c.startSec;
		size_t index = 0;
		for (int beat = 0; beat <= c.totalBeats; ++beat)
		{
			out.push_back(seconds);
			// advance one beat through whatever tempo steps lie inside it
			int tick = beat * TICKS_PER_BEAT;
			const int end = tick + TICKS_PER_BEAT;
			while (tick < end)
			{
				while (index + 1 < timing.tempos.size() && timing.tempos[index + 1].tick <= tick)
					++index;
				const int next = index + 1 < timing.tempos.size() ? std::min(end, timing.tempos[index + 1].tick) : end;
				seconds += (next - tick) * 60.0 / timing.tempos[index].bpm / TICKS_PER_BEAT;
				tick = next;
			}
		}
		return out;
	}
}
