#include "ChartIO.h"
#include "LevelDataSerializer.h"
#include "UscSerializer.h"
#include "../../CustomScoreJsonSerializer.h"
#include "../../NativeScoreSerializer.h"
#include "../../SusSerializer.h"
#include "../../File.h"
#include "../../IO.h"
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>

using namespace MikuMikuWorld;

namespace Canvas
{
	namespace
	{
		bool startsWithBytes(const std::vector<uint8_t>& bytes, const char* text, size_t length)
		{
			return bytes.size() >= length && std::memcmp(bytes.data(), text, length) == 0;
		}

		std::string lowercase(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		// temp file helper for serializers that only speak filenames
		class TempFile
		{
		public:
			explicit TempFile(const std::string& extension)
			{
				static std::atomic<int> counter{ 0 };
				const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
				path = (std::filesystem::temp_directory_path() / ("canvas_" + std::to_string(stamp) + "_" + std::to_string(counter++) + extension)).string();
			}

			~TempFile()
			{
				std::error_code error;
				std::filesystem::remove(path, error);
			}

			std::string path;
		};

		std::vector<uint8_t> readAll(const std::string& path)
		{
			if (!IO::File::exists(path))
				throw std::runtime_error("File not found: " + path);

			IO::File file(path, IO::FileMode::ReadBinary);
			std::vector<uint8_t> bytes = file.readAllBytes();
			file.close();
			return bytes;
		}

		void writeAll(const std::string& path, const std::vector<uint8_t>& bytes)
		{
			IO::File file(path, IO::FileMode::WriteBinary);
			file.writeAllBytes(bytes);
			file.flush();
			file.close();
		}
	}

	const char* formatName(ChartFormat format)
	{
		switch (format)
		{
		case ChartFormat::Mmws: return "MikuMikuWorld score (MMWS)";
		case ChartFormat::CcMmws: return "MikuMikuWorld4CC score (CCMMWS)";
		case ChartFormat::UcMmws: return "UntitledCharts MMW score (UCMMWS)";
		case ChartFormat::Sus: return "Sliding Universal Score (.sus)";
		case ChartFormat::MusicScoreJson: return "Project SEKAI MusicScoreData (.json)";
		case ChartFormat::Usc: return "Universal Sekai Chart (.usc)";
		case ChartFormat::LevelDataNextSekai: return "Sonolus LevelData (Next Sekai / UntitledCharts)";
		case ChartFormat::LevelDataChartCyanvas: return "Sonolus LevelData (Chart Cyanvas)";
		default: return "Unknown";
		}
	}

	const char* exportFormatName(ExportFormat format)
	{
		switch (format)
		{
		case ExportFormat::Mmws: return "MikuMikuWorld score (.mmws)";
		case ExportFormat::MmwsExtended: return "MikuMikuWorld score, extended v7 (.mmws)";
		case ExportFormat::CcMmws: return "MikuMikuWorld4CC score (.ccmmws)";
		case ExportFormat::Sus: return "Sliding Universal Score (.sus)";
		case ExportFormat::SusNoNoteSpeed: return "Sliding Universal Score - no note speed (.sus)";
		case ExportFormat::MusicScoreJson: return "Project SEKAI MusicScoreData (.json)";
		case ExportFormat::MusicScoreJsonBase64: return "Project SEKAI MusicScoreData, base64 gzip (.json)";
		case ExportFormat::Usc: return "Universal Sekai Chart (.usc)";
		case ExportFormat::LevelDataNextSekai: return "Sonolus LevelData - Next Sekai (.json.gz)";
		default: return "Unknown";
		}
	}

	const char* exportFormatExtension(ExportFormat format)
	{
		switch (format)
		{
		case ExportFormat::Mmws:
		case ExportFormat::MmwsExtended: return "mmws";
		case ExportFormat::CcMmws: return "ccmmws";
		case ExportFormat::Sus:
		case ExportFormat::SusNoNoteSpeed: return "sus";
		case ExportFormat::MusicScoreJson:
		case ExportFormat::MusicScoreJsonBase64: return "json";
		case ExportFormat::Usc: return "usc";
		case ExportFormat::LevelDataNextSekai: return "json.gz";
		default: return "";
		}
	}

	std::optional<ExportFormat> exportFormatFromName(const std::string& input)
	{
		std::string name = lowercase(input);
		if (!name.empty() && name[0] == '.')
			name.erase(0, 1);

		if (name == "mmws") return ExportFormat::Mmws;
		if (name == "mmws-v7" || name == "mmws-extended" || name == "mmws_v7") return ExportFormat::MmwsExtended;
		if (name == "ccmmws" || name == "mmw4cc") return ExportFormat::CcMmws;
		if (name == "sus") return ExportFormat::Sus;
		if (name == "sus-no-speed" || name == "sus_no_speed") return ExportFormat::SusNoNoteSpeed;
		if (name == "json" || name == "musicscore" || name == "musicscoredata") return ExportFormat::MusicScoreJson;
		if (name == "json-base64" || name == "musicscore-base64" || name == "pjsk") return ExportFormat::MusicScoreJsonBase64;
		if (name == "usc") return ExportFormat::Usc;
		if (name == "json.gz" || name == "leveldata" || name == "level" || name == "nextsekai") return ExportFormat::LevelDataNextSekai;
		return std::nullopt;
	}

