#include "ChartMetrics.h"
#include "../../Tempo.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

using namespace MikuMikuWorld;

namespace Canvas::ChartMetrics
{
	namespace
	{
		constexpr int TPB = 480;

		struct Hit
		{
			int tick{};
			int lane{}, width{};
			bool flick{}, critical{}, trace{}, holdStart{};
		};

		struct Moment
		{
			int tick{};
			double time{};
			std::vector<Hit> hits;
			double centre() const
			{
				double c = 0.0;
				for (const Hit& h : hits)
					c += h.lane + h.width * 0.5;
				return hits.empty() ? 6.0 : c / hits.size();
			}
		};

		Histogram normalised(Histogram h)
		{
			const double total = std::accumulate(h.begin(), h.end(), 0.0);
			if (total > 0)
				for (double& v : h)
					v /= total;
			return h;
		}

		double quantile(std::vector<double> v, double q)
		{
			if (v.empty())
				return 0.0;
			std::sort(v.begin(), v.end());
			const double pos = q * (v.size() - 1);
			const size_t i = static_cast<size_t>(pos);
			const double f = pos - i;
			return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
		}

		int subdivision(int tick)
		{
			const int pos = ((tick % TPB) + TPB) % TPB;
			for (int n : { 1, 2, 3, 4, 6, 8 })
				if ((pos * n) % TPB == 0)
					return n;
			return 0;
		}

		double jensenShannon(const Histogram& p, const Histogram& q)
		{
			const size_t n = std::min(p.size(), q.size());
			double js = 0.0;
			for (size_t i = 0; i < n; ++i)
			{
				const double m = 0.5 * (p[i] + q[i]);
				if (p[i] > 0 && m > 0)
					js += 0.5 * p[i] * std::log2(p[i] / m);
				if (q[i] > 0 && m > 0)
					js += 0.5 * q[i] * std::log2(q[i] / m);
			}
			return js;
		}
	}

	const std::map<std::string, std::string>& scalarDescriptions()
	{
		static const std::map<std::string, std::string> d{
			{ "notesPerSecond", "actions (taps, traces, hold starts, flicking hold ends) per second" },
			{ "momentsPerSecond", "distinct hit moments per second" },
			{ "chordRate", "share of moments with two or more notes" },
			{ "bigChordRate", "share of moments with three or more notes" },
			{ "flickShare", "flicks per action" },
			{ "criticalShare", "criticals per action" },
			{ "traceShare", "traces per action" },
			{ "holdShare", "hold starts per action" },
			{ "slideShare", "share of holds that move sideways" },
			{ "holdBeats", "median hold length in beats" },
			{ "stepsPerHold", "steps (visible, hidden, ticks) per hold" },
			{ "guidesPerMinute", "decorative guides per minute" },
			{ "speedChangesPerMinute", "hi-speed changes per minute" },
			{ "widthMean", "average note width" },
			{ "densityCV", "how much the density changes from measure to measure (coefficient of variation)" },
			{ "densitySpread", "busiest vs calmest measures: (p90 - p10) / median of notes per measure" },
			{ "restMeasures", "share of measures inside the chart with no notes" },
			{ "rhythmRepeat", "share of two-beat windows with the same rhythm as the window before" },
			{ "exactRepeat", "share of two-beat windows identical (rhythm and lanes) to one of the 4 before" },
			{ "uniqueWindows", "distinct two-beat windows / windows" },
			{ "maxRepeatRun", "longest run of identical consecutive two-beat windows" },
			{ "movementMean", "average lane distance between consecutive moments" },
			{ "fastJumpShare", "share of steps under 0.2 s that jump 5+ lanes" },
			{ "patternStair", "share of three-note runs moving the same way (stairs)" },
			{ "patternAlternate", "share of three-note runs going back and forth" },
			{ "patternJack", "share of three-note runs on the same spot" },
			{ "patternJump", "share of three-note runs with a 5+ lane jump" },
			{ "laneEntropy", "how evenly the lanes are used (0-1)" },
			{ "peakNotesPerSecond", "notes per second in the busiest tenth of the chart (two-measure stretches)" },
			{ "fineRhythmShare", "share of moments on 16ths or finer" },
		};
		return d;
	}

