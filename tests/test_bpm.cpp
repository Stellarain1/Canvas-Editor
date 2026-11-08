#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/BeatDetect.h"
#include "Canvas/Core/BpmTools.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace BD = Canvas::BeatDetect;
namespace BT = Canvas::BpmTools;

namespace
{
	struct Stretch
	{
		int beats;
		double bpm0, bpm1;	// a ramp when they differ (BPM linear in the beat position)
	};

	// song time of `y` beats into a stretch
	double tauOf(double y, const Stretch& s)
	{
		const double k = (s.bpm1 - s.bpm0) / s.beats;
		if (std::abs(k) < 1e-12)
			return 60.0 * y / s.bpm0;
		return 60.0 / k * std::log(1.0 + k * y / s.bpm0);
	}

	// tiny deterministic noise, +-amount
	struct Noise
	{
		uint32_t state{ 12345 };
		double next(double amount)
		{
			state = state * 1664525u + 1013904223u;
			return ((state >> 8) / 16777216.0 * 2.0 - 1.0) * amount;
		}
	};

	// beats of a song that starts at `start` seconds; bars: beats per bar for each bar (repeating the last)
	BD::Beats synth(const std::vector<Stretch>& stretches, const std::vector<int>& bars, double start, double noise = 0.0)
	{
		BD::Beats out;
		Noise rng;
		double t = start;
		int index = 0;
		size_t barIndex = 0;
		int nextDownbeat = 0;
		for (const auto& s : stretches)
		{
			for (int i = 0; i < s.beats; ++i)
			{
				const double at = t + tauOf(i, s);
				const double jitter = noise > 0 ? rng.next(noise) : 0.0;
				out.beats.push_back(at + jitter);
				if (index == nextDownbeat)
				{
					out.downbeats.push_back(at + jitter);
					nextDownbeat += bars[std::min(barIndex, bars.size() - 1)];
					++barIndex;
				}
				++index;
			}
			t += tauOf(s.beats, s);
		}
		return out;
	}

	const BD::Candidate* find(const std::vector<BD::Candidate>& list, BD::Kind kind)
	{
		for (const auto& c : list)
			if (c.kind == kind)
				return &c;
		return nullptr;
	}
}

// ---------------------------------------------------------------- tempo maps

TEST(bpm_constant_tempo_is_found_with_a_bar_length_and_offset)
{
	const BD::Beats beats = synth({ { 160, 172.0, 172.0 } }, { 4 }, 3.37, 0.006);
	const auto list = BD::buildCandidates(beats);
	CHECK(list.size() >= 2);
	const BD::Candidate& best = list[0];
	CHECK(best.kind == BD::Kind::Best);
	CHECK_EQ(best.pieces.size(), static_cast<size_t>(1));
	CHECK(std::abs(best.firstBpm - 172.0) < 0.05);
	CHECK_EQ(best.sigs.size(), static_cast<size_t>(1));
	CHECK_EQ(best.sigs[0].numerator, 4);
	CHECK(best.rmsMs < 8.0);
	// one empty bar fits before the first downbeat (at 3.37 s): tick 0 is a bar earlier
	CHECK(std::abs(best.startSec - (3.37 - 4 * 60.0 / 172.0)) < 0.02);

	// half time for a fast song, and a rounded steady version
	CHECK(find(list, BD::Kind::HalfTime) != nullptr);
	CHECK(std::abs(find(list, BD::Kind::HalfTime)->firstBpm - 86.0) < 0.05);
	CHECK_EQ(find(list, BD::Kind::HalfTime)->sigs[0].numerator, 2);
}

TEST(bpm_simple_candidate_ignores_the_ramp)
{
	const BD::Beats beats = synth({ { 32, 225.0, 225.0 }, { 48, 225.0, 170.0 }, { 32, 170.0, 170.0 } }, { 4 }, 3.0, 0.006);
	const auto list = BD::buildCandidates(beats);
	const BD::Candidate* simple = find(list, BD::Kind::Simple);
	CHECK(simple != nullptr);
	if (simple)
	{
		CHECK_EQ(simple->pieces.size(), static_cast<size_t>(1));
		CHECK_EQ(simple->ramps, 0);
		CHECK(simple->rmsMs > list[0].rmsMs);	// worse fit, simpler map
		CHECK(simple->firstBpm > 170 && simple->firstBpm < 225);
	}
}

