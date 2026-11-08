#include "Localization.h"
#include "DefaultLanguage.h"
#include "Canvas/UI/CanvasStrings.h"
#include "Canvas/UI/CanvasStrings121.h"
#include "Canvas/UI/CanvasStrings122.h"
#include "Canvas/UI/CanvasStrings130.h"
#include "Canvas/UI/CanvasStringsBpm.h"
#include "Canvas/UI/CanvasStringsStages.h"
#include "Canvas/UI/CanvasStringsStagesUi.h"
#include "IO.h"
#include "File.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace MikuMikuWorld
{
	static std::string empty;

	namespace
	{
		std::string normalizeLanguageCode(std::string code)
		{
			std::replace(code.begin(), code.end(), '_', '-');

			const size_t end = code.find_first_of(".@");
			if (end != std::string::npos)
				code = code.substr(0, end);

			std::transform(code.begin(), code.end(), code.begin(), [](unsigned char ch)
			{
				return (char)std::tolower(ch);
			});

			return code;
		}

		std::string canonicalLanguageCode(std::string code)
		{
			code = normalizeLanguageCode(std::move(code));

			if (code == "zh" || code == "zh-cn" || code == "zh-sg" || code == "zh-hans")
				return "zh-cn";

			if (code == "zh-tw" || code == "zh-hk" || code == "zh-mo" || code == "zh-hant")
				return "zh-tw";

			return code;
		}
	}

	std::map<std::string, std::unique_ptr<Language>> Localization::languages;
	Language* Localization::currentLanguage = nullptr;

	void Localization::load(const char* code, std::string name, const std::string& filename)
	{
		if (!IO::File::exists(filename))
			return;

        languages[code] = std::make_unique<Language>(code, name, filename);
	}

	bool Localization::setLanguage(const std::string& code)
	{
		auto it = Localization::languages.find(code);
		if (it != Localization::languages.end())
		{
			Localization::currentLanguage = it->second.get();
			return true;
		}

		std::string normalized = canonicalLanguageCode(code);
		it = Localization::languages.find(normalized);
		if (it != Localization::languages.end())
		{
			Localization::currentLanguage = it->second.get();
			return true;
		}

		size_t separator = normalized.find('-');
		if (separator != std::string::npos)
		{
			it = Localization::languages.find(normalized.substr(0, separator));
			if (it != Localization::languages.end())
			{
				Localization::currentLanguage = it->second.get();
				return true;
			}
		}

		return false;
	}

	void Localization::loadDefault()
	{
		std::map<std::string, std::string> english = en;
		for (const auto& [key, value] : canvasEnglishStrings())
			english.insert_or_assign(key, value);
		for (const auto& [key, value] : canvas121EnglishStrings())
			english.insert_or_assign(key, value);
		for (const auto& [key, value] : canvas122EnglishStrings())
			english.insert_or_assign(key, value);
		for (const auto& [key, value] : canvas130EnglishStrings())
			english.insert_or_assign(key, value);
		for (const auto& [key, value] : canvasBpmEnglishStrings())
			english.insert_or_assign(key, value);
		for (const auto& [key, value] : canvasStagesEnglishStrings())
			english.insert_or_assign(key, value);
		for (const auto& [key, value] : canvasStagesUiEnglishStrings())
			english.insert_or_assign(key, value);
		languages["en"] = std::make_unique<Language>("en", "English", english);
	}

	const char* getString(const std::string& key)
	{
		if (Localization::currentLanguage)
		{
			if (const char* value = Localization::currentLanguage->find(key))
				return value;
		}

		// fall back to English, then to the key itself. The key is interned so the pointer
		// stays valid (callers keep these around, e.g. as ImGui labels).
		auto english = Localization::languages.find("en");
		if (english != Localization::languages.end())
		{
			if (const char* value = english->second->find(key))
				return value;
		}

		static std::set<std::string> internedKeys;
		return internedKeys.insert(key).first->c_str();
	}
}