	const std::map<std::string, std::string>& histogramDescriptions()
	{
		static const std::map<std::string, std::string> d{
			{ "gapBeats", "time between moments in beats: 1/8, 1/6, 1/4, 1/3, 1/2, 2/3, 3/4, 1, 1.5, 2, longer" },
			{ "grid", "rhythm grid of moments: beat, 8th, triplet, 16th, sextuplet, 32nd, other" },
			{ "width", "note widths 1-12" },
			{ "centre", "note centres by lane 0-11" },
			{ "move", "lane distance between consecutive moments 0..7+" },
			{ "holdLength", "hold lengths: up to 1/2, 1, 2, 4 beats, longer" },
			{ "chordSpan", "distance between the two notes of a chord 0..11" },
		};
		return d;
	}

	Profile measure(const Score& score)
	{
		Profile p;
		std::map<int, Moment> byTick;
		auto timeOf = [&](int tick) { return static_cast<double>(accumulateDuration(tick, TPB, score.tempoChanges)); };
		int holds = 0, slides = 0, steps = 0, guides = 0;
		std::vector<double> holdBeats;
		Histogram holdLength(5, 0.0);
		for (const auto& [id, n] : score.notes)
			if (n.getType() == NoteType::Tap)
				byTick[n.tick].hits.push_back({ n.tick, n.lane, n.width, n.flick != FlickType::None, n.critical, n.friction, false });
		for (const auto& [id, h] : score.holdNotes)
		{
			if (h.isGuide())
			{
				++guides;
				continue;
			}
			const Note& start = score.notes.at(h.start.ID);
			const Note& end = score.notes.at(h.end);
			++holds;
			steps += static_cast<int>(h.steps.size());
			bool moves = end.lane != start.lane || end.width != start.width;
			for (const auto& s : h.steps)
			{
				const Note& m = score.notes.at(s.ID);
				moves |= m.lane != start.lane || m.width != start.width;
			}
			slides += moves ? 1 : 0;
			const double beats = (end.tick - start.tick) / double(TPB);
			holdBeats.push_back(beats);
			holdLength[beats <= 0.5 ? 0 : beats <= 1 ? 1 : beats <= 2 ? 2 : beats <= 4 ? 3 : 4] += 1;
			if (h.startType == HoldNoteType::Normal)
				byTick[start.tick].hits.push_back({ start.tick, start.lane, start.width, false, start.critical, start.friction, true });
			if (end.flick != FlickType::None && h.endType == HoldNoteType::Normal)
				byTick[end.tick].hits.push_back({ end.tick, end.lane, end.width, true, end.critical, false, false });
		}
		std::vector<Moment> moments;
		for (auto& [tick, m] : byTick)
		{
			m.tick = tick;
			m.time = timeOf(tick);
			moments.push_back(m);
		}
		p.moments = static_cast<int>(moments.size());
		for (const Moment& m : moments)
			p.actions += static_cast<int>(m.hits.size());
		if (moments.size() < 2)
			return p;
		p.seconds = std::max(1.0, moments.back().time - moments.front().time);
		const double minutes = p.seconds / 60.0;
		auto& s = p.scalars;

		int flicks = 0, criticals = 0, traces = 0, holdStarts = 0, chords = 0, bigChords = 0;
		double widthSum = 0.0;
		Histogram width(12, 0.0), centre(12, 0.0), grid(7, 0.0), chordSpan(12, 0.0);
		for (const Moment& m : moments)
		{
			if (m.hits.size() >= 2)
				++chords;
			if (m.hits.size() >= 3)
				++bigChords;
			if (m.hits.size() == 2)
			{
				const double a = m.hits[0].lane + m.hits[0].width * 0.5, b = m.hits[1].lane + m.hits[1].width * 0.5;
				chordSpan[std::min(11, static_cast<int>(std::lround(std::abs(a - b))))] += 1;
			}
			const int sub = subdivision(m.tick);
			const int bin = sub == 1 ? 0 : sub == 2 ? 1 : sub == 3 ? 2 : sub == 4 ? 3 : sub == 6 ? 4 : sub == 8 ? 5 : 6;
			grid[bin] += 1;
			for (const Hit& h : m.hits)
			{
				flicks += h.flick;
				criticals += h.critical;
				traces += h.trace;
				holdStarts += h.holdStart;
				widthSum += h.width;
				width[std::clamp(h.width, 1, 12) - 1] += 1;
				centre[std::clamp(static_cast<int>(h.lane + h.width * 0.5), 0, 11)] += 1;
			}
		}
		const double actions = std::max(1, p.actions);
		s["notesPerSecond"] = p.actions / p.seconds;
		s["momentsPerSecond"] = p.moments / p.seconds;
		s["chordRate"] = chords / double(p.moments);
		s["bigChordRate"] = bigChords / double(p.moments);
		s["flickShare"] = flicks / actions;
		s["criticalShare"] = criticals / actions;
		s["traceShare"] = traces / actions;
		s["holdShare"] = holdStarts / actions;
		s["slideShare"] = holds ? slides / double(holds) : 0.0;
		s["holdBeats"] = quantile(holdBeats, 0.5);
		s["stepsPerHold"] = holds ? steps / double(holds) : 0.0;
		s["guidesPerMinute"] = guides / minutes;
		s["speedChangesPerMinute"] = score.hiSpeedChanges.size() / minutes;
		s["widthMean"] = widthSum / actions;

		// density over the song, per measure (4/4 assumed where the chart does not say)
		const int beatsPerMeasure = score.timeSignatures.empty() ? 4 : std::max(1, score.timeSignatures.begin()->second.numerator);
		const int measureTicks = beatsPerMeasure * TPB;
		const int firstMeasure = moments.front().tick / measureTicks, lastMeasure = moments.back().tick / measureTicks;
		std::vector<double> perMeasure(lastMeasure - firstMeasure + 1, 0.0);
		for (const Moment& m : moments)
			perMeasure[m.tick / measureTicks - firstMeasure] += static_cast<double>(m.hits.size());
		const double mean = std::accumulate(perMeasure.begin(), perMeasure.end(), 0.0) / perMeasure.size();
		double var = 0.0;
		for (double v : perMeasure)
			var += (v - mean) * (v - mean);
		s["densityCV"] = mean > 0 ? std::sqrt(var / perMeasure.size()) / mean : 0.0;
		const double median = std::max(0.5, quantile(perMeasure, 0.5));
		s["densitySpread"] = (quantile(perMeasure, 0.9) - quantile(perMeasure, 0.1)) / median;
		s["restMeasures"] = std::count(perMeasure.begin(), perMeasure.end(), 0.0) / double(perMeasure.size());
		// the busy parts: notes per second in the busiest tenth of two-measure stretches
		{
			std::vector<double> rates;
			for (size_t k = 0; k + 1 < perMeasure.size(); k += 1)
			{
				const int m0 = firstMeasure + static_cast<int>(k);
				const double t0 = timeOf(m0 * measureTicks), t1 = timeOf((m0 + 2) * measureTicks);
				if (t1 > t0)
					rates.push_back((perMeasure[k] + perMeasure[k + 1]) / (t1 - t0));
			}
			s["peakNotesPerSecond"] = quantile(rates, 0.9);
		}
		{
			double fine = 0.0;
			for (const Moment& m : moments)
			{
				const int sub = subdivision(m.tick);
				fine += sub == 4 || sub == 6 || sub == 8 || sub == 0 ? 1.0 : 0.0;
			}
			s["fineRhythmShare"] = fine / moments.size();
		}

		// repetition, in two-beat windows
		{
			std::map<int, std::string> rhythm, full;
			for (const Moment& m : moments)
			{
				const int w = m.tick / (TPB * 2);
				rhythm[w] += std::to_string(m.tick % (TPB * 2)) + ",";
				full[w] += std::to_string(m.tick % (TPB * 2));
				std::vector<std::pair<int, int>> lanes;
				for (const Hit& h : m.hits)
					lanes.push_back({ h.lane, h.width });
				std::sort(lanes.begin(), lanes.end());
				for (const auto& [l, wd] : lanes)
					full[w] += ":" + std::to_string(l) + "/" + std::to_string(wd);
				full[w] += ";";
			}
			int rhythmSame = 0, exact = 0, windows = 0, run = 1, maxRun = 1;
			std::set<std::string> distinct;
			int previousWindow = -100;
			for (const auto& [w, sig] : full)
			{
				++windows;
				distinct.insert(sig);
				if (rhythm.count(w - 1) && rhythm[w - 1] == rhythm[w])
					++rhythmSame;
				bool repeated = false;
				for (int back = 1; back <= 4; ++back)
					if (full.count(w - back) && full[w - back] == sig)
						repeated = true;
				exact += repeated;
				if (w == previousWindow + 1 && full.count(previousWindow) && full[previousWindow] == sig)
					maxRun = std::max(maxRun, ++run);
				else
					run = 1;
				previousWindow = w;
			}
			s["rhythmRepeat"] = windows ? rhythmSame / double(windows) : 0.0;
			s["exactRepeat"] = windows ? exact / double(windows) : 0.0;
			s["uniqueWindows"] = windows ? distinct.size() / double(windows) : 0.0;
			s["maxRepeatRun"] = maxRun;
		}

		// movement and pattern kinds
		Histogram gap(11, 0.0), move(8, 0.0);
		double moveSum = 0.0;
		int fastJumps = 0, fastSteps = 0;
		for (size_t i = 1; i < moments.size(); ++i)
		{
			const double beats = (moments[i].tick - moments[i - 1].tick) / double(TPB);
			static const double edges[] = { 0.13, 0.18, 0.26, 0.34, 0.51, 0.7, 0.76, 1.01, 1.51, 2.01 };
			int b = 0;
			while (b < 10 && beats > edges[b])
				++b;
			gap[b] += 1;
			const double d = std::abs(moments[i].centre() - moments[i - 1].centre());
			moveSum += d;
			move[std::min(7, static_cast<int>(std::lround(d)))] += 1;
			const double dt = moments[i].time - moments[i - 1].time;
			if (dt < 0.2)
			{
				++fastSteps;
				fastJumps += d >= 5.0;
			}
		}
		s["movementMean"] = moveSum / (moments.size() - 1);
		s["fastJumpShare"] = fastSteps ? fastJumps / double(fastSteps) : 0.0;
		int triples = 0, stair = 0, alternate = 0, jack = 0, jump = 0;
		for (size_t i = 2; i < moments.size(); ++i)
		{
			const Moment &a = moments[i - 2], &b = moments[i - 1], &c = moments[i];
			if (a.hits.size() != 1 || b.hits.size() != 1 || c.hits.size() != 1)
				continue;
			if (c.time - a.time > 0.7)
				continue;	// not a run
			++triples;
			const double d1 = b.centre() - a.centre(), d2 = c.centre() - b.centre();
			if (std::max(std::abs(d1), std::abs(d2)) >= 5.0)
				++jump;
			else if (std::abs(d1) < 0.5 && std::abs(d2) < 0.5)
				++jack;
			else if (d1 * d2 > 0 && std::abs(d1) >= 0.5 && std::abs(d2) >= 0.5)
				++stair;
			else if (d1 * d2 < 0 && std::abs(d1) >= 1.5 && std::abs(d2) >= 1.5)
				++alternate;
		}
		s["patternStair"] = triples ? stair / double(triples) : 0.0;
		s["patternAlternate"] = triples ? alternate / double(triples) : 0.0;
		s["patternJack"] = triples ? jack / double(triples) : 0.0;
		s["patternJump"] = triples ? jump / double(triples) : 0.0;
		{
			const Histogram c = normalised(centre);
			double h = 0.0;
			for (double v : c)
				if (v > 0)
					h -= v * std::log2(v);
			s["laneEntropy"] = h / std::log2(12.0);
		}

		auto& hs = p.histograms;
		hs["gapBeats"] = normalised(gap);
		hs["grid"] = normalised(grid);
		hs["width"] = normalised(width);
		hs["centre"] = normalised(centre);
		hs["move"] = normalised(move);
		hs["holdLength"] = normalised(holdLength);
		hs["chordSpan"] = normalised(chordSpan);
		return p;
	}

