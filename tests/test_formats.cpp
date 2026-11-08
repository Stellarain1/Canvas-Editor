#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/ChartIO.h"
#include "Canvas/Core/ChartTransforms.h"
#include "Canvas/Core/ChartIssues.h"
#include "Canvas/Core/SkillFever.h"
#include "IO.h"
#include "ScoreStats.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

using namespace MikuMikuWorld;
using namespace CanvasTest;

namespace
{
	std::string tempPath(const std::string& name)
	{
		const auto dir = std::filesystem::temp_directory_path() / "canvas_tests";
		std::filesystem::create_directories(dir);
		return (dir / name).string();
	}

	int combo(const Score& score) { return static_cast<int>(calculateComboEvents(score).size()); }

	int countGuides(const Score& score)
	{
		return static_cast<int>(std::count_if(score.holdNotes.begin(), score.holdNotes.end(),
			[](const auto& entry) { return entry.second.isGuide(); }));
	}

	std::multiset<GuideColor> guideColours(const Score& score)
	{
		std::multiset<GuideColor> colours;
		for (const auto& [id, hold] : score.holdNotes)
			if (hold.isGuide())
				colours.insert(hold.guideColor);
		return colours;
	}

	std::multiset<FadeType> guideFades(const Score& score)
	{
		std::multiset<FadeType> fades;
		for (const auto& [id, hold] : score.holdNotes)
			if (hold.isGuide())
				fades.insert(hold.fadeType);
		return fades;
	}

	std::multiset<EaseType> startEases(const Score& score)
	{
		std::multiset<EaseType> eases;
		for (const auto& [id, hold] : score.holdNotes)
			eases.insert(hold.start.ease);
		return eases;
	}

	std::multiset<int> layerOfTaps(const Score& score)
	{
		std::multiset<int> layers;
		for (const auto& [id, note] : score.notes)
			if (note.getType() == NoteType::Tap)
				layers.insert(note.layer);
		return layers;
	}

	Score withMarkers(Score score)
	{
		Canvas::SkillFever::applyMarkers(score, Canvas::SkillFever::generateOfficialStyle(score));
		return score;
	}

	Canvas::LoadResult roundTrip(const Score& score, Canvas::ExportFormat format, std::vector<std::string>* saveWarnings = nullptr)
	{
		const std::string path = tempPath(std::string("roundtrip.") + Canvas::exportFormatExtension(format));
		auto warnings = Canvas::saveChart(score, path, format);
		if (saveWarnings)
			*saveWarnings = warnings;
		return Canvas::loadChart(path);
	}
}

TEST(detects_every_export_format)
{
	const Score score = withMarkers(richChart());
	const std::pair<Canvas::ExportFormat, Canvas::ChartFormat> expected[] = {
		{ Canvas::ExportFormat::Mmws, Canvas::ChartFormat::Mmws },
		{ Canvas::ExportFormat::CcMmws, Canvas::ChartFormat::CcMmws },
		{ Canvas::ExportFormat::Sus, Canvas::ChartFormat::Sus },
		{ Canvas::ExportFormat::SusNoNoteSpeed, Canvas::ChartFormat::Sus },
		{ Canvas::ExportFormat::MusicScoreJson, Canvas::ChartFormat::MusicScoreJson },
		{ Canvas::ExportFormat::MusicScoreJsonBase64, Canvas::ChartFormat::MusicScoreJson },
		{ Canvas::ExportFormat::Usc, Canvas::ChartFormat::Usc },
		{ Canvas::ExportFormat::LevelDataNextSekai, Canvas::ChartFormat::LevelDataNextSekai },
	};

	for (const auto& [exportFormat, chartFormat] : expected)
	{
		const auto bytes = Canvas::saveChartToBytes(score, exportFormat);
		CHECK(!bytes.empty());
		CHECK_EQ(static_cast<int>(Canvas::detectFormat(bytes)), static_cast<int>(chartFormat));
	}
}

TEST(export_format_names_resolve)
{
	for (int i = 0; i < static_cast<int>(Canvas::ExportFormat::FormatCount); ++i)
	{
		const auto format = static_cast<Canvas::ExportFormat>(i);
		const auto parsed = Canvas::exportFormatFromName(Canvas::exportFormatExtension(format));
		CHECK(parsed.has_value());
	}
	CHECK(!Canvas::exportFormatFromName("docx").has_value());
}

