#include "../../UI.h"
#include "CanvasSettings.h"
#include "../../ImGui/imgui_stdlib.h"
#include "../../ImGui/imgui_internal.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../File.h"
#include "../../IconsFontAwesome5.h"
#include "../CanvasBranding.h"
#include "../Sekai/SekaiAssets.h"
#include "../Preview/LiveOverlay.h"
#include "../Preview/LiveCards.h"
#include "../../Rendering/Texture.h"
#include "../Core/ExportTargets.h"
#include "../Core/ProfileLink.h"
#include "Themes.h"
#include <cmath>
#include <filesystem>
#include <algorithm>

namespace MikuMikuWorld
{
	namespace
	{
		void hintedStringProperty(const char* label, std::string& value, const char* hint)
		{
			UI::propertyLabel(label);
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint(UI::labelID(label), hint, &value);
			ImGui::NextColumn();
		}

		void folderProperty(const char* label, std::string& value)
		{
			if (UI::addFileProperty(label, value) == 2)
			{
				IO::FileDialog dialog{};
				dialog.title = getString(label);
				dialog.parentWindowHandle = Application::windowState.windowHandle;
				if (dialog.selectFolder() == IO::FileDialogResult::OK)
					value = dialog.outputFilename;
			}
		}

		void note(const char* key)
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", getString(key));
			ImGui::PopTextWrapPos();
		}
	}

	namespace
	{
		std::string newThemeName;
		std::string themeStatus;

		void drawInterfaceSettings()
		{
			UI::beginPropertyColumns();

			static const char* interfaceStyles[] = { "interface_style_neo", "interface_style_classic" };
			UI::addSelectProperty(getString("interface_style"), config.interfaceStyle, interfaceStyles, 2);
			UI::addCheckboxProperty(getString("interface_neo_backdrop"), config.neoAnimatedBackdrop);

			// the window / taskbar icon and the logo in the menu bar: yours, or MikuMikuWorld's
			if (UI::addFileProperty(getString("app_icon"), config.appIconPath) == 2)
			{
				IO::FileDialog dialog{};
				dialog.title = getString("app_icon_choose");
				dialog.filters = { IO::imageFilter, IO::allFilter };
				dialog.parentWindowHandle = Application::windowState.windowHandle;
				if (dialog.openFile() == IO::FileDialogResult::OK)
					config.appIconPath = dialog.outputFilename;
			}
			UI::endPropertyColumns();
			if (ImGui::Button(getString("app_icon_reset")))
				config.appIconPath.clear();
			note("app_icon_note");
			ImGui::Separator();

			UI::beginPropertyColumns();

			UI::propertyLabel(getString("ui_scale"));
			ImGui::SetNextItemWidth(-1);
			float scalePercent = config.uiScale * 100.0f;
			if (ImGui::SliderFloat("##ui_scale", &scalePercent, 75.0f, 200.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
				config.uiScale = std::round(scalePercent / 5.0f) * 5.0f / 100.0f;
			ImGui::NextColumn();

			static const char* densityKeys[] = { "ui_density_compact", "ui_density_normal", "ui_density_roomy" };
			UI::propertyLabel(getString("ui_density"));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##ui_density", getString(densityKeys[std::clamp(config.uiDensity, 0, 2)])))
			{
				for (int d = 0; d < 3; ++d)
					if (ImGui::Selectable(getString(densityKeys[d]), config.uiDensity == d))
						config.uiDensity = d;
				ImGui::EndCombo();
			}
			ImGui::NextColumn();

			// custom themes go on top of the base theme (General > Theme) and SEKAI Mode
			UI::propertyLabel(getString("ui_custom_theme"));
			ImGui::SetNextItemWidth(-1);
			const auto themes = Canvas::Themes::list();
			if (ImGui::BeginCombo("##ui_custom_theme", config.customTheme.empty() ? getString("none") : config.customTheme.c_str()))
			{
				if (ImGui::Selectable(getString("none"), config.customTheme.empty()))
					config.customTheme.clear();
				for (const auto& theme : themes)
					if (ImGui::Selectable(theme.c_str(), config.customTheme == theme))
						config.customTheme = theme;
				ImGui::EndCombo();
			}
			ImGui::NextColumn();
			UI::endPropertyColumns();

			if (ImGui::TreeNode(getString("ui_edit_colors")))
			{
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", getString("ui_edit_colors_note"));
				ImGui::PopTextWrapPos();

				ImGuiStyle& style = ImGui::GetStyle();
				ImGui::SetNextItemWidth(160);
				ImGui::SliderFloat(getString("ui_rounding"), &style.FrameRounding, 0.0f, 14.0f, "%.0f");
				if (ImGui::IsItemEdited())
				{
					style.GrabRounding = style.FrameRounding;
					style.WindowRounding = style.PopupRounding = style.ChildRounding = std::min(14.0f, style.FrameRounding + 2);
					style.TabRounding = std::min(8.0f, style.FrameRounding);
				}

				if (ImGui::BeginChild("##theme_colors", { 0, 220 }, ImGuiChildFlags_Borders))
				{
					for (int i = 0; i < ImGuiCol_COUNT; ++i)
					{
						ImGui::PushID(i);
						ImGui::ColorEdit4(ImGui::GetStyleColorName(i), &style.Colors[i].x, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs);
						ImGui::PopID();
					}
				}
				ImGui::EndChild();

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				ImGui::InputTextWithHint("##new_theme", getString("ui_theme_name"), &newThemeName);
				ImGui::SameLine();
				ImGui::BeginDisabled(newThemeName.empty());
				if (ImGui::Button(getString("ui_save_theme")))
				{
					if (Canvas::Themes::save(newThemeName, style))
					{
						config.customTheme = Canvas::sanitizeFileName(newThemeName);
						themeStatus = IO::formatString(getString("ui_theme_saved"), config.customTheme.c_str());
					}
					else
						themeStatus = getString("ui_theme_save_failed");
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(config.customTheme.empty());
				if (ImGui::Button(getString("ui_delete_theme")))
				{
					Canvas::Themes::remove(config.customTheme);
					config.customTheme.clear();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button(getString("ui_themes_folder")))
				{
					std::error_code error;
					std::filesystem::create_directories(std::filesystem::u8path(Canvas::Themes::folder()), error);
					IO::openInShell(Canvas::Themes::folder());
				}
				if (!themeStatus.empty())
					ImGui::TextDisabled("%s", themeStatus.c_str());
				ImGui::TreePop();
			}

			ImGui::Separator();
			UI::beginPropertyColumns();
			UI::addCheckboxProperty(getString("overview_enable"), config.overviewEnabled);
			UI::addCheckboxProperty(getString("overview_on_left"), config.overviewOnLeft);
			UI::addSliderProperty(getString("overview_width"), config.overviewWidth, 20, 160, "%d px");
			UI::addCheckboxProperty(getString("overview_show_notes"), config.overviewShowNotes);
			UI::addCheckboxProperty(getString("overview_show_issues"), config.overviewShowIssues);
			UI::addCheckboxProperty(getString("overview_show_markers"), config.overviewShowMarkers);
			UI::endPropertyColumns();
			note("ui_panels_note");
		}
	}

	namespace
	{
		void drawPreviewEffectSettings()
		{
			UI::beginPropertyColumns();
			UI::addCheckboxProperty(getString("preview_draw_skills"), config.pvDrawSkills);
			UI::addCheckboxProperty(getString("preview_ui_280"), config.pvUi280);
			UI::addCheckboxProperty(getString("preview_intro"), config.pvDrawIntro);
			{
				// preview quality: Auto, or a fixed share of the window's pixels
				static const int scales[] = { 0, 100, 85, 70, 50 };
				const std::string current = config.pvRenderScale == 0 ? getString("preview_quality_auto") : std::to_string(config.pvRenderScale) + "%";
				UI::propertyLabel(getString("preview_quality"));
				ImGui::SetNextItemWidth(-1);
				if (ImGui::BeginCombo("##preview_quality", current.c_str()))
				{
					for (int s : scales)
						if (ImGui::Selectable(s == 0 ? getString("preview_quality_auto") : (std::to_string(s) + "%").c_str(), config.pvRenderScale == s))
							config.pvRenderScale = s;
					ImGui::EndCombo();
				}
				UI::tooltip(getString("preview_quality_tip"));
				ImGui::NextColumn();
			}
			UI::addCheckboxProperty(getString("preview_draw_all_perfect"), config.pvDrawAllPerfect);
			UI::addCheckboxProperty(getString("preview_score_delta"), config.pvDrawScoreDelta);
			UI::addCheckboxProperty(getString("preview_card_cutins"), config.pvDrawCutins);
			UI::addCheckboxProperty(getString("preview_ap_combo"), config.pvApCombo);
			UI::addCheckboxProperty(getString("preview_effect_sounds"), config.pvEffectSounds);
			ImGui::BeginDisabled(!config.pvEffectSounds);
			UI::addPercentSliderProperty(getString("preview_effect_volume"), config.pvEffectVolume);
			ImGui::EndDisabled();

			static const char* mvKeys[] = { "preview_mv_lines_off", "preview_mv_lines_fine", "preview_mv_lines_wide" };
			UI::addSelectProperty(getString("preview_mv_lines"), config.pvMvLines, mvKeys, 3);
			UI::tooltip(getString("preview_mv_lines_tooltip"));
			ImGui::BeginDisabled(config.pvMvLines == 0);
			UI::addSliderProperty(getString("preview_mv_line_opacity"), config.pvMvLineOpacity, 0.0f, 2.0f, "%.2fx");
			ImGui::EndDisabled();

			UI::endPropertyColumns();
		}

		std::string newSkinName, skinStatus;

		void skinElementRow(Canvas::Preview::ScoreSkin& skin, const char* key, const char* labelKey)
		{
			Canvas::Preview::SkinElement e = skin.element(key);
			bool changed = false;
			ImGui::PushID(key);
			changed |= ImGui::Checkbox("##shown", &e.visible);
			UI::tooltip(getString("skin_shown"));
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(getString(labelKey));
			ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f);
			const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 3.0f;
			ImGui::SetNextItemWidth(w);
			changed |= ImGui::DragFloat("##x", &e.x, 1.0f, -2000.0f, 4000.0f, "X %.0f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(w);
			changed |= ImGui::DragFloat("##y", &e.y, 1.0f, -2000.0f, 4000.0f, "Y %.0f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::DragFloat("##s", &e.scale, 0.01f, 0.1f, 5.0f, "x%.2f");
			ImGui::PopID();
			if (changed)
				skin.elements[key] = e;
		}

		void drawScoreSkinSettings()
		{
			auto& skin = Canvas::Preview::activeSkin();
			const auto skins = Canvas::Preview::listSkins();

			UI::beginPropertyColumns();
			UI::propertyLabel(getString("score_skin"));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##score_skin", config.pvScoreSkin.empty() ? getString("score_skin_builtin") : config.pvScoreSkin.c_str()))
			{
				if (ImGui::Selectable(getString("score_skin_builtin"), config.pvScoreSkin.empty()))
					config.pvScoreSkin.clear();
				for (const auto& name : skins)
					if (ImGui::Selectable(name.c_str(), config.pvScoreSkin == name))
						config.pvScoreSkin = name;
				ImGui::EndCombo();
			}
			ImGui::NextColumn();
			folderProperty(getString("overlay_assets"), config.overlayAssetsDir);
			UI::endPropertyColumns();
			note("overlay_assets_note");

			if (ImGui::Button(getString("score_skin_open_folder")))
			{
				std::error_code error;
				std::filesystem::create_directories(std::filesystem::u8path(Canvas::Preview::skinsFolder()), error);
				IO::openInShell(Canvas::Preview::skinsFolder());
			}
			ImGui::SameLine();
			if (ImGui::Button(getString("score_skin_reload")))
				Canvas::Preview::reloadSkins();

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ImGui::InputTextWithHint("##new_skin", getString("score_skin_name"), &newSkinName);
			ImGui::SameLine();
			ImGui::BeginDisabled(newSkinName.empty());
			if (ImGui::Button(getString("score_skin_new")))
			{
				std::string error;
				if (Canvas::Preview::createSkinFromCurrent(newSkinName, error))
				{
					// the new folder is the one that was not there before
					for (const auto& name : Canvas::Preview::listSkins())
						if (std::find(skins.begin(), skins.end(), name) == skins.end())
							config.pvScoreSkin = name;
					newSkinName.clear();
					skinStatus.clear();
				}
				else
					skinStatus = error;
			}
			ImGui::EndDisabled();
			if (!skinStatus.empty())
				ImGui::TextDisabled("%s", skinStatus.c_str());
			note("score_skin_note");

			if (ImGui::TreeNode(getString("score_skin_edit")))
			{
				if (skin.folder.empty())
					note("score_skin_edit_builtin");
				else
				{
					note("score_skin_elements_note");
					static const struct { const char* key; const char* label; } elements[] = {
						{ "score", "skin_el_score" }, { "life", "skin_el_life" }, { "combo", "skin_el_combo" }, { "judge", "skin_el_judge" },
						{ "skill", "skin_el_skill" }, { "fever", "skin_el_fever" }, { "all_perfect", "skin_el_all_perfect" },
						{ "score_delta", "skin_el_score_delta" },
					};
					for (const auto& element : elements)
						skinElementRow(skin, element.key, element.label);
					if (ImGui::Button(getString("score_skin_save")))
						skinStatus = skin.save() ? IO::formatString(getString("score_skin_saved"), skin.folder.c_str()) : getString("score_skin_save_failed");
				}
				ImGui::TreePop();
			}
		}
	}

	namespace
	{
		Canvas::Preview::CardImport cardImport;
		std::string cardLinks[Canvas::Preview::DECK_SIZE];
		bool cardTrained[Canvas::Preview::DECK_SIZE] = { true, true, true, true, true };
		std::string cardStatus[Canvas::Preview::DECK_SIZE];
		std::string cardNames[Canvas::Preview::DECK_SIZE];

		bool pickFile(const char* titleKey, const std::vector<IO::FileDialogFilter>& filters, std::string& file)
		{
			IO::FileDialog dialog{};
			dialog.title = getString(titleKey);
			dialog.filters = filters;
			dialog.parentWindowHandle = Application::windowState.windowHandle;
			if (dialog.openFile() != IO::FileDialogResult::OK)
				return false;
			file = dialog.outputFilename;
			return true;
		}

		// one card: its icon, name, skill, files, and "Get" from a sekai.best link
		void cardRow(int slot)
		{
			using namespace Canvas::Preview;
			const LiveCard& card = deckCard(slot);
			ImGui::PushID(slot);
			const float icon = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
			if (const MikuMikuWorld::Texture* t = cardTexture(slot, CardArt::Thumbnail))
				ImGui::Image(static_cast<ImTextureID>(t->getID()), ImVec2(icon, icon));
			else
				ImGui::Dummy(ImVec2(icon, icon));
			ImGui::SameLine();
			ImGui::BeginGroup();

			// the field keeps what you type while it is active; it is saved when you leave it
			if (ImGui::GetActiveID() != ImGui::GetID("##name"))
				cardNames[slot] = card.name;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputTextWithHint("##name", getString("card_name"), &cardNames[slot]);
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				editDeckCard(slot).name = cardNames[slot];
				saveDeck();
			}
			ImGui::SameLine();
			static const char* skills[] = { "card_skill_score", "card_skill_judgment", "card_skill_life" };
			int kind = static_cast<int>(card.skill);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##skill", getString(skills[kind])))
			{
				for (int k = 0; k < 3; ++k)
					if (ImGui::Selectable(getString(skills[k]), k == kind))
					{
						editDeckCard(slot).skill = static_cast<SkillKind>(k);
						saveDeck();
						touchDeck();
					}
				ImGui::EndCombo();
			}

			std::string file, error;
			if (ImGui::Button(getString("card_image")) && pickFile("card_choose_image", { IO::imageFilter, IO::allFilter }, file))
				setCardFile(slot, CardArt::Thumbnail, file, cardStatus[slot]);
			ImGui::SameLine();
			if (ImGui::Button(getString("card_cutin")) && pickFile("card_choose_image", { IO::imageFilter, IO::allFilter }, file))
				setCardFile(slot, CardArt::Cutin, file, cardStatus[slot]);
			ImGui::SameLine();
			if (ImGui::Button(getString("card_voice")) && pickFile("card_choose_voice", { IO::audioFilter, IO::allFilter }, file))
				setCardVoice(slot, file, cardStatus[slot]);
			ImGui::SameLine();
			ImGui::BeginDisabled(!card.custom);
			if (ImGui::Button(getString("card_reset")))
				resetDeckCard(slot);
			ImGui::EndDisabled();
			if (card.voice.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", getString("card_no_voice"));
			}
			ImGui::EndGroup();

			const bool busy = cardImport.busy();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(getString("card_trained")).x - ImGui::CalcTextSize(getString("card_get")).x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x * 4 - ImGui::GetStyle().FramePadding.x * 2);
			ImGui::InputTextWithHint("##link", getString("card_link_hint"), &cardLinks[slot]);
			ImGui::SameLine();
			ImGui::Checkbox(getString("card_trained"), &cardTrained[slot]);
			ImGui::SameLine();
			ImGui::BeginDisabled(busy || cardLinks[slot].empty());
			if (ImGui::Button(getString("card_get")))
				cardImport.start(slot, cardLinks[slot], cardTrained[slot]);
			ImGui::EndDisabled();
			if (cardImport.slot() == slot)
			{
				const std::string text = cardImport.poll();
				if (!text.empty())
					cardStatus[slot] = text;
			}
			if (!cardStatus[slot].empty())
				ImGui::TextDisabled("%s", cardStatus[slot].c_str());
			ImGui::PopID();
		}
	}

	void drawLiveCardSettings()
	{
		note("live_cards_note");
		for (int slot = 0; slot < Canvas::Preview::DECK_SIZE; ++slot)
		{
			cardRow(slot);
			ImGui::Separator();
		}
	}

	void drawPreviewEffectMenuItems()
	{
		ImGui::MenuItem(getString("preview_draw_skills"), NULL, &config.pvDrawSkills);
		ImGui::MenuItem(getString("preview_ui_280"), NULL, &config.pvUi280);
		ImGui::MenuItem(getString("preview_intro"), NULL, &config.pvDrawIntro);
		ImGui::MenuItem(getString("preview_draw_all_perfect"), NULL, &config.pvDrawAllPerfect);
		ImGui::MenuItem(getString("preview_score_delta"), NULL, &config.pvDrawScoreDelta);
		ImGui::MenuItem(getString("preview_card_cutins"), NULL, &config.pvDrawCutins);
		ImGui::MenuItem(getString("preview_effect_sounds"), NULL, &config.pvEffectSounds);

		static const char* mvKeys[] = { "preview_mv_lines_off", "preview_mv_lines_fine", "preview_mv_lines_wide" };
		if (ImGui::BeginMenu(getString("preview_mv_lines")))
		{
			for (int i = 0; i < 3; ++i)
				if (ImGui::MenuItem(getString(mvKeys[i]), NULL, config.pvMvLines == i))
					config.pvMvLines = i;
			ImGui::EndMenu();
		}

	}

	void drawCanvasSettings()
	{
		if (ImGui::CollapsingHeader(getString("interface"), ImGuiTreeNodeFlags_DefaultOpen))
			drawInterfaceSettings();

		if (ImGui::CollapsingHeader(getString("preview_effects"), ImGuiTreeNodeFlags_DefaultOpen))
			drawPreviewEffectSettings();

		if (ImGui::CollapsingHeader(getString("score_skin"), ImGuiTreeNodeFlags_DefaultOpen))
			drawScoreSkinSettings();

		if (ImGui::CollapsingHeader(getString("live_cards"), ImGuiTreeNodeFlags_DefaultOpen))
			drawLiveCardSettings();

		if (ImGui::CollapsingHeader(getString("sekai_mode"), ImGuiTreeNodeFlags_DefaultOpen))
		{
			UI::beginPropertyColumns();
			folderProperty(getString("sekai_assets_dir"), config.sekaiAssetsDir);
			UI::endPropertyColumns();

			auto& assets = Canvas::Sekai::assets();
			assets.refresh(config.sekaiAssetsDir);
			if (config.sekaiAssetsDir.empty())
				note("sekai_assets_not_set");
			else if (!assets.available())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
				ImGui::TextWrapped("%s", getString("sekai_assets_not_found"));
				ImGui::PopStyleColor();
			}
			else
			{
				const bool sounds = !assets.sound("select").empty();
				const bool font = !assets.font("FOT-RodinNTLGPro-DB.otf").empty();
				const size_t backgrounds = assets.backgrounds().size();
				ImGui::Text("%s  %s %s   %s %s   %s %zu", getString("sekai_assets_found"),
					sounds ? ICON_FA_CHECK : ICON_FA_TIMES, getString("sekai_sounds"),
					font ? ICON_FA_CHECK : ICON_FA_TIMES, getString("sekai_font"),
					getString("sekai_backgrounds"), backgrounds);
			}

			UI::beginPropertyColumns();
			UI::addCheckboxProperty(getString("sekai_sounds"), config.sekaiSounds);
			UI::addPercentSliderProperty(getString("sekai_sound_volume"), config.sekaiSoundVolume);
			UI::addCheckboxProperty(getString("sekai_backgrounds"), config.sekaiBackgrounds);
			UI::endPropertyColumns();
			note("sekai_assets_legal");
		}

		if (ImGui::CollapsingHeader(getString("chart_issues"), ImGuiTreeNodeFlags_DefaultOpen))
		{
			UI::beginPropertyColumns();
			UI::addCheckboxProperty(getString("issues_auto_scan_setting"), config.issuesAutoScan);
			UI::endPropertyColumns();
		}

		if (ImGui::CollapsingHeader(getString("export"), ImGuiTreeNodeFlags_DefaultOpen))
		{
			UI::beginPropertyColumns();
			UI::addCheckboxProperty(getString("sus_merge_layers"), config.susMergeLayers);
			UI::addCheckboxProperty(getString("mmws_extended"), config.mmwsExtended);
			UI::endPropertyColumns();
			note("sus_merge_layers_note");
			note("mmws_extended_note");
		}

		if (ImGui::CollapsingHeader(getString("profile_link")))
		{
			Canvas::ProfileLink profile = Canvas::ProfileLink::fromJson(config.profileLink);
			bool changed = false;
			note("profile_link_note");
			UI::beginPropertyColumns();
			UI::propertyLabel(getString("profile_enable"));
			changed |= ImGui::Checkbox("##profile_enable", &profile.enabled);
			ImGui::NextColumn();
			ImGui::BeginDisabled(!profile.enabled);
			UI::propertyLabel(getString("profile_display_name"));
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::InputText("##profile_name", &profile.displayName);
			ImGui::NextColumn();
			UI::propertyLabel(getString("profile_server"));
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::InputTextWithHint("##profile_server", "https://", &profile.server);
			ImGui::NextColumn();
			UI::propertyLabel(getString("profile_keywords"));
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::InputTextWithHint("##profile_keywords", getString("profile_keywords_hint"), &profile.keywords);
			ImGui::NextColumn();
			UI::propertyLabel(getString("profile_token"));
			ImGui::SetNextItemWidth(-1);
			changed |= ImGui::InputText("##profile_token", &profile.token, ImGuiInputTextFlags_Password);
			UI::tooltip(getString("profile_token_tooltip"));
			ImGui::NextColumn();
			UI::propertyLabel(getString("profile_push_target"));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##profile_push_target", profile.pushTarget.empty() ? getString("none") : profile.pushTarget.c_str()))
			{
				if (ImGui::Selectable(getString("none"), profile.pushTarget.empty()))
				{
					profile.pushTarget.clear();
					changed = true;
				}
				for (const auto& target : Canvas::readExportTargets(config.exportTargets))
					if (ImGui::Selectable(target.name.c_str(), profile.pushTarget == target.name))
					{
						profile.pushTarget = target.name;
						changed = true;
					}
				ImGui::EndCombo();
			}
			ImGui::NextColumn();
			ImGui::EndDisabled();
			UI::endPropertyColumns();
			if (changed)
				config.profileLink = profile.toJson();
		}

		if (ImGui::CollapsingHeader(getString("export_targets")))
			note("targets_settings_note");

		if (ImGui::CollapsingHeader(getString("video_export")))
		{
			UI::beginPropertyColumns();
			hintedStringProperty(getString("ffmpeg_path"), config.ffmpegPath, "ffmpeg");
			UI::addIntProperty(getString("video_width"), config.videoWidth, 320, 3840);
			UI::addIntProperty(getString("video_height"), config.videoHeight, 240, 3840);
			UI::addIntProperty(getString("video_fps"), config.videoFps, 15, 120);
			UI::addCheckboxProperty(getString("video_hit_sounds"), config.videoHitSounds);
			UI::addCheckboxProperty(getString("video_draw_ui"), config.videoDrawUi);
			UI::addCheckboxProperty(getString("video_effect_sounds"), config.videoEffectSounds);
			UI::addPercentSliderProperty(getString("video_music_volume"), config.videoMusicVolume);
			UI::addPercentSliderProperty(getString("video_se_volume"), config.videoSeVolume);
			UI::endPropertyColumns();
		}
	}
}
