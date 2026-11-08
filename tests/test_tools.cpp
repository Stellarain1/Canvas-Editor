#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/ChartTools.h"
#include <algorithm>
#include <cmath>

using namespace MikuMikuWorld;
using namespace CanvasTest;
using namespace Canvas::Tools;

namespace
{
	Score fourFour()
	{
		Score score;
		score.tempoChanges = { Tempo(0, 120.0f) };
		score.timeSignatures[0] = TimeSignature{ 0, 4, 4 };
		return score;
	}

	std::vector<const Note*> midsOf(const Score& score, const HoldNote& hold)
	{
		std::vector<const Note*> mids;
		for (const HoldStep& step : hold.steps)
			mids.push_back(&score.notes.at(step.ID));
		std::sort(mids.begin(), mids.end(), [](const Note* a, const Note* b) { return a->tick < b->tick; });
		return mids;
	}
}

TEST(division_grid_up_to_1920)
{
	Score score = fourFour();
	CHECK(std::abs(divisionStep(1920) - 1.0) < 1e-9);
	CHECK(std::abs(divisionStep(768) - 2.5) < 1e-9);
	CHECK(std::abs(divisionStep(16) - 120.0) < 1e-9);
	// 768 div: lines at 0, 2.5 -> 3 (rounded), 5, 7.5 -> 8 ...
	CHECK_EQ(snapToDivision(4, 768, score), 5);
	CHECK_EQ(nextDivisionTick(0, 1920, score), 1);
	CHECK_EQ(nextDivisionTick(5, 768, score), 8);
	CHECK_EQ(previousDivisionTick(8, 768, score), 5);
	CHECK_EQ(nextDivisionTick(1919, 16, score), 1920);
	CHECK_EQ(previousDivisionTick(1920, 16, score), 1800);
	CHECK_EQ(snapToDivision(130, 16, score), 120);
	// a 3/4 measure restarts the grid at its own start
	score.timeSignatures[1] = TimeSignature{ 1, 3, 4 };
	CHECK_EQ(nextDivisionTick(1920, 5, score), 1920 + 384);
}

TEST(repeat_hold_mids_pattern)
{
	resetNextID();
	Score score = fourFour();
	// a zigzag: start lane 2, mid at lane 6, mid back at lane 2 - over one beat
	HoldNote& hold = addHold(score, { 0, 2, 3 }, { { 240, 6, 3, HoldStepType::Normal }, { 480, 2, 3, HoldStepType::Normal } }, { 2400, 2, 3 });
	const int holdId = hold.start.ID;
	std::vector<int> pattern;
	for (const HoldStep& s : hold.steps)
		pattern.push_back(s.ID);
	CHECK_EQ(holdOfSelection(score, pattern), holdId);

	RepeatMidsOptions options;
	const auto result = repeatHoldMids(score, holdId, pattern, options);
	CHECK(result.error.empty());
	// period 480: repeats at 720/960, 1200/1440, 1680/1920, 2160 (2400 is the end)
	CHECK_EQ(result.added, 7);
	const auto mids = midsOf(score, score.holdNotes.at(holdId));
	CHECK_EQ(mids.size(), size_t(9));
	CHECK_EQ(mids[2]->tick, 720);
	CHECK_EQ(mids[2]->lane, 6);
	CHECK_EQ(mids[3]->lane, 2);
	CHECK_EQ(mids.back()->tick, 2160);
}

TEST(repeat_hold_mids_count_and_mirror)
{
	resetNextID();
	Score score = fourFour();
	HoldNote& hold = addHold(score, { 0, 0, 2 }, { { 240, 4, 2, HoldStepType::Normal } }, { 4800, 0, 2 });
	const int holdId = hold.start.ID;
	RepeatMidsOptions options;
	options.untilEnd = false;
	options.count = 2;
	options.mirror = true;
	const auto result = repeatHoldMids(score, holdId, { hold.steps[0].ID }, options);
	CHECK_EQ(result.added, 2);
	const auto mids = midsOf(score, score.holdNotes.at(holdId));
	// the span is lanes 0..6: the mirrored repeat of lane 4 (width 2) is lane 0
	CHECK_EQ(mids[1]->tick, 480);
	CHECK_EQ(mids[1]->lane, 0);
	CHECK_EQ(mids[2]->lane, 4);
}

TEST(repeat_hold_mids_every_step_follows_path)
{
	resetNextID();
	Score score = fourFour();
	HoldNote& hold = addHold(score, { 0, 0, 2 }, {}, { 960, 8, 2 });
	const int holdId = hold.start.ID;
	RepeatMidsOptions options;
	options.mode = RepeatMidsOptions::Mode::EveryStep;
	options.stepTicks = 240;
	options.stepType = HoldStepType::Skip;
	const auto result = repeatHoldMids(score, holdId, {}, options);
	CHECK_EQ(result.added, 3);
	const auto mids = midsOf(score, score.holdNotes.at(holdId));
	CHECK_EQ(mids[1]->tick, 480);
	CHECK_EQ(mids[1]->lane, 4);	// halfway along a straight hold
}

TEST(hispeed_interpolation)
{
	Score score = fourFour();
	score.hiSpeedChanges.push_back({ 960, 3.0f, 0 });
	InterpolateOptions options;
	options.fromTick = 0;
	options.toTick = 960;
	options.fromSpeed = 1.0f;
	options.toSpeed = 2.0f;
	options.stepTicks = 240;
	const int added = interpolateHiSpeeds(score, options);
	CHECK_EQ(added, 5);
	CHECK_EQ(score.hiSpeedChanges.size(), size_t(5));	// the 3.0 at 960 was replaced
	CHECK(std::abs(hiSpeedAt(score, 480, 0) - 1.5f) < 1e-4f);
	CHECK(std::abs(hiSpeedAt(score, 960, 0) - 2.0f) < 1e-4f);
	options.ease = SpeedEase::In;
	interpolateHiSpeeds(score, options);
	CHECK(std::abs(hiSpeedAt(score, 480, 0) - 1.25f) < 1e-4f);
}