	nlohmann::ordered_json Profile::toJson() const
	{
		nlohmann::ordered_json j;
		j["seconds"] = seconds;
		j["actions"] = actions;
		j["moments"] = moments;
		for (const auto& [k, v] : scalars)
			j["scalars"][k] = std::round(v * 10000.0) / 10000.0;
		for (const auto& [k, h] : histograms)
		{
			nlohmann::ordered_json a = nlohmann::ordered_json::array();
			for (double v : h)
				a.push_back(std::round(v * 10000.0) / 10000.0);
			j["histograms"][k] = a;
		}
		return j;
	}

	Profile Profile::fromJson(const nlohmann::json& j)
	{
		Profile p;
		p.seconds = j.value("seconds", 0.0);
		p.actions = j.value("actions", 0);
		p.moments = j.value("moments", 0);
		if (j.contains("scalars"))
			for (const auto& [k, v] : j["scalars"].items())
				p.scalars[k] = v.get<double>();
		if (j.contains("histograms"))
			for (const auto& [k, v] : j["histograms"].items())
				p.histograms[k] = v.get<Histogram>();
		return p;
	}

	double Reference::expected(const std::string& metric, double level) const
	{
		auto s = scalars.find(metric);
		const double plain = s == scalars.end() ? 0.0 : s->second.mean;
		auto f = byLevel.find(metric);
		if (level <= 0 || f == byLevel.end() || f->second.count < 3)
			return plain;
		// stay near what was seen: a little past the observed levels at most
		const double lv = std::clamp(level, levelMin - 2.0, levelMax + 2.0);
		double v = f->second.at(lv);
		if (s != scalars.end())
		{
			const double span = std::max(1e-6, s->second.p90 - s->second.p10);
			v = std::clamp(v, s->second.p10 - span, s->second.p90 + span);
		}
		return v;
	}

