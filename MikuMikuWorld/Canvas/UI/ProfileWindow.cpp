#include "../../UI.h"
#include "ProfileWindow.h"
#include "../Core/ChartIO.h"
#include "../Core/ExportTargets.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../IconsFontAwesome5.h"
#include "../../ImGui/imgui_stdlib.h"
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace MikuMikuWorld
{
	namespace
	{
		constexpr const char* POPUP = "###profile_browser";

		template <typename T>
		bool ready(const std::future<T>& job)
		{
			return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}
	}

	void ProfileWindow::open()
	{
		const Canvas::ProfileLink profile = Canvas::ProfileLink::fromJson(config.profileLink);
		search = profile.keywords;
		page = 0;
		levels = {};
		status.clear();
		openRequested = true;
		if (profile.usable())
			requestList();
	}

	void ProfileWindow::requestList()
	{
		if (listJob.valid())
			return;

		Canvas::ProfileLink profile = Canvas::ProfileLink::fromJson(config.profileLink);
		profile.keywords = search;
		const int requestedPage = page;
		status = getString("profile_loading");
		listJob = std::async(std::launch::async, [profile, requestedPage]()
		{
			ListResult result;
			if (!IO::commandExists("curl"))
			{
				result.error = "curl was not found; it is needed for the profile link.";
				return result;
			}
			int exitCode = -1;
			const std::string body = IO::runProcess(Canvas::curlGetCommand(profile, Canvas::levelListUrl(profile, requestedPage)) + " 2>&1", &exitCode);
			if (exitCode != 0)
			{
				result.error = body.empty() ? "The server could not be reached." : body;
				return result;
			}
			try
			{
				result.page = Canvas::parseLevelList(body, profile.server);
				result.ok = true;
			}
			catch (const std::exception& ex)
			{
				result.error = ex.what();
			}
			return result;
		});
	}

	void ProfileWindow::requestPull(const Canvas::RemoteLevel& level)
	{
		if (pullJob.valid())
			return;

		const Canvas::ProfileLink profile = Canvas::ProfileLink::fromJson(config.profileLink);
		const std::string folder = (fs::u8path(Application::getUserDataDir()) / "downloads" /
			fs::u8path(Canvas::sanitizeFileName(level.name.empty() ? level.title : level.name))).u8string();
		pulling = level.title;
		pullJob = std::async(std::launch::async, [profile, level, folder]()
		{
			PullResult result;
			std::error_code error;
			fs::create_directories(fs::u8path(folder), error);

			const std::string dataPath = (fs::u8path(folder) / "LevelData").u8string();
			int exitCode = -1;
			const std::string output = IO::runProcess(Canvas::curlGetCommand(profile, level.dataUrl, dataPath) + " 2>&1", &exitCode);
			if (exitCode != 0)
			{
				result.error = output.empty() ? "Download failed." : output;
				return result;
			}

			// the song, when the server has one: it becomes the chart's music
			std::string musicPath;
			if (!level.bgmUrl.empty())
			{
				const std::string bgm = (fs::u8path(folder) / "bgm.mp3").u8string();
				IO::runProcess(Canvas::curlGetCommand(profile, level.bgmUrl, bgm) + " 2>&1", &exitCode);
				if (exitCode == 0)
					musicPath = bgm;
			}

			try
			{
				Canvas::LoadResult loaded = Canvas::loadChart(dataPath);
				loaded.score.metadata.title = level.title;
				loaded.score.metadata.author = level.author;
				if (!musicPath.empty())
					loaded.score.metadata.musicFile = musicPath;
				result.chartPath = (fs::u8path(folder) / (Canvas::sanitizeFileName(level.title.empty() ? "chart" : level.title) + ".mmws")).u8string();
				Canvas::saveChart(loaded.score, result.chartPath, Canvas::ExportFormat::Mmws);
				result.ok = true;
			}
			catch (const std::exception& ex)
			{
				result.error = ex.what();
			}
			return result;
		});
	}

	std::string ProfileWindow::update()
	{
		std::string chartToOpen;

		if (ready(listJob))
		{
			ListResult result = listJob.get();
			if (result.ok)
			{
				levels = std::move(result.page);
				status = levels.levels.empty() ? getString("profile_no_levels") : "";
			}
			else
				status = result.error;
		}

		if (ready(pullJob))
		{
			PullResult result = pullJob.get();
			pulling.clear();
			if (result.ok)
			{
				chartToOpen = result.chartPath;
				closeRequested = true;
			}
			else
				status = result.error;
		}

		if (openRequested)
		{
			ImGui::OpenPopup(POPUP);
			openRequested = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize({ 640, 460 }, ImGuiCond_Always);
		const Canvas::ProfileLink profile = Canvas::ProfileLink::fromJson(config.profileLink);
		const std::string title = IO::formatString("%s %s%s", ICON_FA_USER,
			profile.displayName.empty() ? getString("profile_link") : profile.displayName.c_str(), POPUP);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize))
			return chartToOpen;

		if (closeRequested)
		{
			closeRequested = false;
			ImGui::CloseCurrentPopup();
		}

		if (!profile.usable())
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(getString("profile_not_set_up"));
			ImGui::PopTextWrapPos();
		}
		else
		{
			ImGui::TextDisabled("%s", profile.server.c_str());
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110);
			const bool submitted = ImGui::InputTextWithHint("##profile_search", getString("profile_search_hint"), &search, ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button(IO::concat(ICON_FA_SEARCH, getString("profile_search"), " ").c_str(), { -1, 0 }) || submitted) && !listJob.valid())
			{
				page = 0;
				requestList();
			}

			if (!status.empty())
			{
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", status.c_str());
				ImGui::PopTextWrapPos();
			}

			if (ImGui::BeginTable("##remote_levels", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
				{ 0, -ImGui::GetFrameHeightWithSpacing() * 1.2f }))
			{
				ImGui::TableSetupColumn(getString("profile_title"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(getString("profile_author"), ImGuiTableColumnFlags_WidthFixed, 140);
				ImGui::TableSetupColumn(getString("song_level"), ImGuiTableColumnFlags_WidthFixed, 36);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
				ImGui::TableHeadersRow();
				for (size_t i = 0; i < levels.levels.size(); ++i)
				{
					const auto& level = levels.levels[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(level.title.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(level.author.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(level.rating.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::BeginDisabled(pullJob.valid());
					if (ImGui::SmallButton(IO::concat(ICON_FA_DOWNLOAD, getString("profile_open"), " ").c_str()))
						requestPull(level);
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			ImGui::BeginDisabled(page <= 0 || listJob.valid());
			if (ImGui::Button(ICON_FA_ANGLE_LEFT))
			{
				--page;
				requestList();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Text("%d / %d", page + 1, std::max(1, levels.pageCount));
			ImGui::SameLine();
			ImGui::BeginDisabled(page + 1 >= levels.pageCount || listJob.valid());
			if (ImGui::Button(ICON_FA_ANGLE_RIGHT))
			{
				++page;
				requestList();
			}
			ImGui::EndDisabled();
			if (!pulling.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s %s...", getString("profile_downloading"), pulling.c_str());
			}
		}

		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110);
		if (ImGui::Button(getString("close"), { 100, 0 }))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return chartToOpen;
	}
}
