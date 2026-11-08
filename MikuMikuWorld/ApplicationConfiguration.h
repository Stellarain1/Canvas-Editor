#pragma once
#include "UI.h"
#include "MathUtils.h"
#include "InputBinding.h"
#include <json.hpp>

namespace MikuMikuWorld
{
	constexpr const char* APP_CONFIG_FILENAME = "app_config.json";

	struct InputConfiguration
	{
		MultiInputBinding togglePlayback = { "toggle_playback", {ImGuiKey_Space, ImGuiMod_None} };
		MultiInputBinding stop = { "stop", {ImGuiKey_Backspace} };
		MultiInputBinding decreaseNoteSize = { "decrease_note_size", {} };
		MultiInputBinding increaseNoteSize = { "increase_note_size", {} };
		MultiInputBinding shrinkDown = { "shrink_down", {} };
		MultiInputBinding shrinkUp = { "shrink_up", {} };
		MultiInputBinding connectHolds = { "connect_holds", {} };
		MultiInputBinding splitHold = { "split_hold", {} };
		MultiInputBinding openHelp = { "help", {ImGuiKey_F1} };
		MultiInputBinding detectIssues = { "detect_issues", {ImGuiKey_F7} };
		MultiInputBinding autoFixIssues = { "auto_fix_issues", {ImGuiKey_F7, ImGuiMod_Shift} };
		MultiInputBinding openSettings = { "settings", {ImGuiKey_Comma, ImGuiMod_Ctrl} };
		MultiInputBinding deleteSelection = { "delete", {ImGuiKey_Delete} };
		MultiInputBinding copySelection = { "copy", {ImGuiKey_C, ImGuiMod_Ctrl} };
		MultiInputBinding cutSelection = { "cut", {ImGuiKey_X, ImGuiMod_Ctrl} };
		MultiInputBinding paste = { "paste", {ImGuiKey_V, ImGuiMod_Ctrl} };
		MultiInputBinding flipPaste = { "flip_paste", {ImGuiKey_V, ImGuiMod_Ctrl | ImGuiMod_Shift} };
		MultiInputBinding flip = { "flip", {ImGuiKey_F, ImGuiMod_Ctrl} };
		MultiInputBinding cancelPaste = { "cancel_paste", {ImGuiKey_Escape} };
		MultiInputBinding previousTick = { "previous_tick", {ImGuiKey_DownArrow} };
		MultiInputBinding nextTick = { "next_tick", {ImGuiKey_UpArrow} };
		MultiInputBinding create = { "new_chart", {ImGuiKey_N, ImGuiMod_Ctrl} };
		MultiInputBinding open = { "open", {ImGuiKey_O, ImGuiMod_Ctrl} };
		MultiInputBinding save = { "save", {ImGuiKey_S, ImGuiMod_Ctrl} };
		MultiInputBinding saveAs = { "save_as", {ImGuiKey_S, ImGuiMod_Ctrl | ImGuiMod_Shift} };
		MultiInputBinding exportScore = { "export", {ImGuiKey_E, ImGuiMod_Ctrl} };
		MultiInputBinding selectAll = { "select_all", {ImGuiKey_A, ImGuiMod_Ctrl} };
		MultiInputBinding undo = { "undo", {ImGuiKey_Z, ImGuiMod_Ctrl} };
		MultiInputBinding redo = { "redo", {ImGuiKey_Y, ImGuiMod_Ctrl} };
		MultiInputBinding zoomOut = { "zoom_out", {} };
		MultiInputBinding zoomIn = { "zoom_in", {} };

		MultiInputBinding timelineSelect = { "timeline_select", {ImGuiKey_1}, {ImGuiKey_Keypad1} };
		MultiInputBinding timelineTap = { "timeline_tap", {ImGuiKey_2}, {ImGuiKey_Keypad2} };
		MultiInputBinding timelineHold = { "timeline_hold", {ImGuiKey_3}, {ImGuiKey_Keypad3} };
		MultiInputBinding timelineHoldMid = { "timeline_hold_step", {ImGuiKey_4}, {ImGuiKey_Keypad4} };
		MultiInputBinding timelineFlick = { "timeline_flick", {ImGuiKey_5}, {ImGuiKey_Keypad5} };
		MultiInputBinding timelineCritical = { "timeline_critical", {ImGuiKey_6}, {ImGuiKey_Keypad6} };
		MultiInputBinding timelineFriction = { "timeline_trace", {ImGuiKey_7}, {ImGuiKey_Keypad7} };
		MultiInputBinding timelineGuide = { "timeline_guide", {} };
		MultiInputBinding timelineBpm = { "timeline_bpm", {ImGuiKey_8}, {ImGuiKey_Keypad8} };
		MultiInputBinding timelineTimeSignature = { "timeline_time_signature", {ImGuiKey_9}, {ImGuiKey_Keypad9} };
		MultiInputBinding timelineHiSpeed = { "timeline_hi_speed", {ImGuiKey_0}, {ImGuiKey_Keypad0} };
		MultiInputBinding timelineSEVolume = { "timeline_se_volume", {} };
		
