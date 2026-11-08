#include "TestFramework.h"
#include "Canvas/Core/ExportTargets.h"
#include "Canvas/Core/ProfileLink.h"
#include <filesystem>

using namespace Canvas;

namespace
{
	// planned paths use the platform separator (a backslash on Windows)
	std::string generic(const std::string& path) { return std::filesystem::u8path(path).generic_u8string(); }
}

TEST(templates_expand_known_names_only)
{
	const TemplateVars vars{ { "title", "Song" }, { "difficulty", "master" } };
	CHECK_EQ(expandTemplate("{title} - {difficulty}.{ext}", vars), std::string("Song - master.{ext}"));
	CHECK_EQ(expandTemplate("no {braces", vars), std::string("no {braces"));
	CHECK_EQ(expandTemplate("{}", vars), std::string("{}"));
}

TEST(file_names_are_sanitized)
{
	CHECK_EQ(sanitizeFileName("a/b:c*d?.sus"), std::string("a_b_c_d_.sus"));
	CHECK_EQ(sanitizeFileName("trailing. "), std::string("trailing"));
	CHECK_EQ(sanitizeFileName(""), std::string("chart"));
}

TEST(export_targets_round_trip_json)
{
	ExportTarget http;
	http.name = "My server";
	http.kind = ExportTargetKind::Http;
	http.format = ExportFormat::LevelDataNextSekai;
	http.url = "https://example.com/upload?title={title}";
	http.headers = { "Authorization: Bearer abc" };
	http.fields = { { "title", "{title}" } };

	ExportTarget folder;
	folder.name = "Backups";
	folder.folder = "/tmp/charts";

	const auto json = writeExportTargets({ http, folder });
	const auto back = readExportTargets(json);
	CHECK_EQ(back.size(), size_t(2));
	CHECK_EQ(back[0].name, std::string("My server"));
	CHECK(back[0].kind == ExportTargetKind::Http);
	CHECK(back[0].format == ExportFormat::LevelDataNextSekai);
	CHECK_EQ(back[0].headers.size(), size_t(1));
	CHECK_EQ(back[0].fields.at("title"), std::string("{title}"));
	CHECK(back[1].kind == ExportTargetKind::Folder);
	CHECK_EQ(back[1].folder, std::string("/tmp/charts"));
	// nothing is configured unless the user adds it
	CHECK(readExportTargets(nlohmann::json::array()).empty());
	CHECK(readExportTargets(nlohmann::json()).empty());
}

TEST(publish_plans)
{
	const TemplateVars vars{ { "title", "A/B" }, { "difficulty", "expert" } };

	ExportTarget folder;
	folder.kind = ExportTargetKind::Folder;
	folder.folder = "/charts";
	folder.format = ExportFormat::Sus;
	const auto folderPlan = planPublish(folder, vars, "/tmp");
	CHECK_EQ(generic(folderPlan.finalPath), std::string("/charts/A_B - expert.sus"));
	CHECK(folderPlan.command.empty());

	ExportTarget command;
	command.kind = ExportTargetKind::Command;
	command.commandTemplate = "scp {file} me@host:/charts/";
	command.format = ExportFormat::Usc;
	const auto commandPlan = planPublish(command, vars, "/tmp");
	CHECK_EQ(generic(commandPlan.exportPath), std::string("/tmp/A_B - expert.usc"));
	CHECK(commandPlan.command.find("scp ") == 0);
	CHECK(commandPlan.command.find("A_B - expert.usc") != std::string::npos);

	ExportTarget http;
	http.kind = ExportTargetKind::Http;
	http.url = "https://example.com/up";
	http.fields = { { "title", "@/etc/passwd" } };
	const auto httpPlan = planPublish(http, vars, "/tmp", "curl");
	CHECK(httpPlan.command.find("-F ") != std::string::npos);
	CHECK(httpPlan.command.find("--form-string") != std::string::npos);
	CHECK(httpPlan.command.find("https://example.com/up") != std::string::npos);
}

TEST(profile_is_off_by_default)
{
	const ProfileLink profile = ProfileLink::fromJson(nlohmann::json::object());
	CHECK(!profile.enabled);
	CHECK(profile.server.empty());
	CHECK(!profile.usable());
}

TEST(sonolus_level_list_parsing)
{
	const std::string body = R"({
		"pageCount": 3,
		"items": [
			{ "name": "abc", "title": "Song A", "author": "me", "rating": 30,
			  "data": { "hash": "x", "url": "/sonolus/repository/LevelData/abc" },
			  "cover": { "url": "https://cdn.example.com/cover.png" } },
			{ "name": "no-data", "title": "Broken" }
		]
	})";
	const auto page = parseLevelList(body, "https://example.com/");
	CHECK_EQ(page.pageCount, 3);
	CHECK_EQ(page.levels.size(), size_t(1));
	CHECK_EQ(page.levels[0].title, std::string("Song A"));
	CHECK_EQ(page.levels[0].rating, std::string("30"));
	CHECK_EQ(page.levels[0].dataUrl, std::string("https://example.com/sonolus/repository/LevelData/abc"));
	CHECK_EQ(page.levels[0].coverUrl, std::string("https://cdn.example.com/cover.png"));

	ProfileLink profile;
	profile.server = "https://example.com/";
	profile.keywords = "my name";
	CHECK_EQ(levelListUrl(profile, 1), std::string("https://example.com/sonolus/levels/list?type=quick&keywords=my%20name&page=1"));

	bool threw = false;
	try { parseLevelList("<html>", "x"); } catch (const std::exception&) { threw = true; }
	CHECK(threw);
}
