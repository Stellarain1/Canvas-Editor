#include <set>
#include "TestFramework.h"
#include "Canvas/Core/ChartFixes.h"
#include "ChartBuilder.h"
#include "Canvas/Core/ChartIssues.h"
#include "Canvas/Core/SkillFever.h"
#include <algorithm>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace Issues = Canvas::Issues;

namespace
{
	bool has(const std::vector<Issues::Issue>& issues, const std::string& code)
	{
		return std::any_of(issues.begin(), issues.end(), [&](const Issues::Issue& issue) { return issue.code == code; });
	}

	const Issues::Issue* find(const std::vector<Issues::Issue>& issues, const std::string& code)
	{
		for (const auto& issue : issues)
			if (issue.code == code)
				return &issue;
		return nullptr;
	}

	std::string codes(const std::vector<Issues::Issue>& issues)
	{
		std::string out;
		for (const auto& issue : issues)
			out += issue.code + " ";
		return out;
	}

	Issues::ScanOptions noSkills()
	{
		Issues::ScanOptions options;
		options.checkSkillFever = false;
		return options;
	}

	Score withMarkers(Score score)
	{
		Canvas::SkillFever::applyMarkers(score, Canvas::SkillFever::generateOfficialStyle(score));
		return score;
	}
}

TEST(clean_chart_has_no_errors)
{
	const auto issues = Issues::scanChart(withMarkers(richChart()));
	const auto summary = Issues::summarize(issues);
	if (summary.errors)
		std::fprintf(stderr, "  %s\n", codes(issues).c_str());
	CHECK_EQ(summary.errors, 0);
}

TEST(empty_chart)
{
	CHECK(has(Issues::scanChart(Score{}), "empty-chart"));
}

TEST(overlapping_taps)
{
	resetNextID();
	Score score;
	const int a = addTap(score, 1920, 2, 3).ID;
	const int b = addTap(score, 1920, 3, 3).ID;
	const auto issues = Issues::scanChart(score, noSkills());
	const auto* issue = find(issues, "visible-overlap");
	CHECK(issue != nullptr);
	if (issue)
	{
		CHECK_EQ(issue->tick, 1920);
		CHECK_EQ(issue->severity, Issues::Severity::Warning);
		CHECK(issue->category == Issues::Category::VisibleOverlap);
		CHECK_EQ(issue->title, std::string("Visible overlap hides a note"));
		CHECK_EQ(Issues::trackName(*issue), std::string("Lane 4-5"));
		CHECK(std::find(issue->noteIds.begin(), issue->noteIds.end(), a) != issue->noteIds.end());
		CHECK(std::find(issue->noteIds.begin(), issue->noteIds.end(), b) != issue->noteIds.end());
	}
}

TEST(stacked_taps)
{
	resetNextID();
	Score score;
	addTap(score, 960, 4, 2);
	addTap(score, 960, 4, 2, true);
	CHECK(has(Issues::scanChart(score, noSkills()), "duplicate-note"));
}

TEST(side_by_side_taps_are_fine)
{
	resetNextID();
	Score score;
	addTap(score, 960, 0, 3);
	addTap(score, 960, 3, 3);
	const auto issues = Issues::scanChart(score, noSkills());
	CHECK(!has(issues, "visible-overlap"));
	CHECK(!has(issues, "notes-same-key"));
}

TEST(note_off_the_lanes)
{
	resetNextID();
	Score score;
	Note& note = addTap(score, 960, 10, 4);
	(void)note;
	CHECK(has(Issues::scanChart(score, noSkills()), "lane-out-of-range"));
}

TEST(hidden_step_on_a_tap)
{
	resetNextID();
	Score score;
	addHold(score, { 0, 2, 3 }, { { 960, 4, 3, HoldStepType::Hidden } }, { 1920, 4, 3 });
	addTap(score, 960, 4, 2);
	CHECK(has(Issues::scanChart(score, noSkills()), "hidden-step-visible"));
}

TEST(overlapping_holds)
{
	resetNextID();
	Score score;
	addHold(score, { 0, 2, 4 }, {}, { 1920, 2, 4 });
	addHold(score, { 480, 3, 4 }, {}, { 2400, 3, 4 });
	CHECK(has(Issues::scanChart(score, noSkills()), "holds-overlap"));
}

TEST(crossing_holds)
{
	resetNextID();
	Score score;
	addHold(score, { 0, 0, 2 }, {}, { 1920, 10, 2 });
	addHold(score, { 0, 10, 2 }, {}, { 1920, 0, 2 });
	const auto issues = Issues::scanChart(score, noSkills());
	CHECK(has(issues, "holds-cross") || has(issues, "holds-overlap"));
}

