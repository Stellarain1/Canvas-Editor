#pragma once
#include <json.hpp>
#include <string>
#include <vector>

// Optional profile link: read-only access to charts on a Sonolus-compatible server, which is
// what chart hosting sites such as UntitledCharts run. Off by default and there is no built-in
// server: the user enters a server address (and, if the server needs one, a token).
//
// Pulling uses the standard Sonolus server API:
//   GET {server}/sonolus/levels/list?keywords=...&page=N   -> { pageCount, items: [LevelItem] }
//   LevelItem.data.url                                      -> gzip'd LevelData (Canvas imports it)
// Pushing is done through an export target (ExportTargets.h), because upload APIs are
// site-specific; the profile only remembers which target to use.
namespace Canvas
{
	struct ProfileLink
	{
		bool enabled{ false };
		std::string displayName;	// shown in the UI only
		std::string server;			// e.g. https://example.com (no trailing /sonolus)
		std::string keywords;		// search filter, usually the user's author name
		std::string token;			// optional, sent as "Authorization: Bearer <token>"
		std::string pushTarget;		// name of an export target used for "push"

		nlohmann::json toJson() const;
		static ProfileLink fromJson(const nlohmann::json& json);
		bool usable() const { return enabled && !server.empty(); }
	};

	struct RemoteLevel
	{
		std::string name;		// server id
		std::string title;
		std::string author;
		std::string rating;
		std::string dataUrl;	// absolute
		std::string coverUrl;	// absolute, may be empty
		std::string bgmUrl;		// absolute, may be empty
	};

	struct RemoteLevelPage
	{
		int pageCount{ 0 };
		std::vector<RemoteLevel> levels;
	};

	// {server}/sonolus/levels/list?keywords=..&page=..
	std::string levelListUrl(const ProfileLink& profile, int page);

	// Resolves a server-relative URL ("/sonolus/repository/..") against the server address.
	std::string resolveUrl(const std::string& server, const std::string& url);

	// Parses a Sonolus level list response. Throws std::runtime_error on malformed JSON.
	RemoteLevelPage parseLevelList(const std::string& body, const std::string& server);

	std::string urlEncode(const std::string& text);

	// curl command line fetching `url` (to stdout, or into `outputFile` when set).
	std::string curlGetCommand(const ProfileLink& profile, const std::string& url, const std::string& outputFile = {},
		const std::string& curl = "curl");
}
