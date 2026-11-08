#pragma once
#include "../../Score.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// One way in and one way out for every chart format Canvas Editor understands. The format of
// an input file is detected from its bytes, never from its extension alone.
namespace Canvas
{
	enum class ChartFormat
	{
		Unknown,
		Mmws,				// MikuMikuWorld / External-Unofficial-ChartMaker / Canvas (MMWS)
		CcMmws,				// MikuMikuWorld4CC (CCMMWS)
		UcMmws,				// UntitledCharts MMW (UCMMWS)
		Sus,				// Sliding Universal Score
		MusicScoreJson,		// Project SEKAI MusicScoreData (plain / gzip / base64-gzip)
		Usc,				// MMW4CC / Chart Cyanvas USC
		LevelDataNextSekai,	// Sonolus LevelData, Next Sekai / UntitledCharts engine
		LevelDataChartCyanvas	// Sonolus LevelData, Chart Cyanvas engine
	};

	enum class ExportFormat
	{
		Mmws,					// .mmws (MMWS v4, the original MikuMikuWorld's layout, + Canvas extension block)
		CcMmws,					// .ccmmws for MikuMikuWorld4CC
		Sus,					// .sus with per-note speed
		SusNoNoteSpeed,			// .sus without per-note speed cells
		MusicScoreJson,			// .json MusicScoreData (+ CanvasSkillFever)
		MusicScoreJsonBase64,	// same, base64(gzip(json)) like the game's server
		Usc,					// .usc
		LevelDataNextSekai,		// .json.gz Sonolus LevelData
		MmwsExtended,			// .mmws as MMWS v7 (note speed + SE volume in the file; the original MMW cannot open it)
		FormatCount
	};

	const char* formatName(ChartFormat format);
	const char* exportFormatName(ExportFormat format);
	const char* exportFormatExtension(ExportFormat format);	// without the dot, e.g. "sus", "json.gz"
	std::optional<ExportFormat> exportFormatFromName(const std::string& nameOrExtension);

	ChartFormat detectFormat(const std::vector<uint8_t>& bytes);

	// Export format implied by a file name (".json.gz" -> LevelData, ".sus" -> sus, ...).
	std::optional<ExportFormat> exportFormatForPath(const std::string& path);

	// Whether a dropped / opened file is worth trying as a chart (by extension; the actual format
	// is still detected from the contents).
	bool looksLikeChartFile(const std::string& path);

	// Extensions accepted by the open dialog, e.g. "*.mmws;*.sus;..."
	const char* chartFilePatterns();

	struct LoadResult
	{
		MikuMikuWorld::Score score;
		ChartFormat format{ ChartFormat::Unknown };
		std::vector<std::string> warnings;
	};

	// Throws std::runtime_error with a readable message when the file cannot be read.
	LoadResult loadChart(const std::string& path);
	LoadResult loadChartFromBytes(const std::vector<uint8_t>& bytes, const std::string& nameHint = "");

	struct SaveOptions
	{
		// .sus: fold hi-speed layers into #TIL00 (what the game reads). Off keeps #TILxx + #HISPEED.
		bool susMergeLayers{ true };
		// LevelData: gzip the output (the usual .json.gz)
		bool levelDataGzip{ true };
	};

	// Writes the chart. Returns warnings about anything the format could not hold.
	std::vector<std::string> saveChart(const MikuMikuWorld::Score& score, const std::string& path, ExportFormat format, const SaveOptions& options = {});
	std::vector<uint8_t> saveChartToBytes(const MikuMikuWorld::Score& score, ExportFormat format, std::vector<std::string>* warnings = nullptr, const SaveOptions& options = {});
}