TEST(guides_may_overlap_holds)
{
	resetNextID();
	Score score;
	addHold(score, { 0, 2, 4 }, {}, { 1920, 2, 4 });
	addGuide(score, { 240, 3, 4 }, { 1680, 3, 4 }, GuideColor::Green, FadeType::Out);
	CHECK(!has(Issues::scanChart(score, noSkills()), "holds-overlap"));
}

TEST(bad_hi_speeds)
{
	resetNextID();
	Score score;
	addTap(score, 0, 0, 3);
	score.hiSpeedChanges = { { 480, 0.0f, 0 }, { 960, -1.0f, 0 } };
	const auto issues = Issues::scanChart(score, noSkills());
	CHECK(has(issues, "hispeed-zero"));
	CHECK(has(issues, "hispeed-negative"));
}

TEST(hi_speed_readability)
{
	// plain chart with a stop: nothing to report
	resetNextID();
	Score calm;
	for (int i = 0; i < 16; ++i)
		addTap(calm, 1920 + i * 480, (i % 4) * 3, 3);
	calm.hiSpeedChanges = { { 0, 1.0f, 0 }, { 1920 * 3, 0.5f, 0 }, { 1920 * 4, 1.0f, 0 } };
	auto issues = Issues::scanChart(calm, noSkills());
	CHECK(!has(issues, "hispeed-flash"));
	CHECK(!has(issues, "hispeed-stacked"));
	CHECK(!has(issues, "hispeed-overtake"));

	// a 1000x burst right before notes: they are on screen for a split second
	resetNextID();
	Score flash = calm;
	flash.hiSpeedChanges = { { 0, 1.0f, 0 }, { 1920 * 2 + 240, 1000.0f, 0 }, { 1920 * 3, 1.0f, 0 } };
	issues = Issues::scanChart(flash, noSkills());
	CHECK(has(issues, "hispeed-flash"));

	// a stop between two notes on the same lanes: drawn on top of each other
	resetNextID();
	Score stop;
	addTap(stop, 1920, 3, 3);
	addTap(stop, 1920 + 480, 3, 3);
	addTap(stop, 1920 * 3, 6, 3);
	stop.hiSpeedChanges = { { 0, 1.0f, 0 }, { 1920 + 1, 0.0f, 0 }, { 1920 + 480, 1.0f, 0 } };
	issues = Issues::scanChart(stop, noSkills());
	CHECK(has(issues, "hispeed-stacked"));

	// scrolling backwards: a later note shows up below an earlier one
	resetNextID();
	Score reverse;
	for (int i = 0; i < 8; ++i)
		addTap(reverse, 1920 + i * 240, 3, 3);
	reverse.hiSpeedChanges = { { 0, 1.0f, 0 }, { 1920 + 600, -2.0f, 0 }, { 1920 + 1200, 1.0f, 0 } };
	issues = Issues::scanChart(reverse, noSkills());
	CHECK(has(issues, "hispeed-overtake"));
}

TEST(fix_overlaps)
{
	resetNextID();
	Score score;
	addTap(score, 1920, 2, 3);
	addTap(score, 1920, 2, 3, true);			// identical spot: merged, keeps critical
	addTap(score, 3840, 4, 3);
	addTap(score, 3840, 5, 3);					// overlaps: moved aside
	addHold(score, { 5760, 2, 3 }, {}, { 7680, 2, 3 });
	addTap(score, 5760, 3, 3);					// sits on the hold start: moved
	addHold(score, { 9600, 6, 3 }, {}, { 11520, 6, 3 });
	addHold(score, { 9600, 6, 3 }, {}, { 13440, 8, 3 });	// hold on hold: the later hold's start moves
	const size_t before = score.notes.size();
	const auto report = Canvas::Fixes::fixOverlaps(score);
	CHECK_EQ(report.merged, 1);
	CHECK_EQ(report.moved, 3);
	CHECK_EQ(score.notes.size(), before - 1);
	int critical = 0;
	for (const auto& [id, note] : score.notes)
		if (note.tick == 1920)
			critical += note.critical;
	CHECK_EQ(critical, 1);
	// nothing is left, and ticks never moved
	int leftOver = 0;
	for (const auto& issue : Issues::scanChart(score, noSkills()))
		leftOver += Canvas::Fixes::isOverlapCode(issue.code);
	CHECK_EQ(report.left, leftOver);
	CHECK_EQ(leftOver, 0);
	std::set<int> ticks;
	for (const auto& [id, note] : score.notes)
		ticks.insert(note.tick);
	CHECK(ticks.count(3840) && ticks.count(5760));
}

