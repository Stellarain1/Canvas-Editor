#include "ChartTools.h"
#include "../../Constants.h"
#include "../../MathUtils.h"
#include "../../Tempo.h"
#include <algorithm>
#include <cmath>
#include <set>

using namespace MikuMikuWorld;

namespace Canvas::Tools
{
	// ---- divisions ------------------------------------------------------------------------------------

	double divisionStep(int division)
	{
		return static_cast<double>(TICKS_PER_BEAT) * 4.0 / std::clamp(division, 1, MAX_DIVISION);
	}

	namespace
	{
		int measureStart(int tick, const Score& score, int* nextStart = nullptr)
		{
			const int measure = accumulateMeasures(std::max(0, tick), TICKS_PER_BEAT, score.timeSignatures);
			if (nextStart)
				*nextStart = measureToTicks(measure + 1, TICKS_PER_BEAT, score.timeSignatures);
			return measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures);
		}
	}

	int snapToDivision(int tick, int division, const Score& score)
	{
		if (score.timeSignatures.empty())
			return std::max(0, static_cast<int>(std::llround(std::llround(tick / divisionStep(division)) * divisionStep(division))));
		int next = 0;
		const int start = measureStart(tick, score, &next);
		const double step = divisionStep(division);
		const long long k = std::llround((tick - start) / step);
		const int snapped = start + static_cast<int>(std::llround(k * step));
		return std::max(0, std::min(snapped, next));
	}

	int nextDivisionTick(int tick, int division, const Score& score)
	{
		const double step = divisionStep(division);
		if (score.timeSignatures.empty())
			return static_cast<int>(std::llround((std::floor(tick / step + 1e-9) + 1) * step));
		int next = 0;
		const int start = measureStart(tick, score, &next);
		long long k = static_cast<long long>(std::floor((tick - start) / step + 1e-9)) + 1;
		int candidate = start + static_cast<int>(std::llround(k * step));
		while (candidate <= tick)
			candidate = start + static_cast<int>(std::llround(++k * step));
		return std::min(candidate, next);
	}

	int previousDivisionTick(int tick, int division, const Score& score)
	{
		if (tick <= 0)
			return 0;
		const double step = divisionStep(division);
		if (score.timeSignatures.empty())
			return std::max(0, static_cast<int>(std::llround((std::ceil(tick / step - 1e-9) - 1) * step)));
		// the grid of the measure holding tick - 1: its largest line before `tick`
		const int start = measureStart(tick - 1, score);
		long long k = static_cast<long long>(std::ceil((tick - start) / step - 1e-9)) - 1;
		int candidate = start + static_cast<int>(std::llround(k * step));
		while (candidate >= tick && k > 0)
			candidate = start + static_cast<int>(std::llround(--k * step));
		return std::max(start, candidate);
	}

	// ---- repeat hold mids ------------------------------------------------------------------------

	int holdOfSelection(const Score& score, const std::vector<int>& noteIds)
	{
		int found = -1;
		for (int id : noteIds)
		{
			auto it = score.notes.find(id);
			if (it == score.notes.end())
				continue;
			const Note& note = it->second;
			int hold = -1;
			if (note.getType() == NoteType::Hold)
				hold = note.ID;
			else if (note.getType() == NoteType::HoldMid || note.getType() == NoteType::HoldEnd)
				hold = note.parentID;
			if (hold < 0 || !score.holdNotes.count(hold))
				return -1;
			if (found >= 0 && found != hold)
				return -1;
			found = hold;
		}
		return found;
	}

	namespace
	{
		struct Point
		{
			int id, tick, lane, width;
			EaseType ease;
			HoldStepType type;
			bool isStep;
		};

		std::vector<Point> holdPoints(const Score& score, const HoldNote& hold)
		{
			std::vector<Point> points;
			const Note& start = score.notes.at(hold.start.ID);
			points.push_back({ start.ID, start.tick, start.lane, start.width, hold.start.ease, HoldStepType::Normal, false });
			for (const HoldStep& step : hold.steps)
			{
				auto it = score.notes.find(step.ID);
				if (it != score.notes.end())
					points.push_back({ step.ID, it->second.tick, it->second.lane, it->second.width, step.ease, step.type, true });
			}
			const Note& end = score.notes.at(hold.end);
			points.push_back({ end.ID, end.tick, end.lane, end.width, EaseType::Linear, HoldStepType::Normal, false });
			std::stable_sort(points.begin() + 1, points.end() - 1, [](const Point& a, const Point& b) { return a.tick < b.tick; });
			return points;
		}

		Note makeMid(const Note& start, int tick, int lane, int width)
		{
			Note mid(NoteType::HoldMid, tick, lane, width);
			mid.ID = nextID++;
			mid.parentID = start.ID;
			mid.critical = start.critical;
			mid.layer = start.layer;
			return mid;
		}
	}

	RepeatMidsResult repeatHoldMids(Score& score, int holdId, const std::vector<int>& patternMids, const RepeatMidsOptions& options)
	{
		RepeatMidsResult result;
		auto holdIt = score.holdNotes.find(holdId);
		if (holdIt == score.holdNotes.end())
		{
			result.error = "no hold";
			return result;
		}
		HoldNote& hold = holdIt->second;
		const std::vector<Point> points = holdPoints(score, hold);
		const Note start = score.notes.at(hold.start.ID);
		const int endTick = points.back().tick;
		std::set<int> occupied;
		for (const Point& p : points)
			occupied.insert(p.tick);

		auto add = [&](int tick, int lane, int width, HoldStepType type, EaseType ease) {
			if (tick <= points.front().tick || tick >= endTick || occupied.count(tick))
				return;
			width = std::clamp(width, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
			lane = std::clamp(lane, MIN_LANE, NUM_LANES - width);
			Note mid = makeMid(start, tick, lane, width);
			score.notes[mid.ID] = mid;
			hold.steps.push_back({ mid.ID, type, ease });
			occupied.insert(tick);
			++result.added;
		};

		if (options.mode == RepeatMidsOptions::Mode::EveryStep)
		{
			const int step = std::max(1, options.stepTicks);
			const auto pathOf = [&](int tick, int& lane, int& width) {
				for (size_t i = 0; i + 1 < points.size(); ++i)
				{
					const Point& a = points[i];
					const Point& b = points[i + 1];
					if (tick < a.tick || tick > b.tick || b.tick == a.tick)
						continue;
					const float t = static_cast<float>(tick - a.tick) / (b.tick - a.tick);
					const auto ease = getEaseFunction(a.isStep && a.type == HoldStepType::Skip ? EaseType::Linear : a.ease);
					const float left = ease(static_cast<float>(a.lane), static_cast<float>(b.lane), t);
					const float right = ease(static_cast<float>(a.lane + a.width), static_cast<float>(b.lane + b.width), t);
					lane = static_cast<int>(std::lround(left));
					width = std::max(1, static_cast<int>(std::lround(right - left)));
					return;
				}
			};
			for (int tick = points.front().tick + step; tick < endTick; tick += step)
			{
				int lane = start.lane, width = start.width;
				pathOf(tick, lane, width);
				add(tick, lane, width, options.stepType, options.ease);
			}
			sortHoldSteps(score, hold);
			return result;
		}

		// Pattern: the chosen mids, measured from the hold point just before the first of them
		std::vector<Point> pattern;
		for (const Point& p : points)
			if (p.isStep && std::find(patternMids.begin(), patternMids.end(), p.id) != patternMids.end())
				pattern.push_back(p);
		if (pattern.empty())
		{
			result.error = "select the mids to repeat";
			return result;
		}
		const Point* anchor = &points.front();
		for (const Point& p : points)
		{
			if (p.tick >= pattern.front().tick)
				break;
			anchor = &p;
		}
		const int period = pattern.back().tick - anchor->tick;
		if (period <= 0)
		{
			result.error = "the mids span no time";
			return result;
		}
		int lo = anchor->lane, hi = anchor->lane + anchor->width;
		for (const Point& p : pattern)
		{
			lo = std::min(lo, p.lane);
			hi = std::max(hi, p.lane + p.width);
		}
		const int drift = pattern.back().lane - anchor->lane;
		for (int r = 1; options.untilEnd || r <= options.count; ++r)
		{
			if (pattern.front().tick + r * period >= endTick)
				break;
			for (const Point& p : pattern)
			{
				const int tick = p.tick + r * period;
				if (tick >= endTick)
					break;
				const int shift = options.relative ? r * drift : 0;
				int lane = p.lane + shift;
				// mirrored inside the (shifted) span the pattern covers
				if (options.mirror && (r % 2) == 1)
					lane = (lo + shift) + (hi + shift) - (lane + p.width);
				add(tick, lane, p.width, p.type, p.ease);
			}
			if (r > 10000)
				break;
		}
		sortHoldSteps(score, hold);
		if (result.added == 0)
			result.error = "no room left on the hold";
		return result;
	}

	// ---- hi-speed interpolation ------------------------------------------------------------------

	float hiSpeedAt(const Score& score, int tick, int layer)
	{
		float speed = 1.0f;
		int best = -1;
		for (const HiSpeedChange& change : score.hiSpeedChanges)
			if (change.layer == layer && change.tick <= tick && change.tick >= best)
			{
				best = change.tick;
				speed = change.speed;
			}
		return speed;
	}

	int interpolateHiSpeeds(Score& score, const InterpolateOptions& o)
	{
		const int from = std::min(o.fromTick, o.toTick), to = std::max(o.fromTick, o.toTick);
		const float fromSpeed = o.fromTick <= o.toTick ? o.fromSpeed : o.toSpeed;
		const float toSpeed = o.fromTick <= o.toTick ? o.toSpeed : o.fromSpeed;
		if (to <= from)
			return 0;
		if (o.replace)
			score.hiSpeedChanges.erase(std::remove_if(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(),
				[&](const HiSpeedChange& c) { return c.layer == o.layer && c.tick >= from && c.tick <= to; }), score.hiSpeedChanges.end());
		auto ease = [&](float t) {
			switch (o.ease)
			{
			case SpeedEase::In: return t * t;
			case SpeedEase::Out: return 1.0f - (1.0f - t) * (1.0f - t);
			case SpeedEase::InOut: return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
			default: return t;
			}
		};
		const int step = std::max(1, o.stepTicks);
		int added = 0;
		for (int tick = from;; tick += step)
		{
			const bool last = tick >= to;
			const int at = last ? to : tick;
			const float t = static_cast<float>(at - from) / (to - from);
			float speed = fromSpeed + (toSpeed - fromSpeed) * ease(t);
			speed = std::round(speed * 10000.0f) / 10000.0f;
			score.hiSpeedChanges.erase(std::remove_if(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(),
				[&](const HiSpeedChange& c) { return c.layer == o.layer && c.tick == at; }), score.hiSpeedChanges.end());
			score.hiSpeedChanges.push_back({ at, speed, o.layer });
			++added;
			if (last)
				break;
		}
		std::stable_sort(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(), [](const HiSpeedChange& a, const HiSpeedChange& b) { return a.tick < b.tick; });
		return added;
	}
}
