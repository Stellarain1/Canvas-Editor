#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/ChartAutoFix.h"
#include "Canvas/Core/ChartIssues.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace Issues = Canvas::Issues;
namespace Fixes = Canvas::Fixes;

namespace
{
	Score fourFour()
	{
		Score score;
		score.tempoChanges = { Tempo(0, 120.0f) };
		score.timeSignatures[0] = TimeSignature{ 0, 4, 4 };
		return score;
	}

	bool has(const Score& score, const std::string& code)
	{
		const auto issues = Issues::scanChart(score);
		return std::any_of(issues.begin(), issues.end(), [&](const Issues::Issue& i) { return i.code == code; });
	}

	// a spread of taps so the chart is "real"
	void fill(Score& score, int count = 40)
	{
		for (int i = 0; i < count; ++i)
			addTap(score, 1920 + i * 480, (i * 3) % 9, 2);
	}
}

TEST(autofix_timing)
{
	resetNextID();
	Score score = fourFour();
	fill(score);
	score.tempoChanges = { Tempo(240, 150.0f), Tempo(960, -5.0f), Tempo(1920, 160.0f), Tempo(1920, 170.0f) };
	score.timeSignatures[3] = TimeSignature{ 3, 0, 4 };
	CHECK(has(score, "no-bpm-at-start"));
	CHECK(has(score, "bad-bpm"));
	CHECK(has(score, "bad-time-signature"));

	Fixes::AutoFixOptions options;
	options.groups = Fixes::FixTiming;
	const auto report = Fixes::autoFix(score, options);
	CHECK(report.timing >= 4);
	CHECK(!has(score, "no-bpm-at-start"));
	CHECK(!has(score, "bad-bpm"));
	CHECK(!has(score, "bpm-duplicate"));
	CHECK(!has(score, "bad-time-signature"));
	CHECK_EQ(score.tempoChanges.size(), size_t(2));
	CHECK(std::abs(score.tempoChanges[1].bpm - 170.0f) < 1e-4f);	// the later of the two on one tick
}

TEST(autofix_holds)
{
	resetNextID();
	Score score = fourFour();
	fill(score);
	// ends before it starts
	addHold(score, { 12000, 1, 2 }, {}, { 10000, 5, 2 });
	// a step after the end, one on the start, one fine
	addHold(score, { 14000, 2, 3 }, { { 14480, 4, 3 }, { 14000, 3, 3 }, { 16000, 3, 3 } }, { 15000, 2, 3 });
	// no length at all
	addHold(score, { 17000, 6, 2 }, {}, { 17000, 6, 2 });
	// off the lanes
	score.notes.at(addTap(score, 18000, 10, 3).ID).lane = 10;
	// a point of a hold that is gone
	Note orphan(NoteType::HoldMid, 19000, 3, 2);
	orphan.ID = nextID++;
	orphan.parentID = 123456;
	score.notes[orphan.ID] = orphan;
	// a broken hold
	HoldNote& broken = addHold(score, { 20000, 0, 2 }, {}, { 20480, 0, 2 });
	score.notes.erase(broken.end);

	for (const char* code : { "hold-zero-length", "step-outside-hold", "step-on-endpoint", "lane-out-of-range", "orphan-hold-point", "broken-hold" })
		CHECK(has(score, code));

	Fixes::AutoFixOptions options;
	options.groups = Fixes::FixHolds;
	const auto report = Fixes::autoFix(score, options);
	CHECK(report.holds >= 6);
	for (const char* code : { "hold-zero-length", "step-outside-hold", "step-on-endpoint", "lane-out-of-range", "orphan-hold-point", "broken-hold", "hold-points-same-tick" })
		CHECK(!has(score, code));
	CHECK(report.after < report.before);
}

TEST(autofix_hispeed_basics)
{
	resetNextID();
	Score score = fourFour();
	fill(score);
	score.layers = { HiSpeedLayer{ "default" } };
	score.hiSpeedChanges = {
		{ 1920, 1.5f, 0 },
		{ 1920, 2.0f, 0 },								// two on one tick
		{ -480, 1.0f, 0 },								// before the start
		{ 4000, std::numeric_limits<float>::quiet_NaN(), 0 },
		{ 5000, 1.2f, 3 },								// a layer that does not exist
	};
	CHECK(has(score, "hispeed-duplicate"));
	CHECK(has(score, "hispeed-not-finite"));
	CHECK(has(score, "bad-layer"));
	CHECK(has(score, "hispeed-negative-tick"));

	Fixes::AutoFixOptions options;
	options.groups = Fixes::FixHiSpeed;
	Fixes::autoFix(score, options);
	CHECK(!has(score, "hispeed-duplicate"));
	CHECK(!has(score, "hispeed-not-finite"));
	CHECK(!has(score, "bad-layer"));
	CHECK(!has(score, "hispeed-negative-tick"));
	// the later of the two on a tick survived
	bool found = false;
	for (const auto& c : score.hiSpeedChanges)
		found |= c.tick == 1920 && std::abs(c.speed - 2.0f) < 1e-4f;
	CHECK(found);
}