	namespace
	{
		nlohmann::ordered_json spreadJson(const Reference::Spread& s)
		{
			auto r = [](double v) { return std::round(v * 10000.0) / 10000.0; };
			return { { "mean", r(s.mean) }, { "sd", r(s.sd) }, { "p10", r(s.p10) }, { "p50", r(s.p50) }, { "p90", r(s.p90) }, { "count", s.count } };
		}
	}

	nlohmann::ordered_json Reference::toJson() const
	{
		nlohmann::ordered_json j;
		j["charts"] = charts;
		j["levelMin"] = levelMin;
		j["levelMax"] = levelMax;
		for (const auto& [k, v] : scalars)
			j["scalars"][k] = spreadJson(v);
		for (const auto& [k, h] : histograms)
		{
			nlohmann::ordered_json a = nlohmann::ordered_json::array();
			for (double v : h)
				a.push_back(std::round(v * 10000.0) / 10000.0);
			j["histograms"][k] = a;
		}
		for (const auto& [k, f] : byLevel)
			j["byLevel"][k] = { { "a", std::round(f.a * 10000.0) / 10000.0 }, { "b", std::round(f.b * 10000.0) / 10000.0 }, { "count", f.count } };
		return j;
	}

	Reference Reference::fromJson(const nlohmann::json& j)
	{
		Reference r;
		r.charts = j.value("charts", 0);
		r.levelMin = j.value("levelMin", 0.0);
		r.levelMax = j.value("levelMax", 0.0);
		if (j.contains("scalars"))
			for (const auto& [k, v] : j["scalars"].items())
				r.scalars[k] = { v.value("mean", 0.0), v.value("sd", 0.0), v.value("p10", 0.0), v.value("p50", 0.0), v.value("p90", 0.0), v.value("count", 0) };
		if (j.contains("histograms"))
			for (const auto& [k, v] : j["histograms"].items())
				r.histograms[k] = v.get<Histogram>();
		if (j.contains("byLevel"))
			for (const auto& [k, v] : j["byLevel"].items())
				r.byLevel[k] = { v.value("a", 0.0), v.value("b", 0.0), v.value("count", 0) };
		return r;
	}

