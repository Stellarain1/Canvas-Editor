#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>
#include "PreviewData.h"
#include "ApplicationConfiguration.h"
#include "Constants.h"
#include "ResourceManager.h"
#include "ScoreContext.h"
#include "ScoreStats.h"

namespace MikuMikuWorld::Engine
{
	namespace
	{
		constexpr float SCORE_TEAM_POWER = 300000.0f;
		constexpr float SCORE_RATING = 26.0f;

		float clamp01(float value)
		{
			return std::max(0.0f, std::min(1.0f, value));
		}

		float lerpScore(float value, float start, float end, float startPos, float endPos)
		{
			if (end <= start)
				return endPos;

			return ((value - start) / (end - start)) * (endPos - startPos) + startPos;
		}

		std::pair<char, float> scoreRankAndBar(float score)
		{
			const float rankBorder = 1200000.0f + (SCORE_RATING - 5.0f) * 4100.0f;
			const float rankS = 1040000.0f + (SCORE_RATING - 5.0f) * 5200.0f;
			const float rankA = 840000.0f + (SCORE_RATING - 5.0f) * 4200.0f;
			const float rankB = 400000.0f + (SCORE_RATING - 5.0f) * 2000.0f;
			const float rankC = 20000.0f + (SCORE_RATING - 5.0f) * 100.0f;

			constexpr float rankBorderPos = 1650.0f / 1650.0f;
			constexpr float rankSPos = 1478.0f / 1650.0f;
			constexpr float rankAPos = 1234.0f / 1650.0f;
			constexpr float rankBPos = 990.0f / 1650.0f;
			constexpr float rankCPos = 746.0f / 1650.0f;

			if (score >= rankBorder)
				return { 's', rankBorderPos };
			if (score >= rankS)
				return { 's', clamp01(lerpScore(score, rankS, rankBorder, rankSPos, rankBorderPos)) };
			if (score >= rankA)
				return { 'a', clamp01(lerpScore(score, rankA, rankS, rankAPos, rankSPos)) };
			if (score >= rankB)
				return { 'b', clamp01(lerpScore(score, rankB, rankA, rankBPos, rankAPos)) };
			if (score >= rankC)
				return { 'c', clamp01(lerpScore(score, rankC, rankB, rankCPos, rankBPos)) };

			return { 'd', clamp01((score / std::max(rankC, 1.0f)) * rankCPos) };
		}
	}

	struct DrawingHoldStep
	{
		int tick;
		double time;
		float left;
		float right;
		EaseType ease;
	};

	static void addHoldNote(DrawData& drawData, const HoldNote& holdNote, Score const &score);

	const std::vector<HiSpeedChange>& DrawData::hiSpeedsFor(int layer) const
	{
		static const std::vector<HiSpeedChange> none;
		if (layer >= 0 && layer < static_cast<int>(layerHiSpeeds.size()))
			return layerHiSpeeds[layer];
		return layerHiSpeeds.empty() ? none : layerHiSpeeds[0];
	}

	static bool isNoteLayerHidden(const Score& score, int layer)
	{
		return layer >= 0 && layer < static_cast<int>(score.layers.size()) && score.layers[layer].hidden;
	}