TEST(autofix_hispeed_aggressive)
{
	resetNextID();
	Score score = fourFour();
	fill(score);
	// 100x for four measures (a typo for 1.0), a reverse scroll, and a stop between two notes on one lane
	score.hiSpeedChanges = { { 0, 1.0f, 0 }, { 3840, 100.0f, 0 }, { 11520, 1.0f, 0 }, { 13440, -1.5f, 0 }, { 14400, 1.0f, 0 }, { 24000, 0.0f, 0 }, { 24960, 1.0f, 0 } };
	addTap(score, 24120, 4, 2);
	addTap(score, 24600, 4, 2);

	Fixes::AutoFixOptions plain;
	plain.groups = Fixes::FixHiSpeed;
	Score untouched = score;
	Fixes::autoFix(untouched, plain);
	CHECK(has(untouched, "hispeed-stacked"));	// conservative mode leaves the gimmicks alone
	bool stillHuge = false;
	for (const auto& c : untouched.hiSpeedChanges)
		stillHuge |= c.speed > 50.0f;
	CHECK(stillHuge);

	Fixes::AutoFixOptions aggressive = plain;
	aggressive.aggressiveHiSpeed = true;
	const auto report = Fixes::autoFix(score, aggressive);
	CHECK(report.hiSpeed >= 3);
	for (const auto& c : score.hiSpeedChanges)
		CHECK(c.speed >= 0.0f && c.speed <= 20.0f);
	CHECK(!has(score, "hispeed-negative"));
	CHECK(!has(score, "hispeed-stacked"));
	CHECK(report.after <= report.before);
}

TEST(autofix_skills_and_fever)
{
	resetNextID();
	Score score = fourFour();
	fill(score, 120);
	CHECK(has(score, "skill-missing"));
	CHECK(has(score, "fever-chance-missing"));

	Fixes::AutoFixOptions options;
	options.groups = Fixes::FixSkillFever;
	const auto report = Fixes::autoFix(score, options);
	CHECK(report.skillFever >= 2);
	CHECK(!has(score, "skill-missing"));
	CHECK(!has(score, "fever-chance-missing"));
	CHECK(!has(score, "fever-order"));
	CHECK_EQ(score.skills.size(), size_t(6));

	// duplicates and a skill too close to the one before it
	Score messy = fourFour();
	fill(messy, 120);
	messy.skills = { { nextSkillID++, 4000 }, { nextSkillID++, 4000 }, { nextSkillID++, 4480 } };
	messy.fever.chanceTick = 20000;
	messy.fever.startTick = 15000;
	Fixes::autoFix(messy, options);
	CHECK(!has(messy, "skill-duplicate"));
	CHECK(!has(messy, "skill-too-close"));
	CHECK(!has(messy, "fever-order"));
}

TEST(autofix_preview_counts_groups)
{
	resetNextID();
	Score score = fourFour();
	fill(score);
	addTap(score, 4000, 2, 2);
	addTap(score, 4000, 2, 2);	// stacked duplicate
	score.tempoChanges.push_back(Tempo(0, 130.0f));
	const auto preview = Fixes::previewAutoFix(score);
	CHECK(preview.perGroup[0] >= 1);	// overlaps
	CHECK(preview.perGroup[4] >= 1);	// timing: two BPMs on tick 0
	CHECK(preview.fixable() >= 2);
	CHECK_EQ(Fixes::groupOfCode("hispeed-stacked"), int(Fixes::FixHiSpeed));
	CHECK_EQ(Fixes::groupOfCode("critical-end-only"), 0);
}

TEST(autofix_clean_chart_is_left_alone)
{
	resetNextID();
	Score score = fourFour();
	fill(score, 120);
	Fixes::AutoFixOptions options;
	options.groups = Fixes::FixAll & ~Fixes::FixSkillFever;
	const Score copy = score;
	const auto report = Fixes::autoFix(score, options);
	CHECK_EQ(report.total(), 0);
	CHECK_EQ(score.notes.size(), copy.notes.size());
	CHECK_EQ(score.tempoChanges.size(), copy.tempoChanges.size());
}
