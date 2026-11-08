#pragma once
#include "ScoreEditorWindows.h"
#include "ScorePreview.h"
#include "ScoreSerializeWindow.h"
#include "DebugEffectView.h"
#include "Canvas/UI/LayersWindow.h"
#include "Canvas/UI/ChartIssuesWindow.h"
#include "Canvas/UI/AnimationWindow.h"
#include "Canvas/UI/StageWindow.h"
#include "Canvas/UI/SongWindow.h"
#include "Canvas/UI/PublishWindow.h"
#include "Canvas/UI/ProfileWindow.h"
#include "Canvas/UI/AutoFixWindow.h"
#include "Canvas/UI/EditToolsWindow.h"
#include "Canvas/UI/BpmDetectWindow.h"
#include "Canvas/Sekai/SekaiTransition.h"
#include "Canvas/Video/VideoExporter.h"
#include "Canvas/Preview/EffectAudio.h"
#include <random>
#include <map>
#include <future>

namespace MikuMikuWorld
{
	struct AutomationOptions;

	class ScoreEditor
	{
	private:
		ScoreContext context{};
		EditArgs edit{};
		std::unique_ptr<Renderer> renderer;
		PresetManager presetManager;

		ScoreEditorTimeline timeline{};
		ScorePreviewWindow preview{};
		ScorePropertiesWindow propertiesWindow{};
		ScoreOptionsWindow optionsWindow{};
		PresetsWindow presetsWindow{};
		LayersWindow layersWindow{};
		ChartIssuesWindow issuesWindow{};
		AnimationWindow animationWindow{};
		StageWindow stageWindow{};
		bool stagesAutoOpened{ false };		// the Stages panel opened itself for a chart that has stages
		SongWindow songWindow{};
		SongWindow::Host songHost{};
		Canvas::Video::ExportWindow videoExport{};
		PublishWindow publishWindow{};
		ProfileWindow profileWindow{};
		AutoFixWindow autoFixWindow{};
		Canvas::EditToolsWindow editTools{};
		Canvas::BpmDetectWindow bpmDetect{};
		// skill / FEVER / ALL PERFECT sounds during playback
		Canvas::Preview::EffectPlayer effectPlayer;
		std::vector<Canvas::Preview::EffectCue> effectCues;
		std::string effectCuesKey;
		void updateEffectSounds();
		void applyAutoFix(const AutoFixWindow::Output& output);

		PublishWindow::Charts currentChartForPublish();
		DebugWindow debugWindow{};
		SettingsWindow settingsWindow{};
		RecentFileNotFoundDialog recentFileNotFoundDialog{};
		AboutDialog aboutDialog{};
		ScoreSerializeWindow serializeWindow;
		Effect::DebugEffectView debugEffectView{};

		// SEKAI Mode: background behind the timeline and the burst when a chart opens
		std::mt19937 sekaiRng{ std::random_device{}() };
		std::string sekaiBackground;
		std::string appliedBackground;
		Canvas::Sekai::Transition chartTransition;
		bool chartTransitionActive{ false };
		ImVec2 timelineRectMin{}, timelineRectMax{};

		void updateBackground();

		// panels the user can close (X on the tab) and reopen from View > Panels
		std::map<std::string, bool> panelVisibility;
		bool panelSkipped{ false };
		bool panelOpen(const char* id);
		void setPanelOpen(const char* id, bool open);
		bool beginPanel(const char* icon, const char* id);
		void endPanel(const char* id);
		void drawChartTransition();

		Stopwatch autoSaveTimer;
		std::string autoSavePath;
		bool showImGuiDemoWindow{false};

		std::future<void> loadMusicFuture{};
		std::future<void> loadPresetsFuture{};
		std::future<void> importPresetFuture{};

		bool save(std::string filename);

	public:
		ScoreEditor();

		void update();

		void reset();
		void open();
		void loadScore(std::string filename);
		void loadMusic(std::string filename);
		void asyncLoadMusic(std::string filename);
		void exportScore();
		void autoPlaceSkillFever();
		void detectChartIssues();
		// Edit > Auto Fix Chart Issues: the options dialog, then the fixes in the background
		void openAutoFix();
		void clearSkillFever();
		// called after a chart was opened / imported
		void onChartOpened();
		bool saveAs();
		bool trySave(std::string);
		void autoSave();
		int deleteOldAutoSave(int count);
		size_t updateRecentFilesList(const std::string& entry);

		void drawMenubar();
		void drawToolbar();
		void help();

		// --screenshot / --export-video runs: cursor, panels and playback at the right frames
		void applyAutomation(AutomationOptions& options, int frame);

		void loadPresets();
		void openImportPresetDialog();
		void importPreset(const std::string& path);
		void writeSettings();
		void uninitialize();
		
		inline std::string_view getWorkingFilename() const { return context.workingData.filename; }
		inline bool isUpToDate() const { return context.upToDate && !songWindow.hasUnsavedDifficulties(context); }

		inline bool isFullScreenPreview() const { return preview.isFullWindow(); }
		// something moves on its own (playback, a video export): the app must not slow down when idle
		inline bool isBusy() const { return timeline.isPlaying() || videoExport.isRunning(); }
	};
}