	std::optional<ExportFormat> exportFormatForPath(const std::string& path)
	{
		const std::string lower = lowercase(IO::File::getFilename(path));
		if (lower.size() > 8 && lower.compare(lower.size() - 8, 8, ".json.gz") == 0)
			return ExportFormat::LevelDataNextSekai;
		const size_t dot = lower.find_last_of('.');
		if (dot == std::string::npos)
			return std::nullopt;
		return exportFormatFromName(lower.substr(dot + 1));
	}

	bool looksLikeChartFile(const std::string& path)
	{
		static const char* extensions[] = { ".canvassong", ".ce", ".mmws", ".ccmmws", ".ucmmws", ".unchmmws", ".sus", ".usc", ".json", ".gz", ".pjsk", ".txt" };
		const std::string lower = lowercase(path);
		for (const char* extension : extensions)
		{
			const size_t length = std::strlen(extension);
			if (lower.size() > length && lower.compare(lower.size() - length, length, extension) == 0)
				return true;
		}
		// LevelData files are often just called "LevelData" (no extension)
		return lowercase(IO::File::getFilenameWithoutExtension(path)) == "leveldata";
	}

	const char* chartFilePatterns()
	{
		return "*.canvassong;*.ce;*.mmws;*.ccmmws;*.ucmmws;*.unchmmws;*.sus;*.usc;*.json;*.json.gz;*.gz;LevelData";
	}

	ChartFormat detectFormat(const std::vector<uint8_t>& raw)
	{
		if (startsWithBytes(raw, "MMWS\0", 5))
			return ChartFormat::Mmws;
		if (startsWithBytes(raw, "CCMMWS\0", 7))
			return ChartFormat::CcMmws;
		if (startsWithBytes(raw, "UCMMWS\0", 7))
			return ChartFormat::UcMmws;

		std::vector<uint8_t> bytes = raw;
		if (IO::isGzipCompressed(bytes))
			bytes = IO::inflateGzip(bytes);

		// skip a UTF-8 BOM and leading whitespace
		size_t start = 0;
		if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
			start = 3;
		while (start < bytes.size() && std::isspace(bytes[start]))
			++start;

		if (start < bytes.size() && bytes[start] == '{')
		{
			const nlohmann::json value = nlohmann::json::parse(bytes.begin() + start, bytes.end(), nullptr, false);
			if (value.is_object())
			{
				if (value.contains("usc") && value.contains("version"))
					return ChartFormat::Usc;
				if (value.contains("NoteList") || value.contains("MusicScoreEventDataList"))
					return ChartFormat::MusicScoreJson;
				switch (LevelDataSerializer::detect(value))
				{
				case LevelDataSerializer::Variant::ChartCyanvas: return ChartFormat::LevelDataChartCyanvas;
				case LevelDataSerializer::Variant::NextSekai: return ChartFormat::LevelDataNextSekai;
				default: break;
				}
			}
			return ChartFormat::Unknown;
		}

		// base64(gzip(MusicScoreData))
		if (!IO::isGzipCompressed(raw) && raw.size() > 16)
		{
			std::vector<uint8_t> decoded;
			if (IO::base64Decode(std::string(raw.begin(), raw.end()), decoded) && IO::isGzipCompressed(decoded))
			{
				decoded = IO::inflateGzip(decoded);
				const nlohmann::json value = nlohmann::json::parse(decoded.begin(), decoded.end(), nullptr, false);
				if (value.is_object() && (value.contains("NoteList") || value.contains("MusicScoreEventDataList")))
					return ChartFormat::MusicScoreJson;
			}
		}

		// .sus: text with "#..." lines, some of them data lines (#mmmXX:)
		const std::string text(bytes.begin() + start, bytes.begin() + std::min(bytes.size(), start + 65536));
		if (text.find('#') != std::string::npos && (text.find(":") != std::string::npos))
		{
			size_t pos = 0;
			while ((pos = text.find('#', pos)) != std::string::npos)
			{
				if (pos + 4 < text.size() && std::isdigit(static_cast<unsigned char>(text[pos + 1])) &&
					std::isdigit(static_cast<unsigned char>(text[pos + 2])) && std::isdigit(static_cast<unsigned char>(text[pos + 3])))
					return ChartFormat::Sus;
				++pos;
			}
			if (text.find("#TITLE") != std::string::npos || text.find("#REQUEST") != std::string::npos)
				return ChartFormat::Sus;
		}

		return ChartFormat::Unknown;
	}