TEST(bpm_slow_song_gets_a_double_time_variant)
{
	const auto list = BD::buildCandidates(synth({ { 96, 80.0, 80.0 } }, { 4 }, 0.5, 0.004));
	const BD::Candidate* dbl = find(list, BD::Kind::DoubleTime);
	CHECK(dbl != nullptr);
	CHECK(std::abs(dbl->firstBpm - 160.0) < 0.1);
	CHECK_EQ(dbl->sigs[0].numerator, 8);
}

TEST(bpm_step_change_is_one_extra_piece_near_the_right_beat)
{
	const BD::Beats beats = synth({ { 40, 120.0, 120.0 }, { 48, 150.0, 150.0 } }, { 4 }, 2.0, 0.005);
	const BD::Candidate best = BD::buildCandidates(beats)[0];
	CHECK_EQ(best.pieces.size(), static_cast<size_t>(2));
	CHECK_EQ(best.ramps, 0);
	CHECK(std::abs(best.pieces[0].bpmStart - 120.0) < 0.1);
	CHECK(std::abs(best.pieces[1].bpmStart - 150.0) < 0.1);
	const int lead = best.pieces[0].beats - 40;	// lead-in beats added in front of the first piece
	CHECK(std::abs(best.pieces[1].startBeat - lead - 40) <= 1);
	CHECK(best.rmsMs < 8.0);
}

TEST(bpm_linear_ramp_225_to_170_is_one_ramp)
{
	const BD::Beats beats = synth({ { 32, 225.0, 225.0 }, { 48, 225.0, 170.0 }, { 32, 170.0, 170.0 } }, { 4 }, 3.0, 0.006);
	const BD::Candidate best = BD::buildCandidates(beats)[0];
	CHECK_EQ(best.ramps, 1);
	CHECK_EQ(best.pieces.size(), static_cast<size_t>(3));
	const BD::TempoPiece* ramp = nullptr;
	for (const auto& p : best.pieces)
		std::fprintf(stderr, "    piece %d x%d %.2f -> %.2f\n", p.startBeat, p.beats, p.bpmStart, p.bpmEnd);
	for (const auto& p : best.pieces)
		if (p.ramp())
			ramp = &p;
	CHECK(ramp != nullptr);
	if (ramp)
	{
		CHECK(std::abs(ramp->bpmStart - 225.0) < 3.0);
		CHECK(std::abs(ramp->bpmEnd - 170.0) < 3.0);
		CHECK(std::abs(ramp->beats - 48) <= 3);
	}
	// written into a chart (as steps) it still lands on the beats
	CHECK(best.rmsMs < 12.0);
	const BD::Timing timing = BD::toTiming(best);
	CHECK(timing.tempos.size() > 20);
	for (size_t i = 1; i < timing.tempos.size(); ++i)
		CHECK(timing.tempos[i].tick > timing.tempos[i - 1].tick);
}

TEST(bpm_44_to_34_is_a_time_signature_change)
{
	// 8 bars of 4/4, then 8 bars of 3/4 at the same tempo
	std::vector<int> bars(8, 4);
	bars.insert(bars.end(), 8, 3);
	const BD::Beats beats = synth({ { 32 + 24, 140.0, 140.0 } }, bars, 1.0, 0.004);
	const BD::Candidate best = BD::buildCandidates(beats)[0];
	CHECK_EQ(best.sigs.size(), static_cast<size_t>(2));
	CHECK_EQ(best.sigs[0].numerator, 4);
	CHECK_EQ(best.sigs[1].numerator, 3);
	CHECK_EQ(best.sigs[1].measure - best.sigs[0].measure, 8);
	const BD::Timing timing = BD::toTiming(best);
	CHECK_EQ(timing.signatures.size(), static_cast<size_t>(2));
	CHECK_EQ(timing.signatures.begin()->first, 0);
	CHECK_EQ(timing.signatures.rbegin()->second.numerator, 3);
	CHECK_EQ(timing.signatures.rbegin()->second.denominator, 4);
}

