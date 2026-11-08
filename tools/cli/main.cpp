// canvas-cli: the Canvas Editor chart engine without the window.
//
//   canvas-cli info <chart>
//   canvas-cli convert <input> <output> [--format <fmt>] [--keep-layers]
//   canvas-cli skill-fever <input> <output> [--format <fmt>] [--force]
//   canvas-cli check <chart>... [--json] [--strict]
//   canvas-cli dump <chart> [--from <measure>] [--to <measure>]
//   canvas-cli edit <chart> <operations.json> [--out <chart>] [--format <fmt>]
//                  [--title/--charter/--artist/--lyricist/--composer/--arranger/--vocals <text>] [--movie <f> [--original-mv]]
//                  [--preview-start <s>] [--preview-length <s>] [--id <identifier>] [--ffmpeg <exe>]
//
// Every input format is detected from the file contents. Output format comes from --format
// or from the output file's extension.
#include "Canvas/Core/Paths.h"
#include "Canvas/Core/ChartIO.h"
#include "Canvas/Core/ChartMetrics.h"
#include "Canvas/Core/ChartJson.h"
#include "Canvas/Core/ChartIssues.h"
#include "Canvas/Core/ChartTransforms.h"
#include "Canvas/Core/SkillFever.h"
#include "Canvas/Core/SongProject.h"
#include "Canvas/Core/Version.h"
#include "File.h"
#include "ScoreStats.h"
#include <json.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <cstring>
#include <string>
#include <vector>

using namespace MikuMikuWorld;

namespace
{
	int usage()
	{
		std::fprintf(stderr,
			"%s\n\n"
			"usage:\n"
			"  canvas-cli info <chart>\n"
			"  canvas-cli convert <input> <output> [--format <fmt>] [--keep-layers]\n"
			"  canvas-cli skill-fever <input> <output> [--format <fmt>] [--force]\n"
			"  canvas-cli check <chart or .canvassong>... [--json] [--strict]\n"
			"  canvas-cli dump <chart> [--from <measure>] [--to <measure>]\n"
			"  canvas-cli edit <chart> <operations.json> [--out <chart>] [--format <fmt>]\n\n"
			"formats: mmws, ccmmws, sus, sus-no-speed, json, json-base64, usc, leveldata\n"
			"(inputs are detected from their contents: mmws/ccmmws/ucmmws, sus, MusicScoreData json,\n"
			" usc, Sonolus LevelData from Next Sekai/UntitledCharts or Chart Cyanvas, gzip or not)\n",
			Canvas::generatorString().c_str());
		return 2;
	}

	bool hasFlag(const std::vector<std::string>& args, const char* flag)
	{
		for (const auto& arg : args)
			if (arg == flag)
				return true;
		return false;
	}

	std::string optionValue(const std::vector<std::string>& args, const char* option)
	{
		for (size_t i = 0; i + 1 < args.size(); ++i)
			if (args[i] == option)
				return args[i + 1];
		return {};
	}

	std::vector<std::string> positional(const std::vector<std::string>& args)
	{
		std::vector<std::string> out;
		for (size_t i = 0; i < args.size(); ++i)
		{
			// options that take a value
			static const char* const valued[] = { "--format", "--bpm", "--difficulty", "--difficulties", "--seed", "--model",
				"--out", "--level", "--offset", "--title", "--artist", "--density", "--from", "--to", "--reference", "--motion", "--cache", "--candidates", "--phrases", "--phrase-measures",
				"--music", "--jacket", "--charter", "--lyricist", "--composer", "--arranger", "--vocals", "--movie", "--preview-start", "--preview-length", "--id", "--ffmpeg" };
			bool takesValue = false;
			for (const char* option : valued)
				takesValue = takesValue || args[i] == option;
			if (takesValue)
			{
				++i;
				continue;
			}
			if (args[i].rfind("--", 0) == 0)
				continue;
			out.push_back(args[i]);
		}
		return out;
	}

	std::optional<Canvas::ExportFormat> outputFormat(const std::string& output, const std::string& explicitFormat)
	{
		if (!explicitFormat.empty())
			return Canvas::exportFormatFromName(explicitFormat);
		return Canvas::exportFormatForPath(output);
	}

	void printWarnings(const std::vector<std::string>& warnings)
	{
		for (const auto& warning : warnings)
			std::fprintf(stderr, "warning: %s\n", warning.c_str());
	}

