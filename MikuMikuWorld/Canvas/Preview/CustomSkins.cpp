#include "CustomSkins.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../Audio/Sound.h"
#include "../../File.h"
#include "../../IO.h"
#include "../../NoteSkin.h"
#include "../../ResourceManager.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;
using namespace MikuMikuWorld;

namespace Canvas::CustomSkins
{
	namespace
	{
		const char* subfolder(Kind kind)
		{
			switch (kind)
			{
			case Kind::Notes: return "note_skins";
			case Kind::Effects: return "effect_skins";
			default: return "sound_skins";
			}
		}

		std::string builtIn(Kind kind)
		{
			const std::string res = Application::getAppDir() + "res/";
			switch (kind)
			{
			case Kind::Notes: return res + "notes/01/";
			case Kind::Effects: return res + "effect/0/";
			default: return res + "sound/01/";
			}
		}

		std::set<std::string>& loadedNoteSkins()
		{
			static std::set<std::string> loaded;
			return loaded;
		}

		std::string slash(std::string path)
		{
			if (!path.empty() && path.back() != '/' && path.back() != '\\')
				path += '/';
			return path;
		}
	}

	std::string folder(Kind kind)
	{
		return (fs::u8path(Application::getUserDataDir()) / "custom" / subfolder(kind)).u8string();
	}

	std::vector<std::string> list(Kind kind)
	{
		std::vector<std::string> names;
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(fs::u8path(folder(kind)), ec))
			if (entry.is_directory(ec))
				names.push_back(entry.path().filename().u8string());
		std::sort(names.begin(), names.end());
		return names;
	}

	bool createFromBuiltIn(Kind kind, std::string& name, std::string& error)
	{
		std::string clean;
		for (char c : name)
			if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || static_cast<unsigned char>(c) >= 0x80)
				clean.push_back(c);
		name = clean;
		if (name.empty())
		{
			error = "Give it a name.";
			return false;
		}
		std::error_code ec;
		const fs::path target = fs::u8path(folder(kind)) / fs::u8path(name);
		if (fs::exists(target, ec))
		{
			error = "That name is taken.";
			return false;
		}
		fs::create_directories(target, ec);
		fs::copy(fs::u8path(builtIn(kind)), target, fs::copy_options::recursive, ec);
		if (ec)
		{
			error = "Could not copy the default set: " + ec.message();
			return false;
		}
		return true;
	}

	void openFolder(Kind kind)
	{
		std::error_code ec;
		fs::create_directories(fs::u8path(folder(kind)), ec);
		IO::openInShell(folder(kind));
	}

	void loadNoteSkins()
	{
		const std::string defaults = builtIn(Kind::Notes);
		for (const std::string& name : list(Kind::Notes))
		{
			if (!loadedNoteSkins().insert(name).second)
				continue;
			const fs::path dir = fs::u8path(folder(Kind::Notes)) / fs::u8path(name);
			std::error_code ec;
			// the sprite maps are what cut the sheets into notes: a skin without them gets the default's
			fs::create_directories(dir / "spr", ec);
			int indices[3]{};
			int slot = 0;
			for (const char* file : { "notes", "longNoteLine", "touchLine_eff" })
			{
				const fs::path mine = dir / (std::string(file) + ".png");
				const fs::path map = dir / "spr" / (std::string(file) + ".txt");
				const std::string defaultMap = defaults + "spr/" + file + ".txt";
				if (fs::exists(mine, ec) && !fs::exists(map, ec) && IO::File::exists(defaultMap))
					fs::copy_file(fs::u8path(defaultMap), map, ec);
				const std::string path = fs::exists(mine, ec) ? mine.u8string() : defaults + file + ".png";
				ResourceManager::loadTexture(path);
				indices[slot++] = ResourceManager::getTextureByFilename(path);
			}
			noteSkins.add(name, indices[0], indices[1], indices[2]);
		}
		if (config.notesSkin >= static_cast<int>(noteSkins.count()))
			config.notesSkin = 0;
	}

	int effectProfileCount()
	{
		return 2 + static_cast<int>(list(Kind::Effects).size());
	}

	std::string effectProfileName(int profile)
	{
		if (profile == 0)
			return "Normal";
		if (profile == 1)
			return "Reduced";
		const auto names = list(Kind::Effects);
		return profile - 2 < static_cast<int>(names.size()) ? names[profile - 2] : std::string("?");
	}

	std::string effectFile(int profile, const std::string& fileName)
	{
		const std::string res = Application::getAppDir() + "res/effect/";
		if (profile == 0 || profile == 1)
			return res + std::to_string(profile) + "/" + fileName;
		const auto names = list(Kind::Effects);
		if (profile - 2 >= 0 && profile - 2 < static_cast<int>(names.size()))
		{
			const std::string mine = slash((fs::u8path(folder(Kind::Effects)) / fs::u8path(names[profile - 2])).u8string()) + fileName;
			if (IO::File::exists(mine))
				return mine;
		}
		return res + "0/" + fileName;
	}

	std::string soundDir(size_t profileIndex)
	{
		if (profileIndex + 1 < Audio::soundEffectsProfileCount)
			return IO::formatString("%sres/sound/%02d/", Application::getAppDir().c_str(), static_cast<int>(profileIndex + 1));
		// the last slot: the chosen custom set
		if (!config.customSoundSet.empty())
		{
			const fs::path dir = fs::u8path(folder(Kind::Sounds)) / fs::u8path(config.customSoundSet);
			std::error_code ec;
			if (fs::is_directory(dir, ec))
				return slash(dir.u8string());
		}
		return builtIn(Kind::Sounds);
	}

	std::string soundFile(size_t profileIndex, const std::string& name)
	{
		const std::string mine = Audio::soundEffectFile(soundDir(profileIndex), name);
		if (IO::File::exists(mine))
			return mine;
		return Audio::soundEffectFile(builtIn(Kind::Sounds), name);
	}
}