		MultiInputBinding insertSkill = { "insert_skill", {ImGuiKey_1, ImGuiMod_Alt} };
		MultiInputBinding insertFever = { "insert_fever", {} };

		MultiInputBinding togglePreviewFullWindow = { "fullscreen_preview", {ImGuiKey_F5} };
		MultiInputBinding toggleFullscreen = { "fullscreen", {ImGuiKey_F11} };
	};

	constexpr size_t maxRecentFilesEntries = 10;

	struct ApplicationConfiguration
	{
		std::string version;

		// session state
		Vector2 windowPos;
		Vector2 windowSize;
		bool maximized;
		bool fullScreen;
		bool vsync;
		int fpsLimit;				// with VSync off: frames per second at most (0 = as fast as it goes)
		bool showFPS;
		int accentColor;
		Color userColor;
		BaseTheme baseTheme;
		std::string language;
		int division;
		float zoom;

		// settings
		int timelineWidth;
		int notesHeight;
		bool matchTimelineSizeToScreen;
		bool matchNotesSizeToTimeline;
		float laneOpacity;
		float backgroundBrightness;
		bool drawBackground;
		std::string backgroundImage;
		bool useSmoothScrolling;
		float smoothScrollingTime;
		float cursorPositionThreshold;
		float scrollSpeedNormal;
		float scrollSpeedShift;
		bool returnToLastSelectedTickOnPause;
		bool followCursorInPlayback;
		bool drawWaveform;
		bool hideStepOutlinesInPlayback;
		bool stopAtEnd;
		bool autoSaveEnabled;
		int autoSaveInterval;
		int autoSaveMaxCount;
		float masterVolume;
		float bgmVolume;
		float seVolume;
		int seProfileIndex;
		bool debugEnabled;
		bool pvMirrorScore;
		bool pvFlickAnimation;
		bool pvSimultaneousLine;
		bool pvHoldAnimation;
		float pvNoteSpeed;
		float pvHoldAlpha;
		float pvGuideAlpha;
		float pvStageCover;
		float pvStageOpacity;
		float pvBackgroundBrightness;
		bool pvDrawMovie;			// play the song 2DMV behind the preview
		std::string pvMovieFile;	// empty: a movie next to the music file
		float pvMovieOffset;		// ms, added to the movie position
		int pvEffectsProfile;
		int notesSkin;
		bool pvDrawToolbar;
		bool pvDrawScoreHud;
		bool pvDrawLifeHud;
		bool pvDrawCombo;
		bool pvDrawPerfect;

		// ---- Canvas Editor additions ----
		// layers
		bool dimOtherLayers;

		// startup intro (text: see Canvas/CanvasBranding.h; introTextOverride wins when not empty).
		// 1.3.0: the intro no longer plays at startup, the "What are you Charting today?" picker does
		bool showIntro;
		int chartMode;				// Canvas::Sekai::ChartMode: 0 Extended Score, 1 Normal MMW, 2 Dynamic Stages
		bool askChartMode;			// show the picker at startup (when no chart was opened with the app)
		std::string introTextOverride;

		// SEKAI Mode (look and sounds ported from SEKAI-Stories)
		bool sekaiMode;
		std::string sekaiAssetsDir;
		bool sekaiSounds;
		bool sekaiBackgrounds;
		float sekaiSoundVolume;

		// chart checks / Issue List
		bool issuesAutoScan;
		bool issuesShowErrors;
		bool issuesShowWarnings;
		bool issuesShowInfos;
		bool issuesShowDetails;
		int issuesPositionFormat;		// 0 "M001 3/8", 1 "1:2.5" (measure:beat), 2 tick, 3 time
		bool issuesClickFocusesTimeline;	// single click also brings the timeline to front

		// chart overview strip beside the timeline
		bool overviewEnabled;
		bool overviewOnLeft;
		int overviewWidth;
		bool overviewShowNotes;
		bool overviewShowIssues;
		bool overviewShowMarkers;

		// interface
		float uiScale;				// fonts and spacing, 0.75 - 2
		int uiDensity;				// 0 compact, 1 normal, 2 roomy
		std::string customTheme;	// name of a theme in <user data>/themes, "" = built-in theme
		nlohmann::json panels;		// { "layers": false, ... } panels the user closed

		// export
		bool susMergeLayers;
		bool mmwsExtended;			// plain Save writes MMWS v7 (the original MikuMikuWorld cannot open those)
		std::string ffmpegPath;
		int videoWidth;
		int videoHeight;
		int videoFps;
		bool videoHitSounds;

