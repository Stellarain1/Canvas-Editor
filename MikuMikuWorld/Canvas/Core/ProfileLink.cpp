#include "ProfileLink.h"
#include "../../IO.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace Canvas
{
	namespace
	{
		std::string trimSlashes(std::string s)
		{
			while (!s.empty() && s.back() == '/')
				s.pop_back();
			return s;
		}

		std::string text(const nlohmann::json& value)
		{
			if (value.is_string())
				return value.get<std::string>();
			if (value.is_number_integer())
				return std::to_string(value.get<long long>());
			if (value.is_number())
				return IO::formatString("%g", value.get<double>());
			return {};
		}

		std::string srlUrl(const nlohmann::json& item, const char* key)
		{
			if (!item.contains(key) || !item[key].is_object())
				return {};
			return item[key].value("url", "");
		}
	}

	nlohmann::json ProfileLink::toJson() const
	{
		return {
			{ "enabled", enabled },
			{ "display_name", displayName },
			{ "server", server },
			{ "keywords", keywords },
			{ "token", token },
			{ "push_target", pushTarget },
		};
	}

	ProfileLink ProfileLink::fromJson(const nlohmann::json& json)
	{
		ProfileLink profile;
		if (!json.is_object())
			return profile;
		profile.enabled = json.value("enabled", false);
		profile.displayName = json.value("display_name", "");
		profile.server = json.value("server", "");
		profile.keywords = json.value("keywords", "");
		profile.token = json.value("token", "");
		profile.pushTarget = json.value("push_target", "");
		return profile;
	}

	std::string urlEncode(const std::string& value)
	{
		std::string out;
		for (unsigned char c : value)
		{
			if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
				out += static_cast<char>(c);
			else
			{
				char buf[4];
				std::snprintf(buf, sizeof(buf), "%%%02X", c);
				out += buf;
			}
		}
		return out;
	}

	std::string resolveUrl(const std::string& server, const std::string& url)
	{
		if (url.empty())
			return {};
		if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
			return url;
		if (url.rfind("//", 0) == 0)
			return "https:" + url;
		const std::string base = trimSlashes(server);
		return url[0] == '/' ? base + url : base + "/" + url;
	}

	std::string levelListUrl(const ProfileLink& profile, int page)
	{
		std::string url = trimSlashes(profile.server) + "/sonolus/levels/list?type=quick";
		if (!profile.keywords.empty())
			url += "&keywords=" + urlEncode(profile.keywords);
		url += "&page=" + std::to_string(std::max(0, page));
		return url;
	}

	RemoteLevelPage parseLevelList(const std::string& body, const std::string& server)
	{
		const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
		if (!json.is_object() || !json.contains("items") || !json["items"].is_array())
			throw std::runtime_error("The server did not answer with a Sonolus level list.");

		RemoteLevelPage page;
		page.pageCount = json.value("pageCount", 1);
		for (const auto& item : json["items"])
		{
			if (!item.is_object())
				continue;
			RemoteLevel level;
			level.name = text(item.value("name", nlohmann::json()));
			level.title = text(item.value("title", nlohmann::json()));
			level.author = text(item.value("author", nlohmann::json()));
			level.rating = text(item.value("rating", nlohmann::json()));
			level.dataUrl = resolveUrl(server, srlUrl(item, "data"));
			level.coverUrl = resolveUrl(server, srlUrl(item, "cover"));
			level.bgmUrl = resolveUrl(server, srlUrl(item, "bgm"));
			if (!level.dataUrl.empty())
				page.levels.push_back(level);
		}
		return page;
	}

	std::string curlGetCommand(const ProfileLink& profile, const std::string& url, const std::string& outputFile, const std::string& curl)
	{
		std::string cmd = IO::shellQuote(curl) + " -sS -f -L --max-time 60";
		if (!profile.token.empty())
			cmd += " -H " + IO::shellQuote("Authorization: Bearer " + profile.token);
		if (!outputFile.empty())
			cmd += " -o " + IO::shellQuote(outputFile);
		cmd += " " + IO::shellQuote(url);
		return cmd;
	}
}