TEST(bpm_missed_beat_and_stray_downbeat_do_not_break_the_map)
{
	BD::Beats beats = synth({ { 96, 128.0, 128.0 } }, { 4 }, 0.8, 0.004);
	beats.beats.erase(beats.beats.begin() + 40);	// a missed beat
	beats.downbeats.insert(beats.downbeats.begin() + 5, beats.downbeats[5] + 0.9 * 60.0 / 128.0);	// a stray downbeat... (not on a beat of its own)
	const BD::Candidate best = BD::buildCandidates(beats)[0];
	CHECK_EQ(best.pieces.size(), static_cast<size_t>(1));
	CHECK(std::abs(best.firstBpm - 128.0) < 0.1);
	CHECK_EQ(best.sigs[0].numerator, 4);
}

TEST(bpm_too_few_beats_gives_nothing)
{
	BD::Beats beats;
	for (int i = 0; i < 5; ++i)
		beats.beats.push_back(i * 0.5);
	CHECK(BD::buildCandidates(beats).empty());
}

TEST(bpm_timing_matches_the_beats_in_seconds)
{
	const BD::Beats beats = synth({ { 40, 100.0, 100.0 }, { 40, 100.0, 140.0 }, { 40, 140.0, 140.0 } }, { 4 }, 0.0, 0.0);
	const BD::Candidate best = BD::buildCandidates(beats)[0];
	const std::vector<double> times = BD::gridTimes(best);
	CHECK_EQ(static_cast<int>(times.size()), best.totalBeats + 1);
	// no pickup room, so no lead-in; beat k of the song is beat k of the grid
	CHECK(std::abs(times[0]) < 0.01);
	CHECK(std::abs(times[100] - (tauOf(40, { 40, 100, 100 }) + tauOf(40, { 40, 100, 140 }) + tauOf(20, { 40, 140, 140 }))) < 0.02);
	CHECK(BD::toTiming(best).musicOffsetMs <= 0.0);
}

TEST(bpm_peak_picking_matches_the_reference_rules)
{
	// two adjacent frames above 0 merge to the mean, a lone peak stays, negative bumps are ignored
	BD::Activations act;
	act.beat.assign(60, -5.0f);
	act.downbeat.assign(60, -5.0f);
	act.beat[10] = 2.0f;
	act.beat[11] = 3.0f;	// local max, 10 is not (3 > 2)
	act.beat[30] = 1.0f;
	act.beat[45] = -0.5f;
	act.downbeat[31] = 4.0f;	// 31 -> nearest beat is frame 30
	const BD::Beats picked = BD::pickPeaks(act);
	// 11 is the only peak of the first pair (10 is under its neighbour); 30 stands alone
	CHECK_EQ(picked.beats.size(), static_cast<size_t>(2));
	CHECK(std::abs(picked.beats[0] - 11 / 50.0) < 1e-9);
	CHECK(std::abs(picked.beats[1] - 30 / 50.0) < 1e-9);
	CHECK_EQ(picked.downbeats.size(), static_cast<size_t>(1));
	CHECK(std::abs(picked.downbeats[0] - 30 / 50.0) < 1e-9);

	std::vector<int> starts;
	BD::chunkStarts(3000, 1500, 6, starts);
	CHECK_EQ(starts.size(), static_cast<size_t>(3));
	CHECK_EQ(starts[0], -6);
	CHECK_EQ(starts[1], 1482);
	CHECK_EQ(starts.back(), 3000 - 1494);	// the last chunk ends with the piece
	BD::chunkStarts(900, 1500, 6, starts);
	CHECK_EQ(starts.size(), static_cast<size_t>(1));
}

// ---------------------------------------------------------------- chart tools