	void DrawData::calculateDrawData(Score const &score)
	{
		this->clear();
		try
		{
			this->noteSpeed = config.pvNoteSpeed;
			if (score.tempoChanges.empty())
				return;

			layerHiSpeeds = splitHiSpeedLayers(score);

			const std::vector<ComboEvent> comboEvents = calculateComboEvents(score);
			comboTimes.reserve(comboEvents.size());
			hudScores.reserve(comboEvents.size());
			hudRanks.reserve(comboEvents.size());
			hudScoreBars.reserve(comboEvents.size());
			hudJudgeTimes.reserve(comboEvents.size());

			float weightedCount = 0.0f;
			for (const ComboEvent& event : comboEvents)
				weightedCount += std::max(0.0f, event.weight);
			weightedCount = std::max(weightedCount, 1.0f);

			const float levelFactor = (SCORE_RATING - 5.0f) * 0.005f + 1.0f;
			int combo = 0;
			float comboFactor = 1.0f;
			float hudScore = 0.0f;
			for (const ComboEvent& event : comboEvents)
			{
				const float timeSec = accumulateDuration(event.tick, TICKS_PER_BEAT, score.tempoChanges);
				const float weight = std::max(0.0f, event.weight);
				combo++;
				if (combo % 100 == 1 && combo > 1)
					comboFactor = std::min(comboFactor + 0.01f, 1.1f);

				hudScore += (SCORE_TEAM_POWER / weightedCount) * 4.0f * weight * levelFactor * comboFactor;
				const auto [rank, scoreBar] = scoreRankAndBar(hudScore);

				comboTimes.push_back(timeSec);
				hudScores.push_back(std::max(0, static_cast<int>(std::lround(hudScore))));
				hudRanks.push_back(rank);
				hudScoreBars.push_back(scoreBar);
				if (event.showJudge)
					hudJudgeTimes.push_back(timeSec);
			}

			std::map<int, Range> simBuilder;
			std::map<int, float> simDurationBuilder;
			std::map<int, float> simSpeedRatioBuilder;
			std::map<int, int> simLayerBuilder;
			std::set<int> speedMismatchedSimTicks;
			for (auto rit = score.notes.rbegin(), rend = score.notes.rend(); rit != rend; rit++)
			{
				auto& [id, note] = *rit;
				maxTicks = std::max(note.tick, maxTicks);
				NoteType type = note.getType();
				if (type == NoteType::HoldMid
					|| (type == NoteType::Hold && score.holdNotes.at(id).startType != HoldNoteType::Normal)
					|| (type == NoteType::HoldEnd && score.holdNotes.at(note.parentID).endType != HoldNoteType::Normal))
					continue;
				if (type == NoteType::HoldMid)
					continue;

				const int noteLayer = type == NoteType::HoldEnd ? score.notes.at(note.parentID).layer : note.layer;
				if (isNoteLayerHidden(score, noteLayer))
					continue;

				auto visual_tm = getNoteVisualTime(note, score, noteSpeed, hiSpeedsFor(noteLayer));
				drawingNotes.push_back(DrawingNote{note.ID, visual_tm, noteLayer});

				// Find the max and min lane within the same height (visual_tm.max)
				float center = getNoteCenter(note);
				float speedRatio = getEffectiveSpeedRatio(note, score);
				float visibleDuration = getNoteVisibleDuration(note, score, noteSpeed);
				auto&& [it, has_emplaced] = simBuilder.try_emplace(note.tick, Range{center, center});
				auto&& [dit, has_dur] = simDurationBuilder.try_emplace(note.tick, visibleDuration);
				if (!has_dur)
					dit->second = std::max(dit->second, visibleDuration);
				auto&& [sit, has_speed] = simSpeedRatioBuilder.try_emplace(note.tick, speedRatio);
				if (!has_speed && std::abs(sit->second - speedRatio) > 0.0001f)
					speedMismatchedSimTicks.insert(note.tick);
				// notes on different layers scroll differently, so they cannot share a line
				auto&& [lit, has_layer] = simLayerBuilder.try_emplace(note.tick, noteLayer);
				if (!has_layer && lit->second != noteLayer)
					speedMismatchedSimTicks.insert(note.tick);
				auto& x_range = it->second;
				if (has_emplaced)
					continue;
				if (center < x_range.min)
					x_range.min = center;
				if (center > x_range.max)
					x_range.max = center;
				continue;
			}

			for (const auto& [line_tick, x_range] : simBuilder)
			{
				if (x_range.min != x_range.max && speedMismatchedSimTicks.find(line_tick) == speedMismatchedSimTicks.end())
				{
					const int lineLayer = simLayerBuilder.at(line_tick);
					double targetTime = accumulateScaledDuration(line_tick, TICKS_PER_BEAT, score.tempoChanges, hiSpeedsFor(lineLayer));
					drawingLines.push_back(DrawingLine{ x_range, Range{ targetTime - simDurationBuilder.at(line_tick), targetTime }, simDurationBuilder.at(line_tick), lineLayer, line_tick });
				}
			}

			for (auto rit = score.holdNotes.rbegin(), rend = score.holdNotes.rend(); rit != rend; rit++)
			{
				addHoldNote(*this, rit->second, score);
			}
		}
		catch(const std::out_of_range& ex)
		{
			this->clear();
		}
	}