TEST(every_format_keeps_combo_and_markers)
{
	const Score score = withMarkers(richChart());
	const int expectedCombo = combo(score);
	const auto expectedMarkers = Canvas::SkillFever::readMarkers(score);
	CHECK(expectedCombo > 0);
	CHECK_EQ(expectedMarkers.skills.size(), size_t(6));

	for (int i = 0; i < static_cast<int>(Canvas::ExportFormat::FormatCount); ++i)
	{
		const auto format = static_cast<Canvas::ExportFormat>(i);
		const auto loaded = roundTrip(score, format);
		if (combo(loaded.score) != expectedCombo || !(Canvas::SkillFever::readMarkers(loaded.score) == expectedMarkers))
			std::fprintf(stderr, "  in format %s\n", Canvas::exportFormatName(format));
		CHECK_EQ(combo(loaded.score), expectedCombo);
		CHECK_EQ(Canvas::SkillFever::readMarkers(loaded.score).skills, expectedMarkers.skills);
		CHECK_EQ(Canvas::SkillFever::readMarkers(loaded.score).feverChance, expectedMarkers.feverChance);
		CHECK_EQ(Canvas::SkillFever::readMarkers(loaded.score).feverStart, expectedMarkers.feverStart);
		CHECK_EQ(loaded.score.tempoChanges.size(), score.tempoChanges.size());
		CHECK_EQ(countGuides(loaded.score), countGuides(score));
	}
}

TEST(editor_formats_keep_extended_features)
{
	// Formats that can store everything the editor can: layers, guide colours and fades,
	// in-out / out-in eases.
	const Score score = withMarkers(richChart());
	for (auto format : { Canvas::ExportFormat::Mmws, Canvas::ExportFormat::CcMmws, Canvas::ExportFormat::Usc, Canvas::ExportFormat::LevelDataNextSekai })
	{
		const auto loaded = roundTrip(score, format);
		std::fprintf(stderr, "  checking %s\n", Canvas::exportFormatName(format));
		CHECK_EQ(loaded.score.layers.size(), score.layers.size());
		CHECK(layerOfTaps(loaded.score) == layerOfTaps(score));
		CHECK(guideColours(loaded.score) == guideColours(score));
		CHECK(guideFades(loaded.score) == guideFades(score));
		CHECK_EQ(loaded.score.hiSpeedChanges.size(), score.hiSpeedChanges.size());
		if (format != Canvas::ExportFormat::LevelDataNextSekai)
			CHECK(startEases(loaded.score) == startEases(score));
	}
}

TEST(mmws_round_trip_is_exact)
{
	const Score score = withMarkers(richChart());
	const auto loaded = roundTrip(score, Canvas::ExportFormat::Mmws);
	CHECK_EQ(loaded.score.metadata.title, score.metadata.title);
	CHECK_EQ(loaded.score.metadata.artist, score.metadata.artist);
	CHECK_EQ(loaded.score.metadata.author, score.metadata.author);
	CHECK(std::abs(loaded.score.metadata.musicOffset - score.metadata.musicOffset) < 1e-4f);
	CHECK_EQ(loaded.score.notes.size(), score.notes.size());
	CHECK_EQ(loaded.score.holdNotes.size(), score.holdNotes.size());
	CHECK_EQ(loaded.score.layers[1].name, std::string("fast"));
	CHECK(loaded.warnings.empty());
}

TEST(game_formats_fold_extras_with_warnings)
{
	const Score score = withMarkers(richChart());
	for (auto format : { Canvas::ExportFormat::Sus, Canvas::ExportFormat::MusicScoreJson })
	{
		std::vector<std::string> warnings;
		const auto loaded = roundTrip(score, format, &warnings);
		// the game reads one timeline, green/yellow guides that fade out and in/out eases
		CHECK(!warnings.empty());
		CHECK_EQ(loaded.score.layers.size(), size_t(1));
		CHECK_EQ(Canvas::countExtendedEases(loaded.score), 0);
		for (const auto& [id, hold] : loaded.score.holdNotes)
			if (hold.isGuide())
				CHECK(hold.guideColor == GuideColor::Green || hold.guideColor == GuideColor::Yellow);
	}
}

TEST(sus_keeps_layers_when_asked)
{
	const Score score = withMarkers(richChart());
	const std::string path = tempPath("layers.sus");
	Canvas::SaveOptions options;
	options.susMergeLayers = false;
	Canvas::saveChart(score, path, Canvas::ExportFormat::Sus, options);
	const auto loaded = Canvas::loadChart(path);
	CHECK_EQ(loaded.score.layers.size(), size_t(2));
	CHECK(layerOfTaps(loaded.score) == layerOfTaps(score));
	CHECK_EQ(combo(loaded.score), combo(score));
}