TEST(bpm_interpolate_linear_ramp_writes_steps_from_start_to_end)
{
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f), Tempo(7680, 90.0f) };
	const int fromTick = 1920, toTick = 1920 + 1920 * 2;
	BT::interpolateTempo(score, fromTick, toTick, 225.0f, 170.0f, 120);

	float previous = 1e9f;
	bool sawStart = false, sawEnd = false;
	for (const auto& t : score.tempoChanges)
	{
		if (t.tick == fromTick)
		{
			sawStart = true;
			CHECK(t.bpm == 225.0f);
		}
		if (t.tick == toTick)
		{
			sawEnd = true;
			CHECK(t.bpm == 170.0f);
		}
		if (t.tick >= fromTick && t.tick <= toTick)
		{
			CHECK(t.bpm <= previous);	// only ever slows down
			previous = t.bpm;
		}
	}
	CHECK(sawStart);
	CHECK(sawEnd);
	// the tempos around the range stay
	CHECK(score.tempoChanges.front().tick == 0 && score.tempoChanges.front().bpm == 120.0f);
	CHECK(score.tempoChanges.back().tick == 7680 && score.tempoChanges.back().bpm == 90.0f);
	// sorted, one every 120 ticks inside
	for (size_t i = 1; i < score.tempoChanges.size(); ++i)
		CHECK(score.tempoChanges[i].tick > score.tempoChanges[i - 1].tick);
	// halfway along, halfway between
	for (const auto& t : score.tempoChanges)
		if (t.tick == fromTick + 1920)
			CHECK(std::abs(t.bpm - 197.5f) < 0.01f);
}

TEST(bpm_interpolate_replaces_changes_inside_and_keeps_the_rest)
{
	Score score;
	score.tempoChanges = { Tempo(0, 100.0f), Tempo(960, 333.0f), Tempo(1440, 444.0f), Tempo(5000, 80.0f) };
	BT::interpolateTempo(score, 480, 1920, 120.0f, 120.0f, 480);	// flat ramp: one change at the start, one at the end
	int inside = 0;
	for (const auto& t : score.tempoChanges)
		if (t.tick > 480 && t.tick < 1920)
			++inside;
	CHECK_EQ(inside, 0);
	// a flat 120 needs just one change (at 480); the 80 at 5000 stays
	CHECK_EQ(score.tempoChanges.size(), static_cast<size_t>(3));
	CHECK_EQ(score.tempoChanges[1].tick, 480);
	CHECK(score.tempoChanges[1].bpm == 120.0f);
	CHECK_EQ(score.tempoChanges[2].tick, 5000);

	// nonsense ranges change nothing
	const auto before = score.tempoChanges.size();
	BT::interpolateTempo(score, 900, 900, 100.0f, 200.0f, 120);
	BT::interpolateTempo(score, 900, 1000, 100.0f, 200.0f, 0);
	CHECK_EQ(score.tempoChanges.size(), before);
}

TEST(bpm_interpolate_ease_curves_keep_their_ends)
{
	for (auto curve : { BT::Curve::Linear, BT::Curve::EaseIn, BT::Curve::EaseOut, BT::Curve::EaseInOut })
	{
		CHECK(BT::shape(curve, 0.0) == 0.0);
		CHECK(std::abs(BT::shape(curve, 1.0) - 1.0) < 1e-12);
		CHECK(BT::shape(curve, 0.5) >= 0.0);
	}
	CHECK(BT::shape(BT::Curve::EaseIn, 0.5) < 0.5);
	CHECK(BT::shape(BT::Curve::EaseOut, 0.5) > 0.5);
	CHECK(std::abs(BT::shape(BT::Curve::EaseInOut, 0.5) - 0.5) < 1e-12);

	Score score;
	score.tempoChanges = { Tempo(0, 100.0f) };
	BT::interpolateTempo(score, 0, 1920, 100.0f, 200.0f, 240, BT::Curve::EaseIn);
	CHECK(score.tempoChanges.front().bpm == 100.0f);
	CHECK(score.tempoChanges.back().bpm == 200.0f);
	// ease in: slow start, so the middle is under the straight line
	for (const auto& t : score.tempoChanges)
		if (t.tick == 960)
			CHECK(t.bpm < 150.0f);
}

TEST(bpm_apply_timing_keeps_ticks_or_keeps_song_time)
{
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f) };
	addTap(score, 480 * 8, 3, 3);	// 4 s at 120 BPM

	BD::Timing timing;
	timing.tempos = { Tempo(0, 240.0f) };
	timing.signatures[0] = TimeSignature{ 0, 4, 4 };
	timing.musicOffsetMs = 0;

	Score keepTicks = score;
	BT::applyTiming(keepTicks, timing, true);
	CHECK_EQ(keepTicks.notes.begin()->second.tick, 480 * 8);
	CHECK(keepTicks.tempoChanges[0].bpm == 240.0f);

	Score keepTime = score;
	BT::applyTiming(keepTime, timing, false);
	CHECK_EQ(keepTime.notes.begin()->second.tick, 480 * 16);	// twice the beats for the same 4 s

	// an offset moves it again: audio time = chart time - offset
	timing.musicOffsetMs = -1000;	// chart time 0 is 1 s into the song
	Score shifted = score;
	BT::applyTiming(shifted, timing, false);
	// song time 4 s -> chart time 3 s (chart time = song time + offset) -> 12 beats at 240 BPM
	CHECK_EQ(shifted.notes.begin()->second.tick, 480 * 12);
	CHECK(std::abs(shifted.metadata.musicOffset + 1000.0f) < 0.01f);
}