	const std::vector<std::string>& LevelModel::features()
	{
		static const std::vector<std::string> f{ "notesPerSecond", "peakNotesPerSecond", "chordRate", "bigChordRate", "fastJumpShare",
			"movementMean", "fineRhythmShare", "traceShare", "holdShare", "flickShare" };
		return f;
	}

	double LevelModel::predict(const Profile& p) const
	{
		if (weights.empty())
			return 0.0;
		double v = intercept;
		const auto& f = features();
		for (size_t i = 0; i < f.size() && i < weights.size(); ++i)
		{
			auto it = p.scalars.find(f[i]);
			const double x = it == p.scalars.end() ? 0.0 : it->second;
			v += weights[i] * (x - means[i]) / scales[i];
		}
		return v;
	}

	nlohmann::ordered_json ReferenceSet::toJson() const
	{
		nlohmann::ordered_json j;
		j["format"] = "canvas-editor/chart-reference";
		j["version"] = 1;
		j["source"] = source;
		j["note"] = "Spreads of chart metrics over official charts per difficulty (aggregate numbers only).";
		for (const auto& [k, r] : difficulties)
			j["difficulties"][k] = r.toJson();
		if (!levels.weights.empty())
			j["levelModel"] = { { "intercept", levels.intercept }, { "weights", levels.weights }, { "means", levels.means },
				{ "scales", levels.scales }, { "charts", levels.charts }, { "error", levels.error }, { "features", LevelModel::features() } };
		return j;
	}