	int info(const std::string& path)
	{
		const Canvas::LoadResult loaded = Canvas::loadChart(path);
		printWarnings(loaded.warnings);
		const Score& score = loaded.score;

		int taps = 0, holds = 0, guides = 0, steps = 0;
		for (const auto& [id, note] : score.notes)
			taps += note.getType() == NoteType::Tap;
		for (const auto& [id, hold] : score.holdNotes)
		{
			(hold.isGuide() ? guides : holds)++;
			steps += static_cast<int>(hold.steps.size());
		}

		const Canvas::SkillFever::Markers markers = Canvas::SkillFever::readMarkers(score);
		std::printf("format:       %s\n", Canvas::formatName(loaded.format));
		std::printf("title:        %s\n", score.metadata.title.c_str());
		std::printf("taps:         %d\n", taps);
		std::printf("holds:        %d (%d steps)\n", holds, steps);
		std::printf("guides:       %d\n", guides);
		std::printf("combo:        %d\n", static_cast<int>(calculateComboEvents(score).size()));
		std::printf("bpm changes:  %d\n", static_cast<int>(score.tempoChanges.size()));
		std::printf("layers:       %d\n", static_cast<int>(score.layers.size()));
		std::printf("hi-speeds:    %d\n", static_cast<int>(score.hiSpeedChanges.size()));
		std::printf("skills:       %d [", static_cast<int>(markers.skills.size()));
		for (size_t i = 0; i < markers.skills.size(); ++i)
			std::printf("%s%d", i ? ", " : "", markers.skills[i]);
		std::printf("]\n");
		std::printf("fever:        chance %d, start %d\n", markers.feverChance, markers.feverStart);
		return 0;
	}

	int convert(const std::vector<std::string>& args)
	{
		const auto files = positional(args);
		if (files.size() != 2)
			return usage();

		const auto format = outputFormat(files[1], optionValue(args, "--format"));
		if (!format)
		{
			std::fprintf(stderr, "error: cannot tell the output format; pass --format\n");
			return 2;
		}

		const Canvas::LoadResult loaded = Canvas::loadChart(files[0]);
		printWarnings(loaded.warnings);

		Canvas::SaveOptions options;
		options.susMergeLayers = !hasFlag(args, "--keep-layers");
		printWarnings(Canvas::saveChart(loaded.score, files[1], *format, options));
		std::fprintf(stderr, "%s -> %s\n", Canvas::formatName(loaded.format), Canvas::exportFormatName(*format));
		return 0;
	}

	int skillFever(const std::vector<std::string>& args)
	{
		const auto files = positional(args);
		if (files.size() != 2)
			return usage();

		const auto format = outputFormat(files[1], optionValue(args, "--format"));
		if (!format)
		{
			std::fprintf(stderr, "error: cannot tell the output format; pass --format\n");
			return 2;
		}

		Canvas::LoadResult loaded = Canvas::loadChart(files[0]);
		printWarnings(loaded.warnings);

		if (Canvas::SkillFever::hasMarkers(loaded.score) && !hasFlag(args, "--force"))
		{
			std::fprintf(stderr, "the chart already has skill/fever markers; kept as they are (use --force to replace)\n");
		}
		else
		{
			const auto markers = Canvas::SkillFever::generateOfficialStyle(loaded.score);
			if (markers.empty())
			{
				std::fprintf(stderr, "error: the chart has no notes to place markers against\n");
				return 1;
			}
			Canvas::SkillFever::applyMarkers(loaded.score, markers);
			std::fprintf(stderr, "placed %d skills, fever chance at %d, fever start at %d\n",
				static_cast<int>(markers.skills.size()), markers.feverChance, markers.feverStart);
		}

		printWarnings(Canvas::saveChart(loaded.score, files[1], *format));
		return 0;
	}

