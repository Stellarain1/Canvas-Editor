#include "Application.h"
#include "ScoreSerializeWindow.h"
#include "SusExporter.h"

#include "Localization.h"
#include "ApplicationConfiguration.h"
#include "Colors.h"
#include "ScoreEditor.h"

namespace MikuMikuWorld
{
	namespace
	{
		constexpr int EXPORT_FORMAT_COUNT = static_cast<int>(Canvas::ExportFormat::FormatCount);

		// one line under each format in the export list: who reads it
		const char* exportHintKey(Canvas::ExportFormat format)
		{
			switch (format)
			{
			case Canvas::ExportFormat::Mmws: return "export_hint_mmws";
			case Canvas::ExportFormat::MmwsExtended: return "export_hint_mmws_extended";
			case Canvas::ExportFormat::CcMmws: return "export_hint_ccmmws";
			case Canvas::ExportFormat::Sus: return "export_hint_sus";
			case Canvas::ExportFormat::SusNoNoteSpeed: return "export_hint_sus_no_speed";
			case Canvas::ExportFormat::MusicScoreJson: return "export_hint_json";
			case Canvas::ExportFormat::MusicScoreJsonBase64: return "export_hint_json_base64";
			case Canvas::ExportFormat::Usc: return "export_hint_usc";
			case Canvas::ExportFormat::LevelDataNextSekai: return "export_hint_leveldata";
			default: return "";
			}
		}

		std::string bulletList(const std::vector<std::string>& lines)
		{
			std::string text;
			for (const auto& line : lines)
				text.append("\n- ").append(line);
			return text;
		}
	}

	DefaultScoreSerializeController::DefaultScoreSerializeController(Score score, std::string suggestedName)
		: suggestedName(std::move(suggestedName))
	{
		this->score = std::move(score);
	}

	DefaultScoreSerializeController::DefaultScoreSerializeController(Score score, const std::string& filename, Canvas::ExportFormat format)
		: format(format)
	{
		this->score = std::move(score);
		this->filename = filename;
	}

	bool DefaultScoreSerializeController::openFileDialog(Canvas::ExportFormat format, const std::string& suggestedName, std::string& filename)
	{
		const std::string extension = Canvas::exportFormatExtension(format);
		IO::FileDialog fileDialog{};
		fileDialog.title = "Export Chart";
		fileDialog.filters = { { Canvas::exportFormatName(format), "*." + extension } };
		fileDialog.defaultExtension = extension;
		fileDialog.inputFilename = suggestedName;
		fileDialog.parentWindowHandle = Application::windowState.windowHandle;

		if (fileDialog.saveFile() == IO::FileDialogResult::OK)
		{
			filename = fileDialog.outputFilename;
			// "chart.json" + ".gz" style double extensions get lost by some dialogs
			if (!IO::endsWith(filename, "." + extension) && format == Canvas::ExportFormat::LevelDataNextSekai)
				filename += IO::endsWith(filename, ".json") ? ".gz" : ".json.gz";
			return true;
		}
		return false;
	}

	SerializeResult DefaultScoreSerializeController::update()
	{
		if (format && !filename.empty())
		{
			try
			{
				Canvas::SaveOptions options;
				options.susMergeLayers = config.susMergeLayers;
				const auto warnings = Canvas::saveChart(score, filename, *format, options);
				format.reset();

				if (!warnings.empty())
				{
					errorMessage = IO::formatString("%s\n%s", getString("export_with_notes"), bulletList(warnings).c_str());
					return SerializeResult::PartialSerializeSuccess;
				}
				return SerializeResult::SerializeSuccess;
			}
			catch (const SusExportError& err)
			{
				errorMessage = err.getDetailedMessage();
				return SerializeResult::Error;
			}
			catch (const std::exception& err)
			{
				errorMessage = IO::formatString("%s\n%s: %s", getString("error_save_score_file"), getString("error"), err.what());
				return SerializeResult::Error;
			}
		}

		SerializeResult result = SerializeResult::None;
		if (!ImGui::IsPopupOpen("###serializer_picker"))
			ImGui::OpenPopup("###serializer_picker");

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints({ 380, 300 }, { FLT_MAX, FLT_MAX });
		ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize / 2.2f, ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		if (ImGui::BeginPopupModal(APP_NAME "###serializer_picker", NULL, ImGuiWindowFlags_NoResize))
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			ImGui::Text("%s", getString("export_as_file_format"));
			ImVec2 avail = ImGui::GetContentRegionAvail();
			float btnWidth = avail.x / 2 - style.ItemSpacing.x * 2, btnHeight = ImGui::GetFrameHeight();
			if (ImGui::BeginListBox("###score_format_list", { avail.x, avail.y - btnHeight - style.ItemSpacing.y * 2 }))
			{
				for (int i = 0; i < EXPORT_FORMAT_COUNT; ++i)
				{
					const auto option = static_cast<Canvas::ExportFormat>(i);
					const bool isSelected = selectedFormat == i;
					if (isSelected)
					{
						const ImVec4 color = UI::accentColors[config.accentColor];
						ImGui::PushStyleColor(ImGuiCol_Header, color);
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, generateDarkColor(color));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, generateHighlightColor(color));
					}

					ImGui::PushID(i);
					const float rowHeight = ImGui::GetTextLineHeightWithSpacing() * 2;
					if (ImGui::Selectable("##format", isSelected, ImGuiSelectableFlags_AllowDoubleClick, { 0, rowHeight }))
					{
						selectedFormat = i;
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && openFileDialog(option, suggestedName, filename))
						{
							format = option;
							ImGui::CloseCurrentPopup();
						}
					}
					const ImVec2 rowMin = ImGui::GetItemRectMin();
					ImGui::GetWindowDrawList()->AddText(rowMin + ImVec2{ style.FramePadding.x, 0 }, ImGui::GetColorU32(ImGuiCol_Text), Canvas::exportFormatName(option));
					ImGui::GetWindowDrawList()->AddText(rowMin + ImVec2{ style.FramePadding.x, ImGui::GetTextLineHeightWithSpacing() },
						ImGui::GetColorU32(ImGuiCol_TextDisabled), getString(exportHintKey(option)));
					ImGui::PopID();