	ReferenceSet ReferenceSet::fromJson(const nlohmann::json& j)
	{
		ReferenceSet set;
		set.source = j.value("source", "");
		if (j.contains("difficulties"))
			for (const auto& [k, v] : j["difficulties"].items())
				set.difficulties[k] = Reference::fromJson(v);
		if (j.contains("levelModel"))
		{
			const auto& l = j["levelModel"];
			set.levels.intercept = l.value("intercept", 0.0);
			set.levels.weights = l.value("weights", std::vector<double>{});
			set.levels.means = l.value("means", std::vector<double>{});
			set.levels.scales = l.value("scales", std::vector<double>{});
			set.levels.charts = l.value("charts", 0);
			set.levels.error = l.value("error", 0.0);
			if (set.levels.means.size() != set.levels.weights.size() || set.levels.scales.size() != set.levels.weights.size())
				set.levels = {};
		}
		return set;
	}

	const Reference* ReferenceSet::find(const std::string& difficulty) const
	{
		auto it = difficulties.find(difficulty);
		return it == difficulties.end() ? nullptr : &it->second;
	}

	ReferenceSet buildReferences(const std::vector<Sample>& samples, const std::string& source)
	{
		ReferenceSet set;
		set.source = source;
		std::map<std::string, std::vector<const Sample*>> byDifficulty;
		for (const Sample& s : samples)
			if (s.profile.moments >= 20)
				byDifficulty[s.difficulty].push_back(&s);
		for (const auto& [name, list] : byDifficulty)
		{
			Reference r;
			r.charts = static_cast<int>(list.size());
			std::map<std::string, std::vector<double>> values;
			std::map<std::string, Histogram> sums;
			std::vector<double> levels;
			for (const Sample* s : list)
			{
				for (const auto& [k, v] : s->profile.scalars)
					values[k].push_back(v);
				for (const auto& [k, h] : s->profile.histograms)
				{
					auto& sum = sums[k];
					sum.resize(std::max(sum.size(), h.size()), 0.0);
					for (size_t i = 0; i < h.size(); ++i)
						sum[i] += h[i];
				}
				if (s->level > 0)
					levels.push_back(s->level);
			}
			for (const auto& [k, v] : values)
			{
				Reference::Spread sp;
				sp.count = static_cast<int>(v.size());
				sp.mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
				double var = 0.0;
				for (double x : v)
					var += (x - sp.mean) * (x - sp.mean);
				sp.sd = v.size() > 1 ? std::sqrt(var / (v.size() - 1)) : 0.0;
				sp.p10 = quantile(v, 0.1);
				sp.p50 = quantile(v, 0.5);
				sp.p90 = quantile(v, 0.9);
				r.scalars[k] = sp;
			}
			for (auto& [k, h] : sums)
				r.histograms[k] = normalised(h);
			if (!levels.empty())
			{
				r.levelMin = *std::min_element(levels.begin(), levels.end());
				r.levelMax = *std::max_element(levels.begin(), levels.end());
			}
			// level trends: least squares over the charts that have a level
			for (const auto& [k, v] : values)
			{
				double sx = 0, sy = 0, sxx = 0, sxy = 0;
				int n = 0;
				for (const Sample* s : list)
				{
					auto it = s->profile.scalars.find(k);
					if (s->level <= 0 || it == s->profile.scalars.end())
						continue;
					sx += s->level;
					sy += it->second;
					sxx += s->level * s->level;
					sxy += s->level * it->second;
					++n;
				}
				const double den = n * sxx - sx * sx;
				if (n >= 3 && std::abs(den) > 1e-9)
				{
					Reference::LevelFit f;
					f.b = (n * sxy - sx * sy) / den;
					f.a = (sy - f.b * sx) / n;
					f.count = n;
					r.byLevel[k] = f;
				}
			}
			set.difficulties[name] = r;
		}

		// the level model: ridge regression of the in-game level on the chart's numbers, over every
		// chart with a level (all difficulties together: a level means the same everywhere)
		std::vector<const Sample*> levelled;
		for (const Sample& s : samples)
			if (s.level > 0 && s.profile.moments >= 20)
				levelled.push_back(&s);
		const auto& f = LevelModel::features();
		const size_t k = f.size();
		if (levelled.size() >= 12)
		{
			LevelModel lm;
			lm.charts = static_cast<int>(levelled.size());
			lm.means.assign(k, 0.0);
			lm.scales.assign(k, 1.0);
			std::vector<std::vector<double>> x(levelled.size(), std::vector<double>(k, 0.0));
			std::vector<double> y;
			for (size_t i = 0; i < levelled.size(); ++i)
			{
				for (size_t c = 0; c < k; ++c)
				{
					auto it = levelled[i]->profile.scalars.find(f[c]);
					x[i][c] = it == levelled[i]->profile.scalars.end() ? 0.0 : it->second;
				}
				y.push_back(levelled[i]->level);
			}
			for (size_t c = 0; c < k; ++c)
			{
				double m = 0, v = 0;
				for (const auto& row : x)
					m += row[c];
				m /= x.size();
				for (const auto& row : x)
					v += (row[c] - m) * (row[c] - m);
				lm.means[c] = m;
				lm.scales[c] = std::max(1e-6, std::sqrt(v / x.size()));
			}
			const double yMean = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
			// (X'X + lambda I) w = X'y on standardised features
			const double lambda = 1.0;
			std::vector<std::vector<double>> a(k, std::vector<double>(k + 1, 0.0));
			for (size_t i = 0; i < x.size(); ++i)
			{
				std::vector<double> z(k);
				for (size_t c = 0; c < k; ++c)
					z[c] = (x[i][c] - lm.means[c]) / lm.scales[c];
				for (size_t r2 = 0; r2 < k; ++r2)
				{
					for (size_t c = 0; c < k; ++c)
						a[r2][c] += z[r2] * z[c];
					a[r2][k] += z[r2] * (y[i] - yMean);
				}
			}
			for (size_t r2 = 0; r2 < k; ++r2)
				a[r2][r2] += lambda;
			// Gaussian elimination
			for (size_t c = 0; c < k; ++c)
			{
				size_t pivot = c;
				for (size_t r2 = c + 1; r2 < k; ++r2)
					if (std::abs(a[r2][c]) > std::abs(a[pivot][c]))
						pivot = r2;
				std::swap(a[c], a[pivot]);
				if (std::abs(a[c][c]) < 1e-12)
					continue;
				for (size_t r2 = 0; r2 < k; ++r2)
				{
					if (r2 == c)
						continue;
					const double factor = a[r2][c] / a[c][c];
					for (size_t c2 = c; c2 <= k; ++c2)
						a[r2][c2] -= factor * a[c][c2];
				}
			}
			lm.weights.assign(k, 0.0);
			for (size_t c = 0; c < k; ++c)
				lm.weights[c] = std::abs(a[c][c]) < 1e-12 ? 0.0 : a[c][k] / a[c][c];
			lm.intercept = yMean;
			double se = 0.0;
			for (const Sample* s : levelled)
				se += std::pow(lm.predict(s->profile) - s->level, 2);
			lm.error = std::sqrt(se / levelled.size());
			set.levels = lm;
		}
		return set;
	}

