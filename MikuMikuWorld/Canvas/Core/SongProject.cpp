#include "SongProject.h"
#include "../../IO.h"
#include "../../File.h"
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Canvas
{
	namespace
	{
		constexpr const char* FORMAT = "canvas-editor/song";
		constexpr int VERSION = 1;

		fs::path folderOf(const std::string& path)
		{
			return fs::u8path(path).parent_path();
		}

		std::string toAbsolute(const fs::path& base, const std::string& relative)
		{
			if (relative.empty())
				return {};
			fs::path p = fs::u8path(relative);
			if (p.is_relative())
				p = base / p;
			return p.lexically_normal().u8string();
		}

		std::string toRelative(const fs::path& base, const std::string& absolute)
		{
			if (absolute.empty())
				return {};
			const fs::path p = fs::u8path(absolute);
			std::error_code error;
			const fs::path relative = fs::relative(p, base, error);
			// keep absolute paths that point somewhere else entirely (other drive etc.)
			if (error || relative.empty() || relative.u8string().rfind("..", 0) == 0)
				return p.generic_u8string();
			return relative.generic_u8string();
		}
	}

	const std::vector<std::string>& standardDifficulties()
	{
		static const std::vector<std::string> names{ "easy", "normal", "hard", "expert", "master", "append" };
		return names;
	}

	int difficultyOrder(const std::string& name)
	{
		const auto& names = standardDifficulties();
		auto it = std::find(names.begin(), names.end(), name);
		return it == names.end() ? static_cast<int>(names.size()) : static_cast<int>(it - names.begin());
	}

	std::string defaultDifficultyFile(const std::string& songPath, const std::string& name)
	{
		std::string safe;
		for (char c : name)
			safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
		if (safe.empty())
			safe = "chart";
		return (folderOf(songPath) / (safe + ".ce")).u8string();
	}

	SongDifficulty* SongProject::find(const std::string& name)
	{
		auto it = std::find_if(difficulties.begin(), difficulties.end(), [&](const SongDifficulty& d) { return d.name == name; });
		return it == difficulties.end() ? nullptr : &*it;
	}

	const SongDifficulty* SongProject::find(const std::string& name) const
	{
		return const_cast<SongProject*>(this)->find(name);
	}

	SongProject SongProject::load(const std::string& path)
	{
		if (!IO::File::exists(path))
			throw std::runtime_error("Song file not found: " + path);

		IO::File file(path, IO::FileMode::Read);
		const std::string text = file.readAllText();
		file.close();

		const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
		if (!json.is_object() || json.value("format", "") != FORMAT)
			throw std::runtime_error("Not a Canvas song file: " + IO::File::getFilename(path));
		if (json.value("version", 0) > VERSION)
			throw std::runtime_error("This song file was written by a newer Canvas Editor.");

		const fs::path base = folderOf(path);
		SongProject song;
		song.title = json.value("title", "");
		song.artist = json.value("artist", "");
		song.designer = json.value("designer", "");
		song.musicFile = toAbsolute(base, json.value("music", ""));
		song.jacketFile = toAbsolute(base, json.value("jacket", ""));
		song.musicOffset = json.value("musicOffset", 0.0f);

		if (json.contains("difficulties") && json["difficulties"].is_array())
		{
			for (const auto& entry : json["difficulties"])
			{
				SongDifficulty difficulty;
				difficulty.name = entry.value("name", "");
				difficulty.file = toAbsolute(base, entry.value("file", ""));
				difficulty.level = entry.value("level", 0);
				if (!difficulty.name.empty())
					song.difficulties.push_back(difficulty);
			}
		}

		std::stable_sort(song.difficulties.begin(), song.difficulties.end(),
			[](const SongDifficulty& a, const SongDifficulty& b) { return difficultyOrder(a.name) < difficultyOrder(b.name); });
		return song;
	}

	void SongProject::save(const std::string& path) const
	{
		const fs::path base = folderOf(path);
		nlohmann::ordered_json json;
		json["format"] = FORMAT;
		json["version"] = VERSION;
		json["title"] = title;
		json["artist"] = artist;
		json["designer"] = designer;
		json["music"] = toRelative(base, musicFile);
		json["musicOffset"] = musicOffset;
		json["jacket"] = toRelative(base, jacketFile);
		json["difficulties"] = nlohmann::ordered_json::array();
		for (const auto& difficulty : difficulties)
			json["difficulties"].push_back({ { "name", difficulty.name }, { "file", toRelative(base, difficulty.file) }, { "level", difficulty.level } });

		IO::File file(path, IO::FileMode::Write);
		file.write(json.dump(2));
		file.flush();
		file.close();
	}
}
