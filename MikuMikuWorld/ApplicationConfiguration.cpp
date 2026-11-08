#include "ApplicationConfiguration.h"
#include "IO.h"
#include "JsonIO.h"
#include "Constants.h"
#include <filesystem>
#include <fstream>

using namespace nlohmann;

namespace MikuMikuWorld
{
	ApplicationConfiguration config{};
	constexpr const char* CONFIG_VERSION{ "1.18.0" };

	ApplicationConfiguration::ApplicationConfiguration() : version{ CONFIG_VERSION }
	{
		recentFiles.reserve(maxRecentFilesEntries);
		restoreDefault();
	}

	void ApplicationConfiguration::read(const std::string& filename)
	{
#ifdef _WIN32
		std::wstring wFilename = IO::mbToWideStr(filename);
		if (!std::filesystem::exists(wFilename))
			return;

		std::ifstream configFile{ std::filesystem::path(wFilename) };
#else
		if (!std::filesystem::exists(filename))
			return;

		std::ifstream configFile(filename);
#endif
		json config;
		configFile >> config;
		configFile.close();

		version = jsonIO::tryGetValue<std::string>(config, "version", "1.0");
		language = jsonIO::tryGetValue<std::string>(config, "language", "auto");
		debugEnabled = jsonIO::tryGetValue<bool>(config, "debug", false);

		if (jsonIO::keyExists(config, "window"))
		{
			const json& window = config["window"];
			maximized = jsonIO::tryGetValue<bool>(window, "maximized", false);
			vsync = jsonIO::tryGetValue<bool>(window, "vsync", true);
			fpsLimit = std::clamp(jsonIO::tryGetValue<int>(window, "fps_limit", 0), 0, 1000);
			showFPS = jsonIO::tryGetValue<bool>(window, "show_fps", false);
			fullScreen = jsonIO::tryGetValue<bool>(window, "fullscreen", false);

			windowPos = jsonIO::tryGetValue(window, "position", Vector2{});
			if (windowPos.x <= 0) windowPos.x = 150;
			if (windowPos.y <= 0) windowPos.y = 100;

			windowSize = jsonIO::tryGetValue(window, "size", Vector2{});
			if (windowSize.x <= 0 || windowSize.y <= 0)
			{
				windowSize.x = 1000;
				windowSize.y = 800;
			}
		}

		if (jsonIO::keyExists(config, "timeline"))
		{
			auto& timeline = config["timeline"];
			timelineWidth = std::clamp(jsonIO::tryGetValue<int>(timeline, "lane_width", 26), MIN_LANE_WIDTH, MAX_LANE_WIDTH);
			notesHeight = std::clamp(jsonIO::tryGetValue<int>(timeline, "notes_height", 26), MIN_NOTES_HEIGHT, MAX_NOTES_HEIGHT);
			matchTimelineSizeToScreen = jsonIO::tryGetValue<bool>(timeline, "match_timeline_size_to_window", false);
			matchNotesSizeToTimeline = jsonIO::tryGetValue<bool>(timeline, "match_notes_size_to_timeline", true);

			division = jsonIO::tryGetValue<int>(timeline, "division", 8);
			zoom = jsonIO::tryGetValue<float>(timeline, "zoom", 2.0f);
			laneOpacity = jsonIO::tryGetValue<float>(timeline, "lane_opacity", 0.6f);
			backgroundBrightness = jsonIO::tryGetValue<float>(timeline, "background_brightness", 0.8f);
			drawBackground = jsonIO::tryGetValue<bool>(timeline, "draw_background", true);
			backgroundImage = jsonIO::tryGetValue<std::string>(timeline, "background_image", "");

			useSmoothScrolling = jsonIO::tryGetValue<bool>(timeline, "smooth_scrolling_enable", true);
			smoothScrollingTime = jsonIO::tryGetValue<float>(timeline, "smooth_scrolling_time", 48.0f);
			scrollSpeedNormal = jsonIO::tryGetValue<float>(timeline, "scroll_speed_normal", 2.0f);
			scrollSpeedShift = jsonIO::tryGetValue<float>(timeline, "scroll_speed_fast", 5.0f);

			hideStepOutlinesInPlayback = jsonIO::tryGetValue<bool>(timeline, "hide_step_outlines_in_playback", true);
			drawWaveform = jsonIO::tryGetValue<bool>(timeline, "draw_waveform", true);
			returnToLastSelectedTickOnPause = jsonIO::tryGetValue<bool>(timeline, "return_to_last_tick_on_pause", false);
			cursorPositionThreshold = jsonIO::tryGetValue<float>(timeline, "cursor_position_threshold", 0.5f);
			stopAtEnd = jsonIO::tryGetValue<bool>(timeline, "stop_at_end", true);
		}

		if (jsonIO::keyExists(config, "preview"))
		{
			auto& previewObj = config["preview"];
			notesSkin = jsonIO::tryGetValue<int>(previewObj, "notes_skin", 0);
			pvMirrorScore = jsonIO::tryGetValue<bool>(previewObj, "mirror", false);
			pvFlickAnimation = jsonIO::tryGetValue<bool>(previewObj, "marker_animation", true);
			pvHoldAnimation = jsonIO::tryGetValue<bool>(previewObj, "hold_animation", true);
			pvSimultaneousLine = jsonIO::tryGetValue<bool>(previewObj, "simultaneous_line", true);
			pvNoteSpeed = jsonIO::tryGetValue<float>(previewObj, "note_speed", 6.0f);
			pvHoldAlpha = jsonIO::tryGetValue<float>(previewObj, "hold_alpha", 1.f);
			pvGuideAlpha = jsonIO::tryGetValue<float>(previewObj, "guide_alpha", 0.8f);
			pvStageCover = jsonIO::tryGetValue<float>(previewObj, "stage_cover", 0.f);
			pvStageOpacity = jsonIO::tryGetValue<float>(previewObj, "stage_opacity", 1.f);
			pvBackgroundBrightness = jsonIO::tryGetValue<float>(previewObj, "background_brightness", 1.f);
			pvDrawMovie = jsonIO::tryGetValue<bool>(previewObj, "draw_movie", true);
			pvMovieFile = jsonIO::tryGetValue<std::string>(previewObj, "movie_file", "");
			pvMovieOffset = jsonIO::tryGetValue<float>(previewObj, "movie_offset", 0.f);
			pvEffectsProfile = jsonIO::tryGetValue<int>(previewObj, "effects_profile", 0);
			pvDrawToolbar = jsonIO::tryGetValue<bool>(previewObj, "draw_toolbar", true);
			const bool drawHud = jsonIO::tryGetValue<bool>(previewObj, "draw_hud", true);
			pvDrawScoreHud = jsonIO::tryGetValue<bool>(previewObj, "draw_score_hud", drawHud);
			pvDrawLifeHud = jsonIO::tryGetValue<bool>(previewObj, "draw_life_hud", drawHud);
			pvDrawCombo = jsonIO::tryGetValue<bool>(previewObj, "draw_combo", true);
			pvDrawPerfect = jsonIO::tryGetValue<bool>(previewObj, "draw_perfect", true);
		}

		if (jsonIO::keyExists(config, "theme"))
		{
			accentColor = jsonIO::tryGetValue<int>(config["theme"], "accent_color", 1);
			userColor = jsonIO::tryGetValue(config["theme"], "user_color", Color{});
			baseTheme = (BaseTheme)jsonIO::tryGetValue<int>(config["theme"], "base_theme", 0);
		}

		if (jsonIO::keyExists(config, "save"))
		{
			autoSaveEnabled	= jsonIO::tryGetValue<bool>(config["save"], "auto_save_enabled", true);
			autoSaveInterval = jsonIO::tryGetValue<int>(config["save"], "auto_save_interval", 5);
			autoSaveMaxCount = jsonIO::tryGetValue<int>(config["save"], "auto_save_max_count", 100);
		}

		if (jsonIO::keyExists(config, "audio"))
		{
			seProfileIndex = jsonIO::tryGetValue<int>(config["audio"], "se_profile", 0);
			masterVolume	= std::clamp(jsonIO::tryGetValue<float>(config["audio"], "master_volume", 1.0f), 0.0f, 1.0f);
			bgmVolume		= std::clamp(jsonIO::tryGetValue<float>(config["audio"], "bgm_volume", 1.0f), 0.0f, 1.0f);
			seVolume		= std::clamp(jsonIO::tryGetValue<float>(config["audio"], "se_volume", 1.0f), 0.0f, 1.0f);
		}

		if (jsonIO::keyExists(config, "canvas"))
		{
			const json& canvas = config["canvas"];
			dimOtherLayers = jsonIO::tryGetValue<bool>(canvas, "dim_other_layers", true);
			showIntro = jsonIO::tryGetValue<bool>(canvas, "show_intro", true);
			chartMode = std::clamp(jsonIO::tryGetValue<int>(canvas, "chart_mode", 0), 0, 2);
			askChartMode = jsonIO::tryGetValue<bool>(canvas, "ask_chart_mode", true);
			introTextOverride = jsonIO::tryGetValue<std::string>(canvas, "intro_text", "");
			sekaiMode = jsonIO::tryGetValue<bool>(canvas, "sekai_mode", false);
			sekaiAssetsDir = jsonIO::tryGetValue<std::string>(canvas, "sekai_assets_dir", "");
			sekaiSounds = jsonIO::tryGetValue<bool>(canvas, "sekai_sounds", true);
			sekaiBackgrounds = jsonIO::tryGetValue<bool>(canvas, "sekai_backgrounds", true);
			sekaiSoundVolume = std::clamp(jsonIO::tryGetValue<float>(canvas, "sekai_sound_volume", 0.6f), 0.0f, 1.0f);
			issuesAutoScan = jsonIO::tryGetValue<bool>(canvas, "issues_auto_scan", true);
			issuesShowErrors = jsonIO::tryGetValue<bool>(canvas, "issues_show_errors", true);
			issuesShowWarnings = jsonIO::tryGetValue<bool>(canvas, "issues_show_warnings", true);
			issuesShowInfos = jsonIO::tryGetValue<bool>(canvas, "issues_show_infos", true);
			issuesShowDetails = jsonIO::tryGetValue<bool>(canvas, "issues_show_details", true);
			issuesPositionFormat = std::clamp(jsonIO::tryGetValue<int>(canvas, "issues_position_format", 0), 0, 3);
			issuesClickFocusesTimeline = jsonIO::tryGetValue<bool>(canvas, "issues_click_focuses_timeline", false);
			overviewEnabled = jsonIO::tryGetValue<bool>(canvas, "overview_enabled", true);
			overviewOnLeft = jsonIO::tryGetValue<bool>(canvas, "overview_on_left", false);
			overviewWidth = std::clamp(jsonIO::tryGetValue<int>(canvas, "overview_width", 44), 20, 160);
			overviewShowNotes = jsonIO::tryGetValue<bool>(canvas, "overview_show_notes", true);
			overviewShowIssues = jsonIO::tryGetValue<bool>(canvas, "overview_show_issues", true);
			overviewShowMarkers = jsonIO::tryGetValue<bool>(canvas, "overview_show_markers", true);
			uiScale = std::clamp(jsonIO::tryGetValue<float>(canvas, "ui_scale", 1.0f), 0.75f, 2.0f);
			uiDensity = std::clamp(jsonIO::tryGetValue<int>(canvas, "ui_density", 1), 0, 2);
			customTheme = jsonIO::tryGetValue<std::string>(canvas, "custom_theme", "");
			if (jsonIO::keyExists(canvas, "panels") && canvas["panels"].is_object())
				panels = canvas["panels"];
			susMergeLayers = jsonIO::tryGetValue<bool>(canvas, "sus_merge_layers", true);
			mmwsExtended = jsonIO::tryGetValue<bool>(canvas, "mmws_extended", false);
			ffmpegPath = jsonIO::tryGetValue<std::string>(canvas, "ffmpeg_path", "ffmpeg");
			videoWidth = std::clamp(jsonIO::tryGetValue<int>(canvas, "video_width", 1920), 320, 3840);
			videoHeight = std::clamp(jsonIO::tryGetValue<int>(canvas, "video_height", 1080), 240, 3840);
			videoFps = std::clamp(jsonIO::tryGetValue<int>(canvas, "video_fps", 60), 15, 120);
			videoHitSounds = jsonIO::tryGetValue<bool>(canvas, "video_hit_sounds", true);
			if (jsonIO::keyExists(canvas, "export_targets") && canvas["export_targets"].is_array())
				exportTargets = canvas["export_targets"];
			if (jsonIO::keyExists(canvas, "profile_link") && canvas["profile_link"].is_object())
				profileLink = canvas["profile_link"];
		}

		if (jsonIO::keyExists(config, "canvas_121"))
		{
			const json& c = config["canvas_121"];
			interfaceStyle = std::clamp(jsonIO::tryGetValue<int>(c, "interface_style", 0), 0, 1);
			appIconPath = jsonIO::tryGetValue<std::string>(c, "app_icon", "");
			neoAnimatedBackdrop = jsonIO::tryGetValue<bool>(c, "neo_animated_backdrop", true);
			pvDrawSkills = jsonIO::tryGetValue<bool>(c, "preview_skills", true);
			pvDrawFever = jsonIO::tryGetValue<bool>(c, "preview_fever", true);
			pvSuperFever = jsonIO::tryGetValue<bool>(c, "preview_super_fever", true);
			pvRenderScale = std::clamp(jsonIO::tryGetValue<int>(c, "preview_render_scale", 0), 0, 100);
			pvUi280 = jsonIO::tryGetValue<bool>(c, "preview_ui_280", false);
			pvDrawIntro = jsonIO::tryGetValue<bool>(c, "preview_intro", true);
			customSoundSet = jsonIO::tryGetValue<std::string>(c, "custom_sound_set", "");
			pvDrawAllPerfect = jsonIO::tryGetValue<bool>(c, "preview_all_perfect", true);
			pvDrawScoreDelta = jsonIO::tryGetValue<bool>(c, "preview_score_delta", true);
			pvDrawCutins = jsonIO::tryGetValue<bool>(c, "card_cutins_enabled", false);
			pvApCombo = jsonIO::tryGetValue<bool>(c, "preview_ap_combo", true);
			chartingSeconds = std::max(0.0, jsonIO::tryGetValue<double>(c, "charting_seconds", 0.0));
			pvEffectSounds = jsonIO::tryGetValue<bool>(c, "preview_effect_sounds", true);
			pvEffectVolume = std::clamp(jsonIO::tryGetValue<float>(c, "preview_effect_volume", 0.8f), 0.0f, 1.0f);
			pvMvLines = std::clamp(jsonIO::tryGetValue<int>(c, "preview_mv_lines", 1), 0, 2);
			pvMvLineOpacity = std::clamp(jsonIO::tryGetValue<float>(c, "preview_mv_line_opacity", 1.0f), 0.0f, 2.0f);
			pvLiveMode = std::clamp(jsonIO::tryGetValue<int>(c, "preview_live_mode", 0), 0, 2);
			pvScoreSkin = jsonIO::tryGetValue<std::string>(c, "preview_score_skin", "");
			overlayAssetsDir = jsonIO::tryGetValue<std::string>(c, "overlay_assets_dir", "");
			videoDrawUi = jsonIO::tryGetValue<bool>(c, "video_draw_ui", true);
			videoLiveMode = std::clamp(jsonIO::tryGetValue<int>(c, "video_live_mode", 0), 0, 2);
			videoEffectSounds = jsonIO::tryGetValue<bool>(c, "video_effect_sounds", true);
			videoMusicVolume = std::clamp(jsonIO::tryGetValue<float>(c, "video_music_volume", 1.0f), 0.0f, 2.0f);
			videoSeVolume = std::clamp(jsonIO::tryGetValue<float>(c, "video_se_volume", 1.0f), 0.0f, 2.0f);
			autoFixMask = jsonIO::tryGetValue<int>(c, "auto_fix_mask", 0x1f);
			autoFixAggressiveHiSpeed = jsonIO::tryGetValue<bool>(c, "auto_fix_aggressive_hispeed", false);
		}
		else
		{
			// first start of 1.2.1: the SEKAI interface, white and gray with Miku mint
			// (a colour the user picked stays)
			interfaceStyle = 0;
			if (accentColor == 1)
				accentColor = 7;
			baseTheme = BaseTheme::LIGHT;
		}

		if (jsonIO::keyExists(config, "input") && jsonIO::keyExists(config["input"], "bindings"))
		{
			for (auto& [key, value] : config["input"]["bindings"].items())
			{
				for (int i = 0; i < sizeof(bindings) / sizeof(MultiInputBinding*); ++i)
				{
					if (bindings[i]->name == key)
					{
						bindings[i]->clear();

						int keysCount = std::min(value.size(), bindings[i]->bindings.size());
						for (int k = 0; k < keysCount; ++k)
							bindings[i]->addBinding(FromSerializedString(value[k]));
					}
				}
			}
		}

		if (jsonIO::arrayHasData(config, "recent_files"))
		{
			const json& recentFilesJson = config["recent_files"];
			const size_t count = std::min(recentFilesJson.size(), maxRecentFilesEntries);
			recentFiles.insert(recentFiles.end(), recentFilesJson.begin(), recentFilesJson.begin() + count);
		}
	}