TEST(sus_uses_the_skill_fever_spec_encoding)
{
	// skill-fever-format/README.txt: #mmm10:41 skill, #mmm1f:11 fever chance, #mmm1f:21 fever start.
	// This mirrors skill_fever_reference.read_sus_skill_fever().
	const Score score = withMarkers(richChart());
	const auto markers = Canvas::SkillFever::readMarkers(score);
	const std::string path = tempPath("markers.sus");
	Canvas::saveChart(score, path, Canvas::ExportFormat::Sus);

	std::ifstream in(path);
	const std::regex data(R"(^#(\d{3})([0-9a-zA-Z])([0-9a-zA-Z]{1,2})\s*:\s*(\S*)\s*$)");
	const std::regex base(R"(^#MEASUREBS\s+(\d+)\s*$)", std::regex::icase);
	std::vector<int> skills, chance, start;
	int measureBase = 0;
	std::string line;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		std::smatch m;
		if (std::regex_match(line, m, base))
		{
			measureBase = std::stoi(m[1]);
			continue;
		}
		if (!std::regex_match(line, m, data) || m[2] != "1")
			continue;
		const int measure = measureBase + std::stoi(m[1]);
		std::string lane = m[3];
		std::transform(lane.begin(), lane.end(), lane.begin(), ::tolower);
		const std::string cells = m[4];
		const size_t n = std::max<size_t>(1, cells.size() / 2);
		for (size_t i = 0; i * 2 + 1 < cells.size(); ++i)
		{
			const std::string cell = cells.substr(i * 2, 2);
			const int tick = measure * 1920 + static_cast<int>(std::lround(static_cast<double>(i) / n * 1920));
			if (lane == "0" && cell == "41")
				skills.push_back(tick);
			else if (lane == "f" && cell == "11")
				chance.push_back(tick);
			else if (lane == "f" && cell == "21")
				start.push_back(tick);
		}
	}

	std::sort(skills.begin(), skills.end());
	CHECK_EQ(skills, markers.skills);
	CHECK_EQ(chance, std::vector<int>{ markers.feverChance });
	CHECK_EQ(start, std::vector<int>{ markers.feverStart });
}

TEST(music_score_json_has_extension_key)
{
	const Score score = withMarkers(richChart());
	const auto markers = Canvas::SkillFever::readMarkers(score);
	const auto bytes = Canvas::saveChartToBytes(score, Canvas::ExportFormat::MusicScoreJson);
	const auto json = nlohmann::json::parse(bytes.begin(), bytes.end());
	CHECK(json.contains("CanvasSkillFever"));
	CHECK_EQ(json["CanvasSkillFever"]["skills"].get<std::vector<int>>(), markers.skills);
	CHECK_EQ(json["CanvasSkillFever"]["feverChance"].get<int>(), markers.feverChance);
	CHECK_EQ(json["CanvasSkillFever"]["feverStart"].get<int>(), markers.feverStart);
}

TEST(lowering_in_out_eases_adds_a_hidden_midpoint)
{
	resetNextID();
	Score score;
	addHold(score, { 0, 0, 2, HoldStepType::Normal, EaseType::EaseInOut }, {}, { 960, 8, 2 });
	CHECK_EQ(Canvas::lowerExtendedEases(score), 1);

	const HoldNote& hold = score.holdNotes.begin()->second;
	CHECK_EQ(hold.start.ease, EaseType::EaseIn);
	CHECK_EQ(hold.steps.size(), size_t(1));
	const Note& mid = score.notes.at(hold.steps[0].ID);
	CHECK_EQ(mid.tick, 480);
	CHECK_EQ(mid.lane, 4);
	CHECK_EQ(hold.steps[0].type, HoldStepType::Hidden);
	CHECK_EQ(hold.steps[0].ease, EaseType::EaseOut);
	CHECK_EQ(Canvas::countExtendedEases(score), 0);
}

TEST(merge_layers_moves_notes_to_default)
{
	Score score = richChart();
	const auto report = Canvas::mergeLayersToDefault(score);
	CHECK_EQ(report.notesMoved, 1);
	CHECK_EQ(report.hiSpeedsDropped, 1);
	CHECK_EQ(score.layers.size(), size_t(1));
	for (const auto& [id, note] : score.notes)
		CHECK_EQ(note.layer, 0);
}

TEST(normalize_events_sorts_and_dedupes)
{
	Score score;
	score.tempoChanges = { Tempo(960, 140.0f), Tempo(0, 120.0f), Tempo(960, 140.0f) };
	score.hiSpeedChanges = { { 480, 2.0f, 0 }, { 0, 1.0f, 0 }, { 480, 2.0f, 0 }, { 480, 3.0f, 1 } };
	Canvas::normalizeEvents(score);
	CHECK_EQ(score.tempoChanges.size(), size_t(2));
	CHECK_EQ(score.tempoChanges[0].tick, 0);
	CHECK_EQ(score.hiSpeedChanges.size(), size_t(3));
	CHECK(score.layers.size() >= 2);
}