		// Pluggable export targets and the optional profile link are stored as raw JSON
		// and parsed by Canvas/Core/ExportTargets and Canvas/Core/ProfileLink.
		nlohmann::json exportTargets;
		nlohmann::json profileLink;

		// ---- 1.2.1 ----
		// interface
		int interfaceStyle;			// 0 SEKAI (Project SEKAI look, the default), 1 Classic (MikuMikuWorld)
		std::string appIconPath;	// custom app / window icon (png, jpg); empty = Canvas' own
		bool neoAnimatedBackdrop;	// the drawn timeline backdrop moves

		// Score Preview extras
		bool pvDrawSkills;			// skill activations where the chart has skill markers
		bool pvDrawFever;			// FEVER chance / FEVER where the chart has them
		bool pvSuperFever;			// FEVER is SUPER FEVER!! (gauge to the top) or FEVER!
		int pvRenderScale;			// preview quality: 0 = Auto, else % of the window's pixels the 3D preview is drawn at
		bool pvUi280;				// Score Preview HUD: the 2.8.0 (pre-3.0) UI
		bool pvDrawIntro;			// Score Preview: the start animation (jacket, title, credits) before the chart
		std::string customSoundSet;	// folder in <user data>/custom/sound_skins for the "Custom" note sounds
		bool pvDrawAllPerfect;		// ALL PERFECT at the end
		bool pvDrawScoreDelta;		// the "+800" popup beside the score
		bool pvDrawCutins;			// card cut-ins (and their voices) when skills go off
		bool pvApCombo;				// rainbow ALL PERFECT combo instead of the normal one
		double chartingSeconds;		// "Total" on the charting timer, kept across launches
		bool pvEffectSounds;		// skill / FEVER / ALL PERFECT sounds
		float pvEffectVolume;
		int pvMvLines;				// stripes over the 2DMV: 0 off, 1 fine, 2 wide (pjsekai-overlay-APPEND)
		float pvMvLineOpacity;
		int pvLiveMode;				// 0 solo, 1 co-op (Multi Live), 2 Cheerful Carnival
		std::string pvScoreSkin;	// folder name in <user data>/score_skins, "" = built-in
		std::string overlayAssetsDir;	// a pjsekai-overlay-APPEND checkout for official-looking extras

		// video export extras
		bool videoDrawUi;			// score, combo, life, judgements, skills... in the video
		int videoLiveMode;			// like pvLiveMode
		bool videoEffectSounds;
		float videoMusicVolume;
		float videoSeVolume;

		// Edit > Auto Fix Chart Issues: which kinds to fix (bit 0 overlaps, 1 holds, 2 hi-speed,
		// 3 skills & FEVER, 4 timing / structure)
		int autoFixMask;
		bool autoFixAggressiveHiSpeed;

		InputConfiguration input;

		std::vector<std::string> recentFiles;

		ApplicationConfiguration();

		void read(const std::string& filename);
		void write(const std::string& filename);
		void restoreDefault();
	};

	extern ApplicationConfiguration config;

	static MultiInputBinding* bindings[] =
	{
		&config.input.create,
		&config.input.open,
		&config.input.save,
		&config.input.saveAs,
		&config.input.exportScore,
		&config.input.undo,
		&config.input.redo,

		&config.input.cutSelection,
		&config.input.copySelection,
		&config.input.paste,
		&config.input.flipPaste,
		&config.input.flip,
		&config.input.cancelPaste,
		&config.input.deleteSelection,
		&config.input.shrinkDown,
		&config.input.shrinkUp,
		&config.input.connectHolds,
		&config.input.splitHold,
		&config.input.selectAll,

		&config.input.togglePlayback,
		&config.input.stop,
		&config.input.previousTick,
		&config.input.nextTick,
		&config.input.decreaseNoteSize,
		&config.input.increaseNoteSize,
		&config.input.zoomOut,
		&config.input.zoomIn,

		&config.input.timelineSelect,
		&config.input.timelineTap,
		&config.input.timelineHold,
		&config.input.timelineHoldMid,
		&config.input.timelineFlick,
		&config.input.timelineCritical,
		&config.input.timelineFriction,
		&config.input.timelineGuide,
		&config.input.timelineBpm,
		&config.input.timelineTimeSignature,
		&config.input.timelineHiSpeed,
		&config.input.timelineSEVolume,
		
		&config.input.insertSkill,
		&config.input.insertFever,

		&config.input.openHelp,
		&config.input.detectIssues,
		&config.input.autoFixIssues,
		&config.input.openSettings,
		&config.input.togglePreviewFullWindow,
		&config.input.toggleFullscreen
	};

	static_assert(sizeof(InputConfiguration) / sizeof(MultiInputBinding) == (sizeof(bindings) / sizeof(MultiInputBinding*)),
		"Too many or too few items in MikuMikuWorld::bindings");
}
