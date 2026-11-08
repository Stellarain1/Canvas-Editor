#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/SkillFever.h"
#include <algorithm>
#include <cmath>
#include <set>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace SF = Canvas::SkillFever;

namespace
{
	Score chartSpanning(int firstTick, int lastTick)
	{
		resetNextID();
		Score score;
		addTap(score, firstTick, 0, 3);
		addTap(score, lastTick, 6, 3);
		return score;
	}
}

TEST(generate_matches_reference_for_100_measures)
{
	// python3 skill-fever-format/skill_fever_reference.py
	//   skills: [21120, 46080, 71040, 101760, 134400, 165120] feverChance: 107520 feverStart: 130560
	const auto markers = SF::generateOfficialStyle(chartSpanning(0, 100 * 1920));
	CHECK_EQ(markers.skills, (std::vector<int>{ 21120, 46080, 71040, 101760, 134400, 165120 }));
	CHECK_EQ(markers.feverChance, 107520);
	CHECK_EQ(markers.feverStart, 130560);
}

TEST(generate_matches_reference_with_offset_span)
{
	// reference generate(3840, 3840 + 73 * 1920 + 480):
	// each t = first + round(span * f), snapped with round(t / 1920) * 1920 (ties to even)
	const int first = 3840, last = 3840 + 73 * 1920 + 480;
	const auto markers = SF::generateOfficialStyle(chartSpanning(first, last));
	auto at = [&](double fraction)
	{
		const double span = last - first;
		const double t = first + std::nearbyint(span * fraction);
		return static_cast<int>(std::nearbyint(t / 1920.0)) * 1920;
	};
	std::vector<int> expected;
	for (double f : SF::DEFAULT_SKILLS)
		expected.push_back(at(f));
	CHECK_EQ(markers.skills, expected);
	CHECK_EQ(markers.feverChance, at(SF::DEFAULT_FEVER_CHANCE));
	CHECK_EQ(markers.feverStart, at(SF::DEFAULT_FEVER_START));
}

TEST(generate_snaps_to_measures_in_other_time_signatures)
{
	Score score = chartSpanning(0, 90 * 1440);
	score.timeSignatures[0] = { 0, 3, 4 };
	const auto markers = SF::generateOfficialStyle(score);
	CHECK_EQ(markers.skills.size(), size_t(6));
	for (int tick : markers.skills)
		CHECK_EQ(tick % 1440, 0);
	CHECK_EQ(markers.feverChance % 1440, 0);
	CHECK_EQ(markers.feverStart % 1440, 0);
}

TEST(generate_keeps_markers_apart_in_short_charts)
{
	const auto markers = SF::generateOfficialStyle(chartSpanning(0, 1920));
	CHECK_EQ(markers.skills.size(), size_t(6));
	CHECK_EQ(std::set<int>(markers.skills.begin(), markers.skills.end()).size(), size_t(6));
	CHECK(std::is_sorted(markers.skills.begin(), markers.skills.end()));
	CHECK(markers.feverStart > markers.feverChance);
}

TEST(generate_is_empty_without_notes)
{
	Score score;
	CHECK(SF::generateOfficialStyle(score).empty());
}

TEST(apply_and_read_markers)
{
	Score score = chartSpanning(0, 1920 * 40);
	SF::Markers markers;
	markers.skills = { 1920 * 10, 1920 * 2, 1920 * 5 };
	markers.feverChance = 1920 * 20;
	markers.feverStart = 1920 * 25;
	SF::applyMarkers(score, markers);
	CHECK(SF::hasMarkers(score));
	const auto read = SF::readMarkers(score);
	CHECK_EQ(read.skills, (std::vector<int>{ 1920 * 2, 1920 * 5, 1920 * 10 }));
	CHECK_EQ(read.feverChance, 1920 * 20);
	CHECK_EQ(read.feverStart, 1920 * 25);

	SF::applyMarkers(score, {});
	CHECK(!SF::hasMarkers(score));
}

TEST(json_extension_matches_reference_writer)
{
	// write_json_skill_fever(): sorted int skills, null for a missing fever tick, key omitted when empty
	SF::Markers markers;
	markers.skills = { 300, 100, 200 };
	markers.feverChance = 1000;
	const auto json = SF::toJson(markers);
	CHECK_EQ(json["skills"].get<std::vector<int>>(), (std::vector<int>{ 100, 200, 300 }));
	CHECK_EQ(json["feverChance"].get<int>(), 1000);
	CHECK(json["feverStart"].is_null());

	CHECK(SF::toJson({}).is_null());

	const auto back = SF::fromJson(json);
	CHECK_EQ(back.skills, (std::vector<int>{ 100, 200, 300 }));
	CHECK_EQ(back.feverChance, 1000);
	CHECK_EQ(back.feverStart, -1);
}

TEST(ensure_song_copies_from_hardest_difficulty)
{
	Score master = chartSpanning(0, 1920 * 80);
	Score expert = chartSpanning(0, 1920 * 80);
	Score easy = chartSpanning(0, 1920 * 60);
	SF::Markers donor;
	donor.skills = { 1920, 3840, 5760, 7680, 9600, 11520 };
	donor.feverChance = 1920 * 30;
	donor.feverStart = 1920 * 40;
	SF::applyMarkers(master, donor);

	SF::Markers own;
	own.skills = { 1920 * 7 };
	SF::applyMarkers(easy, own);

	std::map<std::string, Score*> song{ { "master", &master }, { "expert", &expert }, { "easy", &easy } };
	const auto filled = SF::ensureSong(song);
	CHECK_EQ(filled, std::vector<std::string>{ "expert" });
	CHECK(SF::readMarkers(expert) == donor);
	CHECK_EQ(SF::readMarkers(easy).skills, std::vector<int>{ 1920 * 7 });
}

TEST(ensure_song_generates_when_nobody_has_markers)
{
	Score master = chartSpanning(0, 1920 * 100);
	Score hard = chartSpanning(0, 1920 * 100);
	std::map<std::string, Score*> song{ { "master", &master }, { "hard", &hard } };
	const auto filled = SF::ensureSong(song);
	CHECK_EQ(filled.size(), size_t(2));
	CHECK(SF::readMarkers(master) == SF::readMarkers(hard));
	CHECK_EQ(SF::readMarkers(master).skills.size(), size_t(6));
}