TEST(detect_rejects_garbage)
{
	const std::string text = "hello, world";
	CHECK_EQ(static_cast<int>(Canvas::detectFormat(std::vector<uint8_t>(text.begin(), text.end()))), static_cast<int>(Canvas::ChartFormat::Unknown));
	CHECK_EQ(static_cast<int>(Canvas::detectFormat({})), static_cast<int>(Canvas::ChartFormat::Unknown));
}

// Optional: run every chart in $CANVAS_EXTERNAL_CHARTS through every format. Official charts
// are not redistributable, so they are not in the repository.
TEST(external_charts_round_trip)
{
	const char* dir = std::getenv("CANVAS_EXTERNAL_CHARTS");
	if (!dir || !std::filesystem::is_directory(dir))
		return;

	for (const auto& entry : std::filesystem::directory_iterator(dir))
	{
		if (!entry.is_regular_file())
			continue;
		Canvas::LoadResult original;
		try
		{
			original = Canvas::loadChart(entry.path().string());
		}
		catch (const std::exception&)
		{
			continue;
		}

		// official charts are the reference: the issue detector must not call anything in them an error
		for (const auto& issue : Canvas::Issues::scanChart(original.score))
		{
			if (issue.severity == Canvas::Issues::Severity::Error)
				std::fprintf(stderr, "  %s: %s\n", entry.path().filename().string().c_str(), issue.code.c_str());
			CHECK(issue.severity != Canvas::Issues::Severity::Error);
		}

		const int expectedCombo = combo(original.score);
		for (int i = 0; i < static_cast<int>(Canvas::ExportFormat::FormatCount); ++i)
		{
			const auto format = static_cast<Canvas::ExportFormat>(i);
			const auto loaded = roundTrip(original.score, format);
			if (combo(loaded.score) != expectedCombo)
				std::fprintf(stderr, "  %s via %s\n", entry.path().filename().string().c_str(), Canvas::exportFormatName(format));
			CHECK_EQ(combo(loaded.score), expectedCombo);
		}
	}
}

#include "Canvas/Core/SongProject.h"

TEST(song_project_round_trip)
{
	const std::string path = tempPath("song/test.canvassong");
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());

	Canvas::SongProject song;
	song.title = "Test Song";
	song.artist = "Someone";
	song.musicFile = tempPath("song/audio/music.mp3");
	song.musicOffset = 12.5f;
	song.difficulties.push_back({ "master", Canvas::defaultDifficultyFile(path, "master"), 30 });
	song.difficulties.push_back({ "easy", Canvas::defaultDifficultyFile(path, "easy"), 5 });
	song.difficulties.push_back({ "my remix", Canvas::defaultDifficultyFile(path, "my remix"), 0 });
	song.save(path);

	// paths are stored relative to the song file
	std::ifstream in(path);
	const auto json = nlohmann::json::parse(in);
	CHECK_EQ(json["music"].get<std::string>(), std::string("audio/music.mp3"));
	CHECK_EQ(json["difficulties"][0]["file"].get<std::string>(), std::string("master.ce"));
	CHECK_EQ(json["difficulties"][2]["file"].get<std::string>(), std::string("my_remix.ce"));

	const auto loaded = Canvas::SongProject::load(path);
	CHECK_EQ(loaded.title, song.title);
	CHECK_EQ(loaded.artist, song.artist);
	CHECK(std::abs(loaded.musicOffset - 12.5f) < 1e-4f);
	CHECK_EQ(std::filesystem::path(loaded.musicFile).lexically_normal(), std::filesystem::path(song.musicFile).lexically_normal());
	// sorted in game order, custom names last
	CHECK_EQ(loaded.difficulties.size(), size_t(3));
	CHECK_EQ(loaded.difficulties[0].name, std::string("easy"));
	CHECK_EQ(loaded.difficulties[1].name, std::string("master"));
	CHECK_EQ(loaded.difficulties[1].level, 30);
	CHECK_EQ(loaded.difficulties[2].name, std::string("my remix"));
	CHECK(loaded.find("master") != nullptr);
	CHECK(loaded.find("expert") == nullptr);
}

TEST(song_project_rejects_other_json)
{
	const std::string path = tempPath("not_a_song.canvassong");
	std::ofstream(path) << R"({"hello": "world"})";
	bool threw = false;
	try
	{
		Canvas::SongProject::load(path);
	}
	catch (const std::exception&)
	{
		threw = true;
	}
	CHECK(threw);
}