					if (isSelected)
					{
						ImGui::PopStyleColor(3);
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndListBox();
			}

			ImGui::BeginDisabled(selectedFormat < 0);
			if (ImGui::Button(getString("select"), { btnWidth, btnHeight }))
			{
				const auto option = static_cast<Canvas::ExportFormat>(selectedFormat);
				if (openFileDialog(option, suggestedName, filename))
				{
					format = option;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2);
			if (ImGui::Button(getString("cancel"), { btnWidth, btnHeight }))
			{
				ImGui::CloseCurrentPopup();
				result = SerializeResult::Cancel;
			}
			ImGui::EndPopup();
		}
		return result;
	}

	DefaultScoreDeserializeController::DefaultScoreDeserializeController(const std::string& filename)
	{
		this->filename = filename;
	}

	SerializeResult DefaultScoreDeserializeController::update()
	{
		if (filename.empty())
			return SerializeResult::Error;

		try
		{
			Canvas::LoadResult loaded = Canvas::loadChart(filename);
			score = std::move(loaded.score);

			// only a Canvas/MMW .mmws can be saved back in place; everything else is an import
			if (loaded.format == Canvas::ChartFormat::Mmws)
				scoreFilename = filename;

			if (!loaded.warnings.empty())
			{
				errorMessage = IO::formatString("%s\n%s: %s\n%s", getString("import_with_notes"),
					getString("score_file"), IO::File::getFilename(filename).c_str(), bulletList(loaded.warnings).c_str());
				return SerializeResult::PartialDeserializeSuccess;
			}
			return SerializeResult::DeserializeSuccess;
		}
		catch (const std::exception& error)
		{
			errorMessage = IO::formatString(
				"%s\n"
				"%s: %s\n"
				"%s: %s",
				getString("error_load_score_file"),
				getString("score_file"), filename.c_str(),
				getString("error"), error.what());
			return SerializeResult::Error;
		}
	}

	void ScoreSerializeWindow::update(ScoreEditor& editor, ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		if (!controller)
			return;
		switch (controller->update())
		{
		case SerializeResult::PartialSerializeSuccess:
			IO::messageBox(
				APP_NAME,
				controller->getErrorMessage(),
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Warning,
				Application::windowState.windowHandle
			);
			controller.reset();
			break;
		case SerializeResult::SerializeSuccess:
			IO::messageBox(
				APP_NAME,
				IO::formatString(getString("export_successful")),
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Information,
				Application::windowState.windowHandle
			);
			controller.reset();
			break;
		case SerializeResult::PartialDeserializeSuccess:
			IO::messageBox(
				APP_NAME,
				controller->getErrorMessage(),
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Warning,
				Application::windowState.windowHandle
			);
			[[fallthrough]];
		case SerializeResult::DeserializeSuccess:
			context.clearSelection();
			context.history.clear();
			context.score = std::move(controller->getScore());
			++context.revision;
			context.currentLayer = 0;
			context.workingData = EditorScoreData(context.score.metadata, controller->getScoreFilename());

			editor.asyncLoadMusic(context.workingData.musicFilename);
			context.audio.setMusicOffset(0, context.workingData.musicOffset);

			context.scoreStats.calculateStats(context.score);
			context.scorePreviewDrawData.calculateDrawData(context.score);
			timeline.calculateMaxOffsetFromScore(context.score);

			UI::setWindowTitle((context.workingData.filename.size() ? IO::File::getFilename(context.workingData.filename) : windowUntitled));
			context.upToDate = true;
			if (!controller->getFilename().empty())
				editor.updateRecentFilesList(controller->getFilename());
			editor.onChartOpened();

			controller.reset();
			break;
		default:
		case SerializeResult::Error:
			IO::messageBox(
				APP_NAME,
				controller->getErrorMessage(),
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Error,
				Application::windowState.windowHandle
			);
			controller.reset();
			break;
		case SerializeResult::Cancel:
			controller.reset();
			break;
		case SerializeResult::None:
			break;
		}
	}

	void ScoreSerializeWindow::serialize(const ScoreContext& context)
	{
		Score score = context.score;
		score.metadata = context.workingData.toScoreMetadata();
		const std::string suggested = context.workingData.filename.empty() ? context.workingData.title
			: IO::File::getFilenameWithoutExtension(context.workingData.filename);
		controller = std::make_unique<DefaultScoreSerializeController>(std::move(score), suggested);
	}

	void ScoreSerializeWindow::serialize(const ScoreContext& context, const std::string& filename, Canvas::ExportFormat format)
	{
		Score score = context.score;
		score.metadata = context.workingData.toScoreMetadata();
		controller = std::make_unique<DefaultScoreSerializeController>(std::move(score), filename, format);
	}

	void ScoreSerializeWindow::deserialize(const std::string& filename)
	{
		controller = std::make_unique<DefaultScoreDeserializeController>(filename);
	}
}
