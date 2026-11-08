#pragma once
#include <random>
#include "Score.h"
#include "MathUtils.h"
#include "EffectView.h"

namespace MikuMikuWorld
{
	struct Score;
	struct Note;
	struct ScoreContext;
}

namespace MikuMikuWorld::Engine
{
	struct DrawingNote
	{
		int refID;
		Range visualTime;
		int layer{};
	};

	struct DrawingLine
	{
		Range xPos;
		Range visualTime;
		float visibleDuration;
		int layer{};
		int tick{};		// the simultaneous notes' tick (Dynamic Stages picks the line's stage from it)
	};

	struct DrawingHoldTick
	{
		int refID;
		float center;
		Range visualTime;
		float visibleDuration;
		int layer{};
	};

	struct DrawingHoldSegment
	{
		int endID;
		EaseType ease;
		bool isGuide;
		int layer;
		GuideColor guideColor;
		FadeType fadeType;
		ptrdiff_t tailStepIndex;
		double headTime, tailTime;
		float headLeft, headRight;
		float tailLeft, tailRight;
		float startTime, endTime;
		double activeTime;
		float visibleDuration;
	};

	struct DrawData
	{
		float noteSpeed;
		int maxTicks;
		std::vector<DrawingNote> drawingNotes;
		std::vector<DrawingLine> drawingLines;
		std::vector<DrawingHoldTick> drawingHoldTicks;
		std::vector<DrawingHoldSegment> drawingHoldSegments;
		std::vector<float> comboTimes;
		std::vector<int> hudScores;
		std::vector<char> hudRanks;
		std::vector<float> hudScoreBars;
		std::vector<float> hudJudgeTimes;
		// hi-speed changes split per layer (index = layer), captured with the draw data
		std::vector<std::vector<HiSpeedChange>> layerHiSpeeds;
		Effect::EffectView effectView;

		const std::vector<HiSpeedChange>& hiSpeedsFor(int layer) const;

		void clear();
		void calculateDrawData(Score const& score);
	};
}
