#include "../../UI.h"
#include "PublishWindow.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../IconsFontAwesome5.h"
#include "../../ImGui/imgui_stdlib.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace MikuMikuWorld
{
	namespace
	{
		constexpr const char* MANAGER_POPUP = "###export_targets";
		constexpr const char* RESULT_POPUP = "###publish_result";

		std::string timestamp(const char* format)
		{
			const std::time_t now = std::time(nullptr);
			std::tm local{};
#ifdef _WIN32
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			char buffer[32]{};
			std::strftime(buffer, sizeof(buffer), format, &local);
			return buffer;
		}

		const char* kindLabel(Canvas::ExportTargetKind kind)
		{
			switch (kind)
			{
			case Canvas::ExportTargetKind::Folder: return ICON_FA_FOLDER " Folder";
			case Canvas::ExportTargetKind::Command: return ICON_FA_TERMINAL " Command";
			default: return ICON_FA_UPLOAD " HTTP upload";
			}
		}
	}

	void PublishWindow::load()
	{
		if (loaded)
			return;
		targets = Canvas::readExportTargets(config.exportTargets);
		loaded = true;
	}

	void PublishWindow::save()
	{
		config.exportTargets = Canvas::writeExportTargets(targets);
	}

	std::vector<std::string> PublishWindow::targetNames() const
	{
		const auto list = loaded ? targets : Canvas::readExportTargets(config.exportTargets);
		std::vector<std::string> names;
		for (const auto& target : list)
			if (target.enabled && !target.name.empty())
				names.push_back(target.name);
		return names;
	}

	void PublishWindow::openManager()
	{
		load();
		managerRequested = true;
	}

	void PublishWindow::publish(const std::string& targetName, Charts charts)
	{
		load();
		if (running || charts.empty())
			return;

		auto it = std::find_if(targets.begin(), targets.end(), [&](const Canvas::ExportTarget& t) { return t.name == targetName && t.enabled; });
		if (it == targets.end())
			return;
		const Canvas::ExportTarget target = *it;

		std::error_code error;
		const fs::path tempDir = fs::temp_directory_path() /
			("canvas_publish_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		if (target.kind != Canvas::ExportTargetKind::Folder)
			fs::create_directories(tempDir, error);

		Result prepared;
		std::vector<std::string> commands;
		for (const auto& [difficulty, score] : charts)
		{
			Canvas::TemplateVars vars{
				{ "title", score.metadata.title.empty() ? std::string("chart") : score.metadata.title },
				{ "artist", score.metadata.artist },
				{ "designer", score.metadata.author },
				{ "difficulty", difficulty },
				{ "date", timestamp("%Y-%m-%d") },
				{ "time", timestamp("%H%M%S") },
			};

			const Canvas::PublishPlan plan = Canvas::planPublish(target, vars, tempDir.u8string());
			try
			{
				fs::create_directories(fs::u8path(plan.exportPath).parent_path(), error);
				Canvas::SaveOptions options;
				options.susMergeLayers = config.susMergeLayers;
				const auto warnings = Canvas::saveChart(score, plan.exportPath, target.format, options);
				prepared.log += IO::formatString("%s %s -> %s\n", ICON_FA_CHECK, difficulty.c_str(), plan.exportPath.c_str());
				for (const auto& warning : warnings)
					prepared.log += "   " + warning + "\n";
			}
			catch (const std::exception& ex)
			{
				prepared.ok = false;
				prepared.log += IO::formatString("%s %s: %s\n", ICON_FA_TIMES, difficulty.c_str(), ex.what());
				continue;
			}

			if (!plan.command.empty())
				commands.push_back(plan.command);
		}

		if (commands.empty() || !prepared.ok)
		{
			fs::remove_all(tempDir, error);
			result = prepared;
			resultRequested = true;
			return;
		}

		running = true;
		runningLabel = target.name;
		job = std::async(std::launch::async, [commands, prepared, tempDir]()
		{
			Result r = prepared;
			for (size_t i = 0; i < commands.size(); ++i)
			{
				int exitCode = -1;
				// the command itself is not logged: it may contain tokens
				const std::string output = IO::runProcess(commands[i] + " 2>&1", &exitCode);
				r.log += IO::formatString("\n[%zu/%zu] exit code %d\n", i + 1, commands.size(), exitCode);
				if (!output.empty())
					r.log += output + (output.back() == '\n' ? "" : "\n");
				if (exitCode != 0)
					r.ok = false;
			}
			std::error_code error;
			fs::remove_all(tempDir, error);
			return r;
		});
	}

	void PublishWindow::drawTargetEditor(Canvas::ExportTarget& target)
	{
		bool changed = false;
		UI::beginPropertyColumns();

		UI::propertyLabel(getString("target_name"));
		changed |= ImGui::InputText("##target_name", &target.name);
		ImGui::NextColumn();

		UI::propertyLabel(getString("target_enabled"));
		changed |= ImGui::Checkbox("##target_enabled", &target.enabled);
		ImGui::NextColumn();

		UI::propertyLabel(getString("target_format"));
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##target_format", Canvas::exportFormatName(target.format)))
		{
			for (int i = 0; i < static_cast<int>(Canvas::ExportFormat::FormatCount); ++i)
			{
				const auto format = static_cast<Canvas::ExportFormat>(i);
				if (ImGui::Selectable(Canvas::exportFormatName(format), target.format == format))
				{
					target.format = format;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::NextColumn();

		UI::propertyLabel(getString("target_file_name"));
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::InputTextWithHint("##target_file_name", "{title} - {difficulty}.{ext}", &target.fileNameTemplate);
		ImGui::NextColumn();

		switch (target.kind)
		{
		case Canvas::ExportTargetKind::Folder:
		{
			if (UI::addFileProperty(getString("target_folder"), target.folder) == 2)
			{
				IO::FileDialog dialog{};
				dialog.title = getString("target_folder");
				dialog.parentWindowHandle = Application::windowState.windowHandle;
				if (dialog.selectFolder() == IO::FileDialogResult::OK)
					target.folder = dialog.outputFilename;
				changed = true;
			}
			changed |= ImGui::IsItemDeactivatedAfterEdit();
			break;
		}
		case Canvas::ExportTargetKind::Command:
			UI::propertyLabel(getString("target_command"));
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::InputTextWithHint("##target_command", "scp {file} me@example.com:charts/", &target.commandTemplate);
			ImGui::NextColumn();
			break;
		case Canvas::ExportTargetKind::Http:
		{
			UI::propertyLabel(getString("target_url"));
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::InputTextWithHint("##target_url", "https://example.com/api/upload", &target.url);
			ImGui::NextColumn();

			UI::propertyLabel(getString("target_method"));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##target_method", target.method.c_str()))
			{
				for (const char* method : { "POST", "PUT" })
					if (ImGui::Selectable(method, target.method == method))
					{
						target.method = method;
						changed = true;
					}
				ImGui::EndCombo();
			}
			ImGui::NextColumn();

			UI::propertyLabel(getString("target_multipart"));
			changed |= ImGui::Checkbox("##target_multipart", &target.multipart);
			ImGui::NextColumn();

			if (target.multipart)
			{
				UI::propertyLabel(getString("target_field"));
				ImGui::SetNextItemWidth(-1);
				changed |= ImGui::InputText("##target_field", &target.fieldName);
				ImGui::NextColumn();
			}

			// one header / field per line: "Name: value" and "name=value"
			std::string headers;
			for (const auto& header : target.headers)
				headers += header + "\n";
			UI::propertyLabel(getString("target_headers"));
			if (ImGui::InputTextMultiline("##target_headers", &headers, { -1, ImGui::GetTextLineHeight() * 3.5f }))
			{
				target.headers.clear();
				size_t start = 0;
				while (start <= headers.size())
				{
					const size_t end = headers.find('\n', start);
					const std::string line = headers.substr(start, end == std::string::npos ? std::string::npos : end - start);
					if (!line.empty())
						target.headers.push_back(line);
					if (end == std::string::npos)
						break;
					start = end + 1;
				}
				changed = true;
			}
			ImGui::NextColumn();

			std::string fields;
			for (const auto& [key, value] : target.fields)
				fields += key + "=" + value + "\n";
			UI::propertyLabel(getString("target_fields"));
			if (ImGui::InputTextMultiline("##target_fields", &fields, { -1, ImGui::GetTextLineHeight() * 3.5f }))
			{
				target.fields.clear();
				size_t start = 0;
				while (start <= fields.size())
				{
					const size_t end = fields.find('\n', start);
					const std::string line = fields.substr(start, end == std::string::npos ? std::string::npos : end - start);
					const size_t eq = line.find('=');
					if (eq != std::string::npos && eq > 0)
						target.fields[line.substr(0, eq)] = line.substr(eq + 1);
					if (end == std::string::npos)
						break;
					start = end + 1;
				}
				changed = true;
			}
			ImGui::NextColumn();
			break;
		}
		}

		UI::endPropertyColumns();

		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", getString("target_placeholders"));
		if (target.kind == Canvas::ExportTargetKind::Http)
			ImGui::TextDisabled("%s", getString("target_http_note"));
		ImGui::PopTextWrapPos();

		if (changed)
			save();
	}

	void PublishWindow::drawManager()
	{
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize({ 760, 480 }, ImGuiCond_Always);
		const std::string title = IO::formatString("%s %s%s", ICON_FA_UPLOAD, getString("export_targets"), MANAGER_POPUP);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize))
			return;

		const float listWidth = 220;
		ImGui::BeginChild("##target_list", { listWidth, -ImGui::GetFrameHeightWithSpacing() * 2 }, ImGuiChildFlags_Borders);
		if (targets.empty())
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", getString("targets_none"));
			ImGui::PopTextWrapPos();
		}
		for (int i = 0; i < static_cast<int>(targets.size()); ++i)
		{
			ImGui::PushID(i);
			const std::string label = (targets[i].name.empty() ? std::string("(unnamed)") : targets[i].name) + (targets[i].enabled ? "" : "  (off)");
			if (ImGui::Selectable(label.c_str(), selected == i))
				selected = i;
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##target_editor", { 0, -ImGui::GetFrameHeightWithSpacing() * 2 }, ImGuiChildFlags_Borders);
		if (selected >= 0 && selected < static_cast<int>(targets.size()))
		{
			ImGui::TextUnformatted(kindLabel(targets[selected].kind));
			ImGui::Separator();
			drawTargetEditor(targets[selected]);
		}
		else
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", getString("targets_help"));
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();

		// add / remove
		auto add = [&](Canvas::ExportTargetKind kind, const char* baseName)
		{
			Canvas::ExportTarget target;
			target.kind = kind;
			target.name = baseName;
			for (int n = 2; std::any_of(targets.begin(), targets.end(), [&](const Canvas::ExportTarget& t) { return t.name == target.name; }); ++n)
				target.name = std::string(baseName) + " " + std::to_string(n);
			targets.push_back(target);
			selected = static_cast<int>(targets.size()) - 1;
			save();
		};
		if (ImGui::Button(kindLabel(Canvas::ExportTargetKind::Folder)))
			add(Canvas::ExportTargetKind::Folder, "Folder");
		ImGui::SameLine();
		if (ImGui::Button(kindLabel(Canvas::ExportTargetKind::Command)))
			add(Canvas::ExportTargetKind::Command, "Command");
		ImGui::SameLine();
		if (ImGui::Button(kindLabel(Canvas::ExportTargetKind::Http)))
			add(Canvas::ExportTargetKind::Http, "Upload");
		ImGui::SameLine();
		ImGui::BeginDisabled(selected < 0 || selected >= static_cast<int>(targets.size()));
		if (ImGui::Button(IO::concat(ICON_FA_TRASH, getString("target_remove"), " ").c_str()))
		{
			targets.erase(targets.begin() + selected);
			selected = std::min(selected, static_cast<int>(targets.size()) - 1);
			save();
		}
		ImGui::EndDisabled();

		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110);
		if (ImGui::Button("OK", { 100, 0 }))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void PublishWindow::update()
	{
		if (running && job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			result = job.get();
			running = false;
			resultRequested = true;
		}

		if (managerRequested)
		{
			ImGui::OpenPopup(MANAGER_POPUP);
			managerRequested = false;
		}
		drawManager();

		if (running)
		{
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter() + ImVec2{ 0, ImGui::GetMainViewport()->WorkSize.y * 0.4f },
				ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowBgAlpha(0.9f);
			if (ImGui::Begin("##publish_running", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing))
			{
				static const char* spinner = "|/-\\";
				ImGui::Text("%s %s %c", getString("publishing_to"), runningLabel.c_str(), spinner[static_cast<int>(ImGui::GetTime() * 8) % 4]);
			}
			ImGui::End();
		}

		if (resultRequested)
		{
			ImGui::OpenPopup(RESULT_POPUP);
			resultRequested = false;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize({ 620, 360 }, ImGuiCond_Always);
		const std::string title = IO::formatString("%s %s%s", result.ok ? ICON_FA_CHECK_CIRCLE : ICON_FA_EXCLAMATION_TRIANGLE,
			getString(result.ok ? "publish_done" : "publish_failed"), RESULT_POPUP);
		if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::BeginChild("##publish_log", { -1, -ImGui::GetFrameHeightWithSpacing() }, ImGuiChildFlags_Borders);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(result.log.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110);
			if (ImGui::Button("OK", { 100, 0 }))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
}