	LoadResult loadChart(const std::string& path)
	{
		const std::vector<uint8_t> bytes = readAll(path);
		LoadResult result;
		result.format = detectFormat(bytes);

		std::unique_ptr<ScoreSerializer> serializer;
		switch (result.format)
		{
		case ChartFormat::Mmws:
		case ChartFormat::CcMmws:
		case ChartFormat::UcMmws:
			serializer = std::make_unique<NativeScoreSerializer>();
			break;
		case ChartFormat::Sus:
			serializer = std::make_unique<SusSerializer>();
			break;
		case ChartFormat::MusicScoreJson:
		{
			auto json = std::make_unique<CustomScoreJsonSerializer>();
			result.score = json->parseBytes(bytes);
			result.warnings = json->warnings;
			return result;
		}
		case ChartFormat::Usc:
			serializer = std::make_unique<UscSerializer>();
			break;
		case ChartFormat::LevelDataNextSekai:
		{
			// Next SEKAI's own editor saves stages and a camera in the same LevelData
			const std::vector<uint8_t> raw = readAll(path);
			const std::string bytes(raw.begin(), raw.end());
			std::string error;
			Score score;
			Canvas::Stages::StageSet stages;
			if (Canvas::Stages::importLevelData(bytes, score, stages, error, &result.warnings) && stages.enabled)
			{
				score.stages = std::move(stages);
				result.score = std::move(score);
				return result;
			}
			result.warnings.clear();
			serializer = std::make_unique<LevelDataSerializer>();
			break;
		}
		case ChartFormat::LevelDataChartCyanvas:
			serializer = std::make_unique<LevelDataSerializer>();
			break;
		default:
			throw std::runtime_error("Unrecognised chart format: " + IO::File::getFilename(path));
		}

		result.score = serializer->deserialize(path);
		result.warnings = serializer->warnings;
		return result;
	}

	LoadResult loadChartFromBytes(const std::vector<uint8_t>& bytes, const std::string& nameHint)
	{
		TempFile temp(nameHint.empty() ? ".chart" : "_" + IO::File::getFilename(nameHint));
		writeAll(temp.path, bytes);
		return loadChart(temp.path);
	}

	std::vector<std::string> saveChart(const Score& score, const std::string& path, ExportFormat format, const SaveOptions& options)
	{
		std::unique_ptr<ScoreSerializer> serializer;
		switch (format)
		{
		case ExportFormat::Mmws:
			serializer = std::make_unique<NativeScoreSerializer>(NativeScoreSerializer::Flavor::CanvasMmws);
			break;
		case ExportFormat::MmwsExtended:
			serializer = std::make_unique<NativeScoreSerializer>(NativeScoreSerializer::Flavor::CanvasMmwsExtended);
			break;
		case ExportFormat::CcMmws:
			serializer = std::make_unique<NativeScoreSerializer>(NativeScoreSerializer::Flavor::MikuMikuWorld4CC);
			break;
		case ExportFormat::Sus:
			serializer = std::make_unique<SusSerializer>(true, options.susMergeLayers);
			break;
		case ExportFormat::SusNoNoteSpeed:
			serializer = std::make_unique<SusSerializer>(false, options.susMergeLayers);
			break;
		case ExportFormat::MusicScoreJson:
			serializer = std::make_unique<CustomScoreJsonSerializer>(false);
			break;
		case ExportFormat::MusicScoreJsonBase64:
			serializer = std::make_unique<CustomScoreJsonSerializer>(true);
			break;
		case ExportFormat::Usc:
			serializer = std::make_unique<UscSerializer>();
			break;
		case ExportFormat::LevelDataNextSekai:
			if (!score.stages.empty())
			{
				std::string json, error;
				std::vector<std::string> warnings;
				if (!Canvas::Stages::exportLevelData(score, score.stages, json, error, &warnings))
					throw std::runtime_error("Dynamic Stages export: " + error);
				std::vector<uint8_t> bytes(json.begin(), json.end());
				if (options.levelDataGzip)
					bytes = IO::deflateGzip(bytes);
				writeAll(path, bytes);
				return warnings;
			}
			serializer = std::make_unique<LevelDataSerializer>(options.levelDataGzip);
			break;
		default:
			throw std::runtime_error("Unknown export format");
		}

		serializer->serialize(score, path);
		return serializer->warnings;
	}

	std::vector<uint8_t> saveChartToBytes(const Score& score, ExportFormat format, std::vector<std::string>* warnings, const SaveOptions& options)
	{
		TempFile temp(std::string(".") + exportFormatExtension(format));
		std::vector<std::string> w = saveChart(score, temp.path, format, options);
		if (warnings)
			*warnings = std::move(w);
		return readAll(temp.path);
	}
}