	void ApplicationConfiguration::write(const std::string& filename)
	{
		json config;

		// update to latest version
		config["version"] = CONFIG_VERSION;
		config["language"] = language;
		config["debug"] = debugEnabled;
		config["window"]["position"] = {
			{"x", windowPos.x},
			{"y", windowPos.y}
		};

		config["window"]["size"] = {
			{"x", windowSize.x},
			{"y", windowSize.y}
		};

		config["window"]["maximized"] = maximized;
		config["window"]["vsync"] = vsync;
		config["window"]["fps_limit"] = fpsLimit;
		config["window"]["show_fps"] = showFPS;
		config["window"]["fullscreen"] = fullScreen;

		config["timeline"] = {
			{"lane_width", timelineWidth},
			{"notes_height", notesHeight},
			{"match_timeline_size_to_window", matchTimelineSizeToScreen},
			{"match_notes_size_to_timeline", matchNotesSizeToTimeline},
			{"division", division},
			{"zoom", zoom},
			{"lane_opacity", laneOpacity},
			{"background_brightness", backgroundBrightness},
			{"draw_background", drawBackground},
			{"background_image", backgroundImage},
			{"smooth_scrolling_enable", useSmoothScrolling},
			{"smooth_scrolling_time", smoothScrollingTime},
			{"scroll_speed_normal", scrollSpeedNormal},
			{"scroll_speed_fast", scrollSpeedShift},
			{"hide_step_outlines_in_playback", hideStepOutlinesInPlayback},
			{"draw_waveform", drawWaveform},
			{"return_to_last_tick_on_pause", returnToLastSelectedTickOnPause},
			{"cursor_position_threshold", cursorPositionThreshold},
			{"stop_at_end", stopAtEnd}
		};

		config["preview"] = {
			{"notes_skin", notesSkin},
			{"mirror", pvMirrorScore},
			{"marker_animation", pvFlickAnimation},
			{"hold_animation", pvHoldAnimation},
			{"simultaneous_line", pvSimultaneousLine},
			{"note_speed", pvNoteSpeed},
			{"hold_alpha", pvHoldAlpha},
			{"guide_alpha", pvGuideAlpha},
			{"stage_cover", pvStageCover},
			{"stage_opacity", pvStageOpacity},
			{"background_brightness", pvBackgroundBrightness},
			{"draw_movie", pvDrawMovie},
			{"movie_file", pvMovieFile},
			{"movie_offset", pvMovieOffset},
			{"effects_profile", pvEffectsProfile},
			{"draw_toolbar", pvDrawToolbar},
			{"draw_score_hud", pvDrawScoreHud},
			{"draw_life_hud", pvDrawLifeHud},
			{"draw_combo", pvDrawCombo},
			{"draw_perfect", pvDrawPerfect}
		};

		config["theme"] = {
			{"accent_color", accentColor},
			{"user_color",
				{
					{"r", userColor.r},
					{"g", userColor.g},
					{"b", userColor.b},
					{"a", userColor.a}
				}
			},
			{ "base_theme", static_cast<int>(baseTheme) }
		};

		config["save"] = {
			{"auto_save_enabled", autoSaveEnabled},
			{"auto_save_interval", autoSaveInterval},
			{"auto_save_max_count", autoSaveMaxCount}
		};

		config["audio"] = {
			{"se_profile", seProfileIndex},
			{"master_volume", masterVolume},
			{"bgm_volume", bgmVolume},
			{"se_volume", seVolume}
		};

		config["canvas"] = {
			{"dim_other_layers", dimOtherLayers},
			{"show_intro", showIntro},
			{"chart_mode", chartMode},
			{"ask_chart_mode", askChartMode},
			{"intro_text", introTextOverride},
			{"sekai_mode", sekaiMode},
			{"sekai_assets_dir", sekaiAssetsDir},
			{"sekai_sounds", sekaiSounds},
			{"sekai_backgrounds", sekaiBackgrounds},
			{"sekai_sound_volume", sekaiSoundVolume},
			{"issues_auto_scan", issuesAutoScan},
			{"issues_show_errors", issuesShowErrors},
			{"issues_show_warnings", issuesShowWarnings},
			{"issues_show_infos", issuesShowInfos},
			{"issues_show_details", issuesShowDetails},
			{"issues_position_format", issuesPositionFormat},
			{"issues_click_focuses_timeline", issuesClickFocusesTimeline},
			{"overview_enabled", overviewEnabled},
			{"overview_on_left", overviewOnLeft},
			{"overview_width", overviewWidth},
			{"overview_show_notes", overviewShowNotes},
			{"overview_show_issues", overviewShowIssues},
			{"overview_show_markers", overviewShowMarkers},
			{"ui_scale", uiScale},
			{"ui_density", uiDensity},
			{"custom_theme", customTheme},
			{"panels", panels},
			{"sus_merge_layers", susMergeLayers},
			{"mmws_extended", mmwsExtended},
			{"ffmpeg_path", ffmpegPath},
			{"video_width", videoWidth},
			{"video_height", videoHeight},
			{"video_fps", videoFps},
			{"video_hit_sounds", videoHitSounds},
			{"export_targets", exportTargets},
			{"profile_link", profileLink}
		};

		config["canvas_121"] = {
			{"interface_style", interfaceStyle},
			{"app_icon", appIconPath},
			{"neo_animated_backdrop", neoAnimatedBackdrop},
			{"preview_skills", pvDrawSkills},
			{"preview_fever", pvDrawFever},
			{"preview_super_fever", pvSuperFever},
			{"preview_render_scale", pvRenderScale},
			{"preview_ui_280", pvUi280},
			{"preview_intro", pvDrawIntro},
			{"custom_sound_set", customSoundSet},
			{"preview_all_perfect", pvDrawAllPerfect},
			{"preview_score_delta", pvDrawScoreDelta},
			{"card_cutins_enabled", pvDrawCutins},
			{"preview_ap_combo", pvApCombo},
			{"charting_seconds", chartingSeconds},
			{"preview_effect_sounds", pvEffectSounds},
			{"preview_effect_volume", pvEffectVolume},
			{"preview_mv_lines", pvMvLines},
			{"preview_mv_line_opacity", pvMvLineOpacity},
			{"preview_live_mode", pvLiveMode},
			{"preview_score_skin", pvScoreSkin},
			{"overlay_assets_dir", overlayAssetsDir},
			{"video_draw_ui", videoDrawUi},
			{"video_live_mode", videoLiveMode},
			{"video_effect_sounds", videoEffectSounds},
			{"video_music_volume", videoMusicVolume},
			{"video_se_volume", videoSeVolume},
			{"auto_fix_mask", autoFixMask},
			{"auto_fix_aggressive_hispeed", autoFixAggressiveHiSpeed}
		};

		json keyBindings;
		for (const auto& binding : bindings)
		{
			json keys;
			for (int k = 0; k < binding->getCount(); ++k)
			{
				if (binding->bindings[k].keyCode != ImGuiKey_None)
					keys.push_back(ToSerializedString(binding->bindings[k]));
			}

			keyBindings[binding->name] = keys;
		}

		config["input"] = {
			{"bindings", keyBindings}
		};

		config["recent_files"] = recentFiles;

#ifdef _WIN32
		std::wstring wFilename = IO::mbToWideStr(filename);
		std::ofstream configFile{ std::filesystem::path(wFilename) };
#else
		std::ofstream configFile(filename);
#endif
		configFile << std::setw(4) << config;
		configFile.flush();
		configFile.close();
	}

