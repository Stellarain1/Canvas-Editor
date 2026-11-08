#include "ChartTransforms.h"
#include "../../MathUtils.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace MikuMikuWorld;

namespace Canvas
{
	int lowerExtendedEases(Score& score)
	{
		int rewritten = 0;
		for (auto& [holdId, hold] : score.holdNotes)
		{
			// Walk the joints (start, non-skip steps, end). Collect insertions first so the
			// step vector is not modified while iterating.
			struct Insertion { int afterJointNote; int beforeJointNote; EaseType first; EaseType second; EaseType* easeSlot; };
			std::vector<Insertion> insertions;

			std::vector<std::pair<int, EaseType*>> joints;
			joints.push_back({ hold.start.ID, &hold.start.ease });
			for (auto& step : hold.steps)
				if (step.type != HoldStepType::Skip)
					joints.push_back({ step.ID, &step.ease });
			joints.push_back({ hold.end, nullptr });

			for (size_t i = 0; i + 1 < joints.size(); ++i)
			{
				EaseType* ease = joints[i].second;
				if (!ease || !isExtendedEase(*ease))
					continue;

				const bool inOut = *ease == EaseType::EaseInOut;
				insertions.push_back({ joints[i].first, joints[i + 1].first,
					inOut ? EaseType::EaseIn : EaseType::EaseOut,
					inOut ? EaseType::EaseOut : EaseType::EaseIn,
					ease });
			}

			// ease slots point into hold.steps, so rewrite them all before any step is appended
			std::vector<HoldStep> newSteps;
			for (const auto& ins : insertions)
			{
				const Note& a = score.notes.at(ins.afterJointNote);
				const Note& b = score.notes.at(ins.beforeJointNote);

				Note mid(NoteType::HoldMid);
				mid.ID = nextID++;
				mid.parentID = hold.start.ID;
				mid.tick = (a.tick + b.tick) / 2;
				// the ease-in / ease-out halves meet exactly half-way across both edges
				const float left = (a.lane + b.lane) * 0.5f;
				const float right = (a.lane + a.width + b.lane + b.width) * 0.5f;
				mid.lane = static_cast<int>(std::lround(left));
				mid.width = std::max(MIN_NOTE_WIDTH, static_cast<int>(std::lround(right)) - mid.lane);
				mid.lane = std::clamp(mid.lane, MIN_LANE, MAX_LANE - mid.width + 1);
				mid.critical = score.notes.at(hold.start.ID).critical;
				mid.layer = a.layer;
				mid.speedRatio = score.notes.at(hold.start.ID).speedRatio;

				*ins.easeSlot = ins.first;
				score.notes[mid.ID] = mid;
				newSteps.push_back(HoldStep{ mid.ID, HoldStepType::Hidden, ins.second });
				++rewritten;
			}

			if (!newSteps.empty())
			{
				hold.steps.insert(hold.steps.end(), newSteps.begin(), newSteps.end());
				sortHoldSteps(score, hold);
			}
		}

		return rewritten;
	}

	LayerMergeReport mergeLayersToDefault(Score& score)
	{
		LayerMergeReport report;
		for (auto& [id, note] : score.notes)
		{
			if (note.layer != 0)
			{
				if (note.getType() == NoteType::Tap || note.getType() == NoteType::Hold)
					++report.notesMoved;
				note.layer = 0;
			}
		}

		auto& changes = score.hiSpeedChanges;
		const size_t before = changes.size();
		changes.erase(std::remove_if(changes.begin(), changes.end(), [](const HiSpeedChange& c) { return c.layer != 0; }), changes.end());
		report.hiSpeedsDropped = static_cast<int>(before - changes.size());

		if (score.layers.empty())
			score.layers.push_back({ "default" });
		score.layers.resize(1);
		score.layers[0].hidden = false;
		return report;
	}

	int countExtendedGuides(const Score& score)
	{
		int count = 0;
		for (const auto& [id, hold] : score.holdNotes)
			if (hold.isGuide() && (isExtendedGuideColor(hold.guideColor) || hold.fadeType != FadeType::Out))
				++count;
		return count;
	}

	int countExtendedEases(const Score& score)
	{
		int count = 0;
		for (const auto& [id, hold] : score.holdNotes)
		{
			count += isExtendedEase(hold.start.ease);
			for (const auto& step : hold.steps)
				count += step.type != HoldStepType::Skip && isExtendedEase(step.ease);
		}
		return count;
	}