	int check(const std::vector<std::string>& args)
	{
		const auto files = positional(args);
		if (files.empty())
			return usage();

		const bool asJson = hasFlag(args, "--json");
		const bool strict = hasFlag(args, "--strict");
		nlohmann::json report = nlohmann::json::array();
		int errors = 0, warnings = 0;

		for (const auto& file : files)
		{
			Canvas::LoadResult loaded;
			try
			{
				if (!(file.size() > 11 && file.compare(file.size() - 11, 11, Canvas::SONG_EXTENSION) == 0))
					loaded = Canvas::loadChart(file);
			}
			catch (const std::exception& ex)
			{
				std::fprintf(stderr, "%s: cannot load: %s\n", file.c_str(), ex.what());
				++errors;
				continue;
			}

			std::vector<Canvas::Issues::Issue> issues;
			if (file.size() > 11 && file.compare(file.size() - 11, 11, Canvas::SONG_EXTENSION) == 0)
			{
				// a whole song: every difficulty plus the cross-difficulty checks
				const Canvas::SongProject song = Canvas::SongProject::load(file);
				std::vector<Score> scores;
				scores.reserve(song.difficulties.size());
				for (const auto& difficulty : song.difficulties)
					scores.push_back(Canvas::loadChart(difficulty.file).score);
				std::vector<Canvas::Issues::DifficultyChart> charts;
				for (size_t i = 0; i < scores.size(); ++i)
					charts.push_back({ song.difficulties[i].name, &scores[i] });
				issues = Canvas::Issues::scanSong(charts);
				loaded.score = scores.empty() ? Score{} : scores.front();
			}
			else
				issues = Canvas::Issues::scanChart(loaded.score);
			for (const auto& issue : issues)
			{
				errors += issue.severity == Canvas::Issues::Severity::Error;
				warnings += issue.severity == Canvas::Issues::Severity::Warning;
				if (asJson)
				{
					report.push_back({ { "file", file }, { "severity", Canvas::Issues::severityName(issue.severity) },
						{ "code", issue.code }, { "title", issue.title }, { "category", Canvas::Issues::categoryName(issue.category) },
							{ "tick", issue.tick }, { "measure", issue.tick >= 0 ? Canvas::Issues::measureFraction(loaded.score, issue.tick) : "" },
							{ "track", Canvas::Issues::trackName(issue) }, { "difficulty", issue.difficulty }, { "message", issue.message } });
				}
				else
				{
					std::string where = issue.tick >= 0 ? Canvas::Issues::measureFraction(loaded.score, issue.tick) : "";
					const std::string track = Canvas::Issues::trackName(issue);
					if (!track.empty())
						where += (where.empty() ? "" : " ") + track;
					std::printf("%s%s%s: %s [%s] %s%s%s. %s\n", file.c_str(), issue.difficulty.empty() ? "" : "/", issue.difficulty.c_str(),
						Canvas::Issues::severityName(issue.severity), issue.code.c_str(), where.c_str(), where.empty() ? "" : ": ",
						issue.title.c_str(), issue.message.c_str());
				}
			}
		}

		if (asJson)
			std::printf("%s\n", report.dump(2).c_str());
		else
			std::printf("%d error(s), %d warning(s)\n", errors, warnings);

		return errors > 0 || (strict && warnings > 0) ? 1 : 0;
	}
}

namespace
{
	int dump(const std::vector<std::string>& args)
	{
		const auto files = positional(args);
		if (files.size() != 1)
			return usage();
		const auto loaded = Canvas::loadChart(files[0]);
		int from = -1, to = -1;
		if (!optionValue(args, "--from").empty())
			from = Canvas::ChartJson::tickOf(loaded.score, { { "measure", std::stoi(optionValue(args, "--from")) } });
		if (!optionValue(args, "--to").empty())
			to = Canvas::ChartJson::tickOf(loaded.score, { { "measure", std::stoi(optionValue(args, "--to")) + 1 } }) - 1;
		std::printf("%s\n", Canvas::ChartJson::toJson(loaded.score, from, to).dump(1).c_str());
		return 0;
	}

	int edit(const std::vector<std::string>& args)
	{
		const auto files = positional(args);
		if (files.size() != 2)
			return usage();
		auto loaded = Canvas::loadChart(files[0]);
		IO::File file(files[1], IO::FileMode::Read);
		const auto operations = nlohmann::json::parse(file.readAllText());
		file.close();
		const auto result = Canvas::ChartJson::applyEdits(loaded.score, operations);
		for (const auto& error : result.errors)
			std::fprintf(stderr, "%s\n", error.c_str());
		const std::string out = optionValue(args, "--out").empty() ? files[0] : optionValue(args, "--out");
		const std::string formatName = optionValue(args, "--format");
		auto format = formatName.empty() ? Canvas::exportFormatForPath(out) : Canvas::exportFormatFromName(formatName);
		if (!format)
			throw std::runtime_error("Cannot tell the output format; use --format or a known extension.");
		Canvas::saveChart(loaded.score, out, *format);
		std::printf("applied %d of %zu operation(s)\n", result.applied, operations.size());
		return result.errors.empty() ? 0 : 1;
	}
}

namespace
{
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return usage();

	const std::string command = argv[1];
	std::vector<std::string> args(argv + 2, argv + argc);

	try
	{
		if (command == "info" && args.size() == 1)
			return info(args[0]);
		if (command == "convert")
			return convert(args);
		if (command == "skill-fever")
			return skillFever(args);
		if (command == "check")
			return check(args);
		if (command == "dump")
			return dump(args);
		if (command == "edit")
			return edit(args);
		if (command == "--version" || command == "version")
		{
			std::printf("%s\n", Canvas::generatorString().c_str());
			return 0;
		}
	}
	catch (const std::exception& ex)
	{
		std::fprintf(stderr, "error: %s\n", ex.what());
		return 1;
	}

	return usage();
}