	void DrawData::clear()
	{
		drawingLines.clear();
		drawingNotes.clear();
		drawingHoldTicks.clear();
		drawingHoldSegments.clear();
		comboTimes.clear();
		hudScores.clear();
		hudRanks.clear();
		hudScoreBars.clear();
		hudJudgeTimes.clear();
		layerHiSpeeds.clear();
		effectView.reset();

		maxTicks = 1;
	}

	void addHoldNote(DrawData &drawData, const HoldNote &holdNote, Score const &score)
	{
		const float noteDuration = getNoteVisibleDuration(score.notes.at(holdNote.start.ID), score, drawData.noteSpeed);
		const Note& startNote = score.notes.at(holdNote.start.ID), endNote = score.notes.at(holdNote.end);
		const int layer = startNote.layer;
		if (isNoteLayerHidden(score, layer))
			return;
		const std::vector<HiSpeedChange>& hiSpeeds = drawData.hiSpeedsFor(layer);
		float activeTime = accumulateDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges);
		float startTime = activeTime;
		DrawingHoldStep head = {
			startNote.tick,
			accumulateScaledDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges, hiSpeeds),
			Engine::laneToLeft(startNote.lane),
			Engine::laneToLeft(startNote.lane) + startNote.width,
			holdNote.start.ease
		};
		for (ptrdiff_t headIdx = -1, tailIdx = 0, stepSz = holdNote.steps.size(); headIdx < stepSz; ++tailIdx)
		{
			if (tailIdx < stepSz && holdNote.steps[tailIdx].type == HoldStepType::Skip)
				continue;
			HoldStep tailStep = tailIdx == stepSz ? HoldStep{ holdNote.end, HoldStepType::Hidden } : holdNote.steps[tailIdx];
			const Note& tailNote = score.notes.at(tailStep.ID);
			auto easeFunction = getEaseFunction(head.ease);
			DrawingHoldStep tail = {
				tailNote.tick,
				accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, hiSpeeds),
				Engine::laneToLeft(tailNote.lane),
				Engine::laneToLeft(tailNote.lane) + tailNote.width,
				tailStep.ease
			};
			float endTime = accumulateDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges);
			drawData.drawingHoldSegments.push_back(DrawingHoldSegment {
				holdNote.end, 
				head.ease,
			holdNote.isGuide(),
			layer,
			holdNote.guideColor,
			holdNote.fadeType,
			tailIdx,
			head.time, tail.time,
			head.left, head.right,
			tail.left, tail.right,
			startTime, endTime,
			activeTime,
			noteDuration,
		});
			startTime = endTime;
			while ((headIdx + 1) < tailIdx)
			{
				const HoldStep& skipStep = holdNote.steps[headIdx + 1];
				assert(skipStep.type == HoldStepType::Skip);
				const Note& skipNote = score.notes.at(skipStep.ID);
				if (skipNote.tick > tail.tick)
					break;
				double tickTime = accumulateScaledDuration(skipNote.tick, TICKS_PER_BEAT, score.tempoChanges, hiSpeeds);
				double tick_t = unlerpD(head.time, tail.time, tickTime);
				float skipLeft = easeFunction(head.left, tail.left, tick_t);
				float skipRight = easeFunction(head.right, tail.right, tick_t);
				drawData.drawingHoldTicks.push_back(DrawingHoldTick{
					skipStep.ID,
					skipLeft + (skipRight - skipLeft) / 2,
					Range{tickTime - noteDuration, tickTime},
					noteDuration,
					layer
				});
				++headIdx;
			}
			if (tailStep.type != HoldStepType::Hidden)
			{
				double tickTime = accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, hiSpeeds);
				drawData.drawingHoldTicks.push_back(DrawingHoldTick{
					tailNote.ID,
					getNoteCenter(tailNote),
					{tickTime - noteDuration, tickTime},
					noteDuration,
					layer
				});
			}
			head = tail;
			++headIdx;
		}
	}
}