	int countNotesWithSpeedRatio(const Score& score)
	{
		int count = 0;
		for (const auto& [id, note] : score.notes)
			if ((note.getType() == NoteType::Tap || note.getType() == NoteType::Hold) && std::abs(note.speedRatio - 1.0f) > 0.0001f)
				++count;
		return count;
	}

	bool usesLayers(const Score& score)
	{
		for (const auto& [id, note] : score.notes)
			if (note.layer != 0)
				return true;
		for (const auto& change : score.hiSpeedChanges)
			if (change.layer != 0)
				return true;
		return false;
	}

	void normalizeEvents(Score& score)
	{
		auto& tempos = score.tempoChanges;
		std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
		// the later of two tempo changes on one tick wins, like in the timeline
		for (size_t i = 1; i < tempos.size();)
		{
			if (tempos[i].tick == tempos[i - 1].tick)
				tempos.erase(tempos.begin() + (i - 1));
			else
				++i;
		}
		if (tempos.empty() || tempos.front().tick != 0)
			tempos.insert(tempos.begin(), Tempo(0, tempos.empty() ? 120.0f : tempos.front().bpm));

		auto& speeds = score.hiSpeedChanges;
		std::stable_sort(speeds.begin(), speeds.end(), [](const HiSpeedChange& a, const HiSpeedChange& b)
		{
			return a.layer != b.layer ? a.layer < b.layer : a.tick < b.tick;
		});
		for (size_t i = 1; i < speeds.size();)
		{
			if (speeds[i].tick == speeds[i - 1].tick && speeds[i].layer == speeds[i - 1].layer)
				speeds.erase(speeds.begin() + (i - 1));
			else
				++i;
		}
		std::stable_sort(speeds.begin(), speeds.end(), [](const HiSpeedChange& a, const HiSpeedChange& b) { return a.tick < b.tick; });

		auto& volumes = score.seVolumeChanges;
		std::stable_sort(volumes.begin(), volumes.end(), [](const SEVolumeChange& a, const SEVolumeChange& b) { return a.tick < b.tick; });

		std::sort(score.skills.begin(), score.skills.end(), [](const SkillTrigger& a, const SkillTrigger& b) { return a.tick < b.tick; });

		int maxLayer = 0;
		for (const auto& [id, note] : score.notes)
			maxLayer = std::max(maxLayer, note.layer);
		for (const auto& change : score.hiSpeedChanges)
			maxLayer = std::max(maxLayer, change.layer);
		if (score.layers.empty())
			score.layers.push_back({ "default" });
		while (static_cast<int>(score.layers.size()) <= maxLayer)
			score.layers.push_back({ "Layer " + std::to_string(score.layers.size()) });
	}

	bool playableTickRange(const Score& score, int& firstTick, int& lastTick)
	{
		firstTick = std::numeric_limits<int>::max();
		lastTick = std::numeric_limits<int>::min();
		for (const auto& [id, note] : score.notes)
		{
			// a hold point whose hold is missing (a broken chart, which Auto Fix repairs) is not playable
			bool playable = true;
			if (note.getType() == NoteType::Hold)
			{
				const auto hold = score.holdNotes.find(note.ID);
				playable = hold != score.holdNotes.end() && hold->second.startType == HoldNoteType::Normal;
			}
			else if (note.getType() == NoteType::HoldEnd)
			{
				const auto hold = score.holdNotes.find(note.parentID);
				playable = hold != score.holdNotes.end() && hold->second.endType == HoldNoteType::Normal;
			}
			else if (note.getType() == NoteType::HoldMid)
			{
				const auto holdIt = score.holdNotes.find(note.parentID);
				if (holdIt == score.holdNotes.end())
					playable = false;
				else
				{
					const HoldNote& hold = holdIt->second;
					const int stepIndex = findHoldStep(hold, note.ID);
					playable = !hold.isGuide() && stepIndex >= 0 && hold.steps[stepIndex].type != HoldStepType::Hidden;
				}
			}

			if (!playable)
				continue;

			firstTick = std::min(firstTick, note.tick);
			lastTick = std::max(lastTick, note.tick);
		}

		return firstTick <= lastTick;
	}
}