	nlohmann::ordered_json Comparison::toJson() const
	{
		nlohmann::ordered_json j;
		j["score"] = std::round(score * 1000.0) / 1000.0;
		nlohmann::ordered_json list = nlohmann::ordered_json::array();
		for (const auto& d : deviations)
		{
			auto r = [](double v) { return std::round(v * 1000.0) / 1000.0; };
			nlohmann::ordered_json e{ { "metric", d.metric }, { "z", r(d.z) } };
			if (!d.histogram)
			{
				e["value"] = r(d.value);
				e["official"] = r(d.expected);
			}
			list.push_back(e);
		}
		j["deviations"] = list;
		return j;
	}

	Comparison compare(const Profile& chart, const Reference& reference, double level)
	{
		Comparison c;
		double total = 0.0;
		int n = 0;
		for (const auto& [k, spread] : reference.scalars)
		{
			auto it = chart.scalars.find(k);
			if (it == chart.scalars.end())
				continue;
			const double expected = reference.expected(k, level);
			// never trust a spread from very few charts to be tight: one chart says little about
			// how far others of its difficulty wander
			const double floor = std::max((0.15 + 0.5 / std::max(1, spread.count)) * std::abs(spread.mean), 0.02);
			const double sd = std::max(spread.sd, floor);
			Deviation d{ k, it->second, expected, (it->second - expected) / sd, false };
			c.deviations.push_back(d);
			total += std::min(4.0, std::abs(d.z));
			++n;
		}
		for (const auto& [k, h] : reference.histograms)
		{
			auto it = chart.histograms.find(k);
			if (it == chart.histograms.end())
				continue;
			// a histogram of nothing (no chords, no holds) says nothing about its shape
			const double mass = std::accumulate(it->second.begin(), it->second.end(), 0.0);
			const double referenceMass = std::accumulate(h.begin(), h.end(), 0.0);
			// (averaged shares: a mass under one half means most official charts of this difficulty
			// do not have the thing at all, so its shape is not typical of them)
			if (mass <= 1e-9 || referenceMass < 0.5)
				continue;
			Deviation d{ k, 0.0, 0.0, 10.0 * jensenShannon(it->second, h), true };
			c.deviations.push_back(d);
			total += std::min(4.0, d.z);
			++n;
		}
		std::sort(c.deviations.begin(), c.deviations.end(), [](const Deviation& a, const Deviation& b) { return std::abs(a.z) > std::abs(b.z); });
		c.score = n ? total / n : 0.0;
		return c;
	}
}
