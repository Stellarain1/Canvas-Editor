#include "BpmTools.h"
#include "../../Constants.h"
#include <algorithm>
#include <cmath>

using namespace MikuMikuWorld;

namespace Canvas::BpmTools
{
	double shape(Curve curve, double u)
	{
		u = std::clamp(u, 0.0, 1.0);
		switch (curve)
		{
		case Curve::EaseIn: return u * u;
		case Curve::EaseOut: return 1.0 - (1.0 - u) * (1.0 - u);
		case Curve::EaseInOut: return u < 0.5 ? 2.0 * u * u : 1.0 - 2.0 * (1.0 - u) * (1.0 - u);
		default: return u;
		}
	}

	double tickToSeconds(int tick, const std::vector<Tempo>& tempos)
	{
		if (tempos.empty())
			return tick * 60.0 / 120.0 / TICKS_PER_BEAT;
		double seconds = 0;
		for (size_t i = 0; i < tempos.size(); ++i)
		{
			const int from = tempos[i].tick;
			if (tick <= from && i > 0)
				break;
			const int to = i + 1 < tempos.size() ? std::min(tempos[i + 1].tick, tick) : tick;
			if (to > from)
				seconds += (to - from) * 60.0 / tempos[i].bpm / TICKS_PER_BEAT;
			if (to >= tick)
				break;
		}
		return seconds;
	}

	double secondsToTick(double seconds, const std::vector<Tempo>& tempos)
	{
		if (tempos.empty())
			return seconds * 120.0 / 60.0 * TICKS_PER_BEAT;
		double elapsed = 0;
		for (size_t i = 0; i < tempos.size(); ++i)
		{
			const double tickSeconds = 60.0 / tempos[i].bpm / TICKS_PER_BEAT;
			if (i + 1 < tempos.size())
			{
				const double span = (tempos[i + 1].tick - tempos[i].tick) * tickSeconds;
				if (seconds < elapsed + span)
					return tempos[i].tick + (seconds - elapsed) / tickSeconds;
				elapsed += span;
			}
			else
				return tempos[i].tick + (seconds - elapsed) / tickSeconds;
		}
		return 0;
	}

	int interpolateTempo(Score& score, int fromTick, int toTick, float startBpm, float endBpm, int stepTicks, Curve curve)
	{
		if (fromTick < 0 || toTick <= fromTick || stepTicks <= 0)
			return static_cast<int>(score.tempoChanges.size());

		auto& tempos = score.tempoChanges;
		std::sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
		tempos.erase(std::remove_if(tempos.begin(), tempos.end(), [&](const Tempo& t) { return t.tick >= fromTick && t.tick <= toTick; }), tempos.end());

		const double span = toTick - fromTick;
		auto bpmAt = [&](int tick) {
			const double value = startBpm + (endBpm - startBpm) * shape(curve, (tick - fromTick) / span);
			// two decimals is far below anything audible and keeps the file small
			return std::clamp(static_cast<float>(std::round(value * 100.0) / 100.0), MIN_BPM, MAX_BPM);
		};

		std::vector<Tempo> ramp;
		auto push = [&](int tick) {
			const float bpm = bpmAt(tick);
			if (ramp.empty() || ramp.back().bpm != bpm)
				ramp.push_back(Tempo(tick, bpm));
		};
		for (int tick = fromTick; tick < toTick; tick += stepTicks)
			push(tick);
		// the end of the range always carries the end BPM, even when it equals the step before
		const float endValue = bpmAt(toTick);
		if (ramp.empty() || ramp.back().bpm != endValue)
			ramp.push_back(Tempo(toTick, endValue));

		tempos.insert(tempos.end(), ramp.begin(), ramp.end());
		std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
		return static_cast<int>(tempos.size());
	}

	void retimeScore(Score& score, const std::vector<Tempo>& oldTempos, double oldOffsetMs, const std::vector<Tempo>& newTempos,
		double newOffsetMs)
	{
		// audio time = chart time - offset
		auto move = [&](int tick) {
			if (tick < 0)
				return tick;
			const double audio = tickToSeconds(tick, oldTempos) - oldOffsetMs / 1000.0;
			const double chartTime = audio + newOffsetMs / 1000.0;
			return std::max(0, static_cast<int>(std::lround(secondsToTick(std::max(0.0, chartTime), newTempos))));
		};

		for (auto& [id, note] : score.notes)
			note.tick = move(note.tick);
		for (auto& change : score.hiSpeedChanges)
			change.tick = move(change.tick);
		for (auto& change : score.seVolumeChanges)
			change.tick = move(change.tick);
		for (auto& skill : score.skills)
			skill.tick = move(skill.tick);
		score.fever.chanceTick = move(score.fever.chanceTick);
		score.fever.startTick = move(score.fever.startTick);
	}

	void applyTiming(Score& score, const BeatDetect::Timing& timing, bool keepNoteTicks)
	{
		const std::vector<Tempo> oldTempos = score.tempoChanges;
		const double oldOffset = score.metadata.musicOffset;

		score.tempoChanges = timing.tempos;
		score.timeSignatures = timing.signatures;
		score.metadata.musicOffset = static_cast<float>(timing.musicOffsetMs);

		if (!keepNoteTicks)
			retimeScore(score, oldTempos, oldOffset, timing.tempos, timing.musicOffsetMs);
	}
}