TEST(bpm_tick_second_conversions_round_trip)
{
	const std::vector<Tempo> tempos = { Tempo(0, 120.0f), Tempo(1920, 60.0f), Tempo(3840, 240.0f) };
	for (int tick : { 0, 100, 1920, 2500, 3840, 9000 })
	{
		const double seconds = BT::tickToSeconds(tick, tempos);
		CHECK(std::abs(BT::secondsToTick(seconds, tempos) - tick) < 1e-6);
	}
	CHECK(std::abs(BT::tickToSeconds(1920, tempos) - 2.0) < 1e-9);
	CHECK(std::abs(BT::tickToSeconds(3840, tempos) - 6.0) < 1e-9);
}

// ---------------------------------------------------------------- the real thing (opt-in)
// CANVAS_BPM_TRACK=<audio file> [CANVAS_BPM_APPDIR=<folder with res/>] [CANVAS_BPM_DUMP=<empty folder>]
// Skipped when the variable is not set. Prints what it finds.

TEST(bpm_real_song_end_to_end)
{
	const char* track = std::getenv("CANVAS_BPM_TRACK");
	if (!track)
		return;
	const char* appDir = std::getenv("CANVAS_BPM_APPDIR");
	const BD::Runtime runtime = BD::locate(appDir ? appDir : "");
	std::string why;
	if (!BD::available(runtime, &why))
	{
		std::fprintf(stderr, "  beat detect not available: %s\n", why.c_str());
		return;
	}
	const char* dump = std::getenv("CANVAS_BPM_DUMP");

	int rate = 0;
	std::string error;
	std::vector<float> mono = BD::decodeMono(track, rate, error);
	CHECK(!mono.empty());
	const std::vector<float> signal = BD::resample(mono, rate, BD::SAMPLE_RATE);
	std::fprintf(stderr, "  decoded %zu samples at %d Hz, %zu at 22050\n", mono.size(), rate, signal.size());

	BD::Activations act;
	const BD::Result result = BD::detectSignal(signal, runtime, {}, nullptr, {}, &act);
	std::fprintf(stderr, "  took %.1f s for %.1f s of music: %s\n", result.tookSeconds, result.songSeconds, result.ok ? "ok" : result.error.c_str());
	CHECK(result.ok);
	for (const auto& c : result.candidates)
	{
		std::fprintf(stderr, "  [%d] %s | first %.3f | rms %.1f ms max %.1f ms | start %.3f s\n", static_cast<int>(c.kind),
			c.description.c_str(), c.firstBpm, c.rmsMs, c.maxMs, c.startSec);
		for (const auto& g : c.sigs)
			std::fprintf(stderr, "      sig from measure %d: %d/4\n", g.measure, g.numerator);
	}
	if (!result.candidates.empty())
		for (const auto& p : result.candidates[0].pieces)
			std::fprintf(stderr, "      piece beat %d x%d: %.3f -> %.3f\n", p.startBeat, p.beats, p.bpmStart, p.bpmEnd);

	if (dump)
	{
		auto writeFloats = [&](const char* name, const std::vector<float>& v) {
			std::ofstream out(std::string(dump) + "/" + name, std::ios::binary);
			out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(float)));
		};
		writeFloats("signal22k.f32", signal);
		int frames = 0;
		writeFloats("mel.f32", BD::logMel(signal, frames));
		writeFloats("beat_logits.f32", act.beat);
		writeFloats("downbeat_logits.f32", act.downbeat);
		std::ofstream txt(std::string(dump) + "/beats.txt");
		for (double b : result.beats.beats)
			txt << "b " << b << "\n";
		for (double d : result.beats.downbeats)
			txt << "d " << d << "\n";
	}
}