	void ApplicationConfiguration::restoreDefault()
	{
		windowPos = Vector2(150, 100);
		windowSize = Vector2(1100, 800);
		fullScreen = false;
		maximized = false;
		vsync = true;
		fpsLimit = 0;
		showFPS = false;
		accentColor = 1;
		userColor = Color(0.2f, 0.2f, 0.2f, 1.0f);
		language = "auto";

		timelineWidth = 26;
		notesHeight = 26;
		matchNotesSizeToTimeline = true;
		division = 8;
		zoom = 2.0f;
		laneOpacity = 0.6f;
		backgroundBrightness = 0.8f;
		drawBackground = true;
		backgroundImage = "";
		useSmoothScrolling = true;
		smoothScrollingTime = 48.0f;
		scrollSpeedNormal = 2.0f;
		scrollSpeedShift = 5.0f;
		cursorPositionThreshold = 0.5;
		drawWaveform = true;
		followCursorInPlayback = true;
		returnToLastSelectedTickOnPause = false;
		hideStepOutlinesInPlayback = true;
		stopAtEnd = true;

		pvMirrorScore = false;
		pvFlickAnimation = true;
		pvSimultaneousLine = true;
		pvHoldAnimation = true;
		pvNoteSpeed = 6.0f;
		pvHoldAlpha = 1.f;
		pvGuideAlpha = 0.8f;
		pvStageCover = 0.f;
		pvStageOpacity = 1.f;
		pvBackgroundBrightness = 1.f;
		pvDrawMovie = true;
		pvMovieFile.clear();
		pvMovieOffset = 0.f;
		pvEffectsProfile = 0;
		notesSkin = 0;
		pvDrawToolbar = true;
		pvDrawScoreHud = true;
		pvDrawLifeHud = true;
		pvDrawCombo = true;
		pvDrawPerfect = true;

		autoSaveEnabled = true;
		autoSaveInterval = 5;
		autoSaveMaxCount = 100;

		seProfileIndex = 0;
		masterVolume = 1.0f;
		bgmVolume = 1.0f;
		seVolume = 1.0f;

		debugEnabled = false;

		dimOtherLayers = true;
		showIntro = true;
		chartMode = 0;
		askChartMode = true;
		introTextOverride = "";
		sekaiMode = false;
		sekaiAssetsDir = "";
		sekaiSounds = true;
		sekaiBackgrounds = true;
		sekaiSoundVolume = 0.6f;
		issuesAutoScan = true;
		issuesShowErrors = true;
		issuesShowWarnings = true;
		issuesShowInfos = true;
		issuesShowDetails = true;
		issuesPositionFormat = 0;
		issuesClickFocusesTimeline = false;
		overviewEnabled = true;
		overviewOnLeft = false;
		overviewWidth = 44;
		overviewShowNotes = true;
		overviewShowIssues = true;
		overviewShowMarkers = true;
		uiScale = 1.0f;
		uiDensity = 1;
		customTheme = "";
		panels = json::object();
		susMergeLayers = true;
		mmwsExtended = false;
		ffmpegPath = "ffmpeg";
		videoWidth = 1920;
		videoHeight = 1080;
		videoFps = 60;
		videoHitSounds = true;
		exportTargets = json::array();	// no export target is configured out of the box
		profileLink = json::object();	// profile linking is off unless the user sets it up

		interfaceStyle = 0;
		accentColor = 7;	// Miku mint (the SEKAI interface default)
		baseTheme = BaseTheme::LIGHT;
		appIconPath = "";
		neoAnimatedBackdrop = true;
		pvDrawSkills = true;
		pvDrawFever = true;
		pvSuperFever = true;
		pvRenderScale = 0;
		pvUi280 = false;
		pvDrawIntro = true;
		customSoundSet = "";
		pvDrawAllPerfect = true;
		pvDrawScoreDelta = true;
		pvDrawCutins = false;
		pvApCombo = true;
		chartingSeconds = 0.0;
		pvEffectSounds = true;
		pvEffectVolume = 0.8f;
		pvMvLines = 1;
		pvMvLineOpacity = 1.0f;
		pvLiveMode = 0;
		pvScoreSkin = "";
		overlayAssetsDir = "";
		videoDrawUi = true;
		videoLiveMode = 0;
		videoEffectSounds = true;
		videoMusicVolume = 1.0f;
		videoSeVolume = 1.0f;
		autoFixMask = 0x1f;
		autoFixAggressiveHiSpeed = false;
	}
}