TEST(bad_bpm)
{
	resetNextID();
	Score score;
	addTap(score, 0, 0, 3);
	score.tempoChanges = { Tempo(0, 0.0f) };
	CHECK(has(Issues::scanChart(score, noSkills()), "bad-bpm"));
}

TEST(missing_and_misordered_skill_fever)
{
	Score score = richChart();
	CHECK(has(Issues::scanChart(score), "skill-missing"));

	Canvas::SkillFever::Markers markers = Canvas::SkillFever::generateOfficialStyle(score);
	std::swap(markers.feverChance, markers.feverStart);
	Canvas::SkillFever::applyMarkers(score, markers);
	CHECK(has(Issues::scanChart(score), "fever-order"));
}

TEST(song_level_checks)
{
	const Score master = withMarkers(richChart());
	Score expert = richChart();
	Canvas::SkillFever::Markers other = Canvas::SkillFever::readMarkers(master);
	other.feverChance += 1920;
	Canvas::SkillFever::applyMarkers(expert, other);

	const auto issues = Issues::scanSong({ { "master", &master }, { "expert", &expert }, { "master", &master } });
	CHECK(has(issues, "difficulty-missing"));
	CHECK(has(issues, "difficulty-duplicate"));
	CHECK(has(issues, "skill-fever-mismatch"));
}

TEST(positions_are_readable)
{
	Score score;
	CHECK(!Issues::describePosition(score, 0).empty());
	CHECK(Issues::describePosition(score, 1920 * 3 + 480) != Issues::describePosition(score, 1920 * 3));
}

TEST(exact_hold_node_overlap)
{
	// three holds ending on the same spot (lanes 5-7), like two charts merged by hand
	resetNextID();
	Score score;
	addHold(score, { 960, 0, 3 }, {}, { 2640, 4, 3 });
	addHold(score, { 1440, 8, 3 }, {}, { 2640, 4, 3 });
	addHold(score, { 1920, 4, 3 }, {}, { 2640, 4, 3 });
	const auto issues = Issues::scanChart(score, noSkills());
	int collisions = 0;
	for (const auto& issue : issues)
	{
		if (issue.code != "hold-node-collision")
			continue;
		++collisions;
		CHECK(issue.severity == Issues::Severity::Error);
		CHECK(issue.category == Issues::Category::HoldNodeCollision);
		CHECK_EQ(issue.tick, 2640);
		CHECK_EQ(Issues::trackName(issue), std::string("Lane 5-7"));
		CHECK_EQ(Issues::measureFraction(score, issue.tick), std::string("M001 3/8"));
		CHECK_EQ(Issues::measureOf(score, issue.tick), 1);
	}
	CHECK_EQ(collisions, 3);	// one per pair
	CHECK(!has(issues, "visible-overlap"));
}

TEST(visible_overlap_names_the_notes)
{
	resetNextID();
	Score score;
	addHold(score, { 0, 2, 4 }, {}, { 960, 4, 3 });		// ends on lanes 5-7
	addHold(score, { 480, 8, 3 }, {}, { 960, 5, 2 });	// ends on lanes 6-7: shares the right edge
	const auto issues = Issues::scanChart(score, noSkills());
	const auto* issue = find(issues, "visible-overlap");
	CHECK(issue != nullptr);
	if (issue)
		CHECK_EQ(issue->message, std::string("Hold End overlaps with earlier Hold End at the same tick and shared edge (lanes 6-7). The later note may be hidden."));
}

TEST(measure_fractions)
{
	Score score;
	CHECK_EQ(Issues::measureFraction(score, 0), std::string("M000"));
	CHECK_EQ(Issues::measureFraction(score, 1920 + 960), std::string("M001 1/2"));
	CHECK_EQ(Issues::measureFraction(score, 1920 * 12 + 240), std::string("M012 1/8"));
	score.timeSignatures[0] = { 0, 3, 4 };
	CHECK_EQ(Issues::measureFraction(score, 1440 + 480), std::string("M001 1/3"));
	CHECK_EQ(Issues::measureFraction(score, -1), std::string(""));
}

TEST(every_issue_has_a_title)
{
	for (const auto& issue : Issues::scanChart(richChart()))
		CHECK(!issue.title.empty() && issue.title != issue.code);
}
