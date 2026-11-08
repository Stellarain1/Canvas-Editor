#include "ScoreStats.h"
#include "Score.h"
#include "Constants.h"
#include <algorithm>

namespace MikuMikuWorld
{
	namespace
	{
		float getComboWeight(const Note& note)
		{
			const NoteType type = note.getType();
			if (note.isFlick() || note.friction || type == NoteType::HoldMid)
				return note.critical ? 0.2f : 0.1f;

			return note.critical ? 2.0f : 1.0f;
		}
	}

	std::vector<ComboEvent> calculateComboEvents(const Score& score)
	{
		std::vector<ComboEvent> comboEvents;
		comboEvents.reserve(score.notes.size());

		for (const auto& [id, note] : score.notes)
		{
			(void)id;
			const HoldNote* hold = nullptr;
			const NoteType type = note.getType();

			if (type == NoteType::Hold)
			{
				const auto holdIt = score.holdNotes.find(note.ID);
				if (holdIt == score.holdNotes.end())
					continue;

				hold = &holdIt->second;
				if (hold->startType != HoldNoteType::Normal)
					continue;
			}
			else if (type == NoteType::HoldEnd)
			{
				const auto holdIt = score.holdNotes.find(note.parentID);
				if (holdIt == score.holdNotes.end())
					continue;

				hold = &holdIt->second;
				if (hold->endType != HoldNoteType::Normal)
					continue;
			}
			else if (type == NoteType::HoldMid)
			{
				const auto holdIt = score.holdNotes.find(note.parentID);
				if (holdIt == score.holdNotes.end())
					continue;

				hold = &holdIt->second;
				const int stepIndex = findHoldStep(*hold, note.ID);
				if (stepIndex != -1 && hold->steps[stepIndex].type == HoldStepType::Hidden)
					continue;
			}

			if (hold != nullptr && hold->isGuide())
				continue;

			comboEvents.push_back({ note.tick, getComboWeight(note), true });
		}

		constexpr int halfBeat = TICKS_PER_BEAT / 2;
		for (const auto& [holdId, hold] : score.holdNotes)
		{
			if (hold.isGuide())
				continue;

			const auto holdStartIt = score.notes.find(holdId);
			const auto holdEndIt = score.notes.find(hold.end);
			if (holdStartIt == score.notes.end() || holdEndIt == score.notes.end())
				continue;

			const Note& holdStart = holdStartIt->second;
			const int startTick = holdStart.tick;
			int endTick = holdEndIt->second.tick;
			int eighthTick = startTick + halfBeat;
			if (eighthTick % halfBeat)
				eighthTick -= eighthTick % halfBeat;

			if (eighthTick == startTick || eighthTick == endTick)
				continue;

			if (endTick % halfBeat)
				endTick += halfBeat - (endTick % halfBeat);

			for (int tick = eighthTick; tick < endTick; tick += halfBeat)
				comboEvents.push_back({ tick, holdStart.critical ? 0.2f : 0.1f, true });
		}

		std::stable_sort(comboEvents.begin(), comboEvents.end(), [](const ComboEvent& lhs, const ComboEvent& rhs)
		{
			return lhs.tick < rhs.tick;
		});
		return comboEvents;
	}

	std::vector<int> calculateComboTicks(const Score& score)
	{
		const std::vector<ComboEvent> comboEvents = calculateComboEvents(score);
		std::vector<int> comboTicks;
		comboTicks.reserve(comboEvents.size());
		for (const ComboEvent& event : comboEvents)
			comboTicks.push_back(event.tick);

		return comboTicks;
	}

	ScoreStats::ScoreStats()
	{
		reset();
	}

	void ScoreStats::reset()
	{
		resetCounts();
		resetCombo();
	}

	void ScoreStats::resetCounts()
	{
		taps = flicks = holds = steps = traces = total = 0;
	}

	void ScoreStats::resetCombo()
	{
		combo = 0;
	}

	void ScoreStats::calculateStats(const Score& score)
	{
		taps = std::count_if(score.notes.begin(), score.notes.end(), [](const auto& n)
		{
			const Note& note = n.second;
			return note.getType() == NoteType::Tap && !note.isFlick() && !note.friction;
		});

		holds = std::count_if(score.notes.begin(), score.notes.end(),
			[](const auto& n) { return n.second.getType() == NoteType::Hold; });

		steps = std::count_if(score.notes.begin(), score.notes.end(),
			[](const auto& n) { return n.second.getType() == NoteType::HoldMid; });

		flicks = std::count_if(score.notes.begin(), score.notes.end(),
			[](const auto& n) { return n.second.isFlick(); });

		traces = std::count_if(score.notes.begin(), score.notes.end(),
			[](const auto& n) { return n.second.friction; });

		total = score.notes.size();
		calculateCombo(score);
	}

	void ScoreStats::calculateCombo(const Score& score)
	{
		combo = static_cast<int>(calculateComboEvents(score).size());
	}
}
