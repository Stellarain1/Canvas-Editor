#include "Canvas/UI/CanvasSettings.h"
#include "Canvas/Core/Profiler.h"
#include "Canvas/Sekai/ModePicker.h"
#include "Application.h"
#include "ApplicationConfiguration.h"
#include "File.h"
#include "UI.h"
#include "Constants.h"
#include "Utilities.h"
#include "ImageCrop.h"
#include "NativeScoreSerializer.h"
#include "ScoreSerializeWindow.h"
#include "Canvas/Core/ChartIO.h"
#include "Canvas/Core/SkillFever.h"
#include "Canvas/Core/SongProject.h"
#include "Canvas/Core/StageEdit.h"
#include "Canvas/Sekai/SekaiAssets.h"
#include "Canvas/Sekai/SekaiMode.h"
#include "Canvas/Sekai/UiSounds.h"
#include "Canvas/UI/SekaiStyle.h"
#include "Canvas/Preview/LiveCards.h"
#include "Canvas/Preview/IntroOverlay.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace MikuMikuWorld
{
	static MultiInputBinding* timelineModeBindings[] =
	{
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
		&config.input.timelineSEVolume
	};

	ScoreEditor::ScoreEditor() : presetManager(Application::getUserDataDir() + "library")
	{
		renderer = std::make_unique<Renderer>();

		context.audio.initializeAudioEngine();
		context.audio.setMasterVolume(config.masterVolume);
		context.audio.setMusicVolume(config.bgmVolume);
		context.audio.setSoundEffectsVolume(config.seVolume);
		context.audio.loadSoundEffects();
		context.audio.setSoundEffectsProfileIndex(config.seProfileIndex);
		Canvas::Sekai::uiSounds().initialize(context.audio.getEngine(), Application::getUserDataDir() + "sounds");

		timeline.setDivision(config.division);
		timeline.setZoom(config.zoom);

		autoSavePath = Application::getUserDataDir() + "auto_save";
		autoSaveTimer.reset();

		preview.loadNoteEffects(context.scorePreviewDrawData.effectView);

		songHost.chartSwapped = [this]()
		{
			timeline.setPlaying(context, false);
			context.audio.setMusicOffset(0, context.workingData.musicOffset);
			context.scoreStats.calculateStats(context.score);
			context.scorePreviewDrawData.calculateDrawData(context.score);
			timeline.calculateMaxOffsetFromScore(context.score);
			issuesWindow.requestScan();
			UI::setWindowTitle(IO::File::getFilename(songWindow.getSongPath()) + " - " + songWindow.currentDifficulty() + (context.upToDate ? "" : "*"));
		};
		songHost.loadMusic = [this](const std::string& file)
		{
			if (!file.empty())
				asyncLoadMusic(file);
		};
		timeline.onRepeatHoldMids = [this]() { editTools.openRepeatMids(context, timeline.getDivision()); };
		timeline.onInterpolateHiSpeeds = [this]() { editTools.openInterpolate(context, timeline.selectedHiSpeeds()); };
		issuesWindow.songIssuesProvider = [this]() { return songWindow.songIssues(context); };
		issuesWindow.onSelectDifficulty = [this](const std::string& difficulty) { songWindow.switchTo(difficulty, context, songHost); };
	}

	void ScoreEditor::writeSettings()
	{
		config.masterVolume = context.audio.getMasterVolume();
		config.bgmVolume = context.audio.getMusicVolume();
		config.seVolume = context.audio.getSoundEffectsVolume();

		config.division = timeline.getDivision();
		config.zoom = timeline.getZoom();
	}

	bool ScoreEditor::panelOpen(const char* id)
	{
		auto it = panelVisibility.find(id);
		if (it == panelVisibility.end())
		{
			const bool closedByDefault = std::string(id) == "preview_animation" || std::string(id) == "stages";
			const bool stored = config.panels.is_object() && config.panels.contains(id) && config.panels[id].is_boolean();
			const bool open = stored ? config.panels[id].get<bool>() : !closedByDefault;
			it = panelVisibility.emplace(id, open).first;
		}
		return it->second;
	}

	void ScoreEditor::setPanelOpen(const char* id, bool open)
	{
		panelOpen(id);
		panelVisibility[id] = open;
		if (!config.panels.is_object())
			config.panels = nlohmann::json::object();
		if (open && std::string(id) != "preview_animation" && std::string(id) != "stages")
			config.panels.erase(id);
		else
			config.panels[id] = open;
	}

	bool ScoreEditor::beginPanel(const char* icon, const char* id)
	{
		panelSkipped = !panelOpen(id);
		if (panelSkipped)
			return false;
		return ImGui::Begin(IMGUI_TITLE(icon, id), &panelVisibility[id], ImGuiWindowFlags_Static);
	}

	void ScoreEditor::endPanel(const char* id)
	{
		if (panelSkipped)
			return;
		ImGui::End();
		if (!panelVisibility[id])
			setPanelOpen(id, false);
	}

	void ScoreEditor::updateBackground()
	{
		static const std::string defaultBackgroundPath = Application::getAppDir() + "res/editor/default.png";
		std::string wanted = config.backgroundImage.empty() ? defaultBackgroundPath : config.backgroundImage;

		if (config.sekaiMode && config.sekaiBackgrounds && Canvas::Sekai::assets().isStoriesFolder())
		{
			if (sekaiBackground.empty())
				sekaiBackground = Canvas::Sekai::pickBackground(sekaiRng);
			if (!sekaiBackground.empty())
				wanted = sekaiBackground;
		}

		if (settingsWindow.isBackgroundChangePending || wanted != appliedBackground)
		{
			timeline.background.load(wanted);
			appliedBackground = wanted;
			settingsWindow.isBackgroundChangePending = false;
		}
	}

	void ScoreEditor::onChartOpened()
	{
		++context.revision;
		issuesWindow.requestScan();
		if (!config.sekaiMode || timelineRectMax.x <= timelineRectMin.x)
			return;

		Canvas::Sekai::TransitionConfig transitionConfig;
		transitionConfig.maxTriangles = 60;
		transitionConfig.timeScale = 0.35f;
		transitionConfig.flashStrength = 0.35f;
		chartTransition = Canvas::Sekai::Transition(transitionConfig);
		chartTransition.start(timelineRectMin, timelineRectMax);
		chartTransitionActive = true;
	}

	void ScoreEditor::drawChartTransition()
	{
		if (!chartTransitionActive)
			return;

		chartTransition.update(ImGui::GetIO().DeltaTime);
		chartTransition.draw(ImGui::GetForegroundDrawList(ImGui::GetMainViewport()));
		chartTransitionActive = !chartTransition.finished();
	}

	void ScoreEditor::uninitialize()
	{
		Canvas::Sekai::uiSounds().shutdown();
		context.audio.uninitializeAudioEngine();
		timeline.background.dispose();
	}

	void ScoreEditor::update()
	{
		// the start animation's difficulty bar
		{
			const std::string difficulty = songWindow.isOpen() ? songWindow.currentDifficulty() : songWindow.standaloneDifficulty();
			Canvas::Preview::setIntroDifficulty(difficulty, songWindow.isOpen() ? songWindow.levelOf(difficulty) : 0);
		}
		if (!isFullScreenPreview())
		{
			drawMenubar();
			drawToolbar();
		}

		if (!ImGui::GetIO().WantCaptureKeyboard)
		{
			if (ImGui::IsAnyPressed(config.input.create)) Application::windowState.resetting = true;
			if (ImGui::IsAnyPressed(config.input.open))
			{
				Application::windowState.resetting = true;
				Application::windowState.shouldPickScore = true;
			}

			if (ImGui::IsAnyPressed(config.input.openSettings)) settingsWindow.open = true;
			if (ImGui::IsAnyPressed(config.input.openHelp)) help();
			if (ImGui::IsAnyPressed(config.input.autoFixIssues)) openAutoFix();
			else if (ImGui::IsAnyPressed(config.input.detectIssues)) detectChartIssues();
			if (ImGui::IsAnyPressed(config.input.save)) trySave(context.workingData.filename);
			if (ImGui::IsAnyPressed(config.input.saveAs)) saveAs();
			if (ImGui::IsAnyPressed(config.input.exportScore)) exportScore();
			if (ImGui::IsAnyPressed(config.input.togglePlayback)) timeline.setPlaying(context, !timeline.isPlaying());
			if (ImGui::IsAnyPressed(config.input.stop)) timeline.stop(context);
			if (ImGui::IsAnyPressed(config.input.previousTick, true)) timeline.previousTick(context);
			if (ImGui::IsAnyPressed(config.input.nextTick, true)) timeline.nextTick(context);
			if (ImGui::IsAnyPressed(config.input.togglePreviewFullWindow, false)) preview.setFullWindow(!preview.isFullWindow());

			if (!preview.isFullWindow())
			{
				if (ImGui::IsAnyPressed(config.input.selectAll)) context.selectAll();
				if (ImGui::IsAnyPressed(config.input.deleteSelection))
				{
					const bool deletedHiSpeed = timeline.deleteSelectedHiSpeed(context);
					const bool deletedSEVolume = timeline.deleteSelectedSEVolume(context);
					if (!deletedHiSpeed && !deletedSEVolume) context.deleteSelection();
				}
				if (ImGui::IsAnyPressed(config.input.cutSelection) && !timeline.cutSelectedHiSpeed(context) && !timeline.cutSelectedSEVolume(context)) context.cutSelection();
				if (ImGui::IsAnyPressed(config.input.copySelection) && !timeline.copySelectedHiSpeed(context) && !timeline.copySelectedSEVolume(context)) context.copySelection();
				if (ImGui::IsAnyPressed(config.input.paste) && !timeline.pasteHiSpeed(context, false) && !timeline.pasteSEVolume(context, false)) context.paste(false);
				if (ImGui::IsAnyPressed(config.input.flipPaste) && !timeline.pasteHiSpeed(context, true) && !timeline.pasteSEVolume(context, true)) context.paste(true);
				if (ImGui::IsAnyPressed(config.input.cancelPaste)) context.cancelPaste();
				if (ImGui::IsAnyPressed(config.input.flip)) context.flipSelection();
				if (ImGui::IsAnyPressed(config.input.undo)) context.undo();
				if (ImGui::IsAnyPressed(config.input.redo)) context.redo();
				if (ImGui::IsAnyPressed(config.input.zoomOut, true)) timeline.setZoom(timeline.getZoom() - 0.25f);
				if (ImGui::IsAnyPressed(config.input.zoomIn, true)) timeline.setZoom(timeline.getZoom() + 0.25f);
				if (ImGui::IsAnyPressed(config.input.decreaseNoteSize, true)) edit.noteWidth = std::clamp(edit.noteWidth - 1, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
				if (ImGui::IsAnyPressed(config.input.increaseNoteSize, true)) edit.noteWidth = std::clamp(edit.noteWidth + 1, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
				if (ImGui::IsAnyPressed(config.input.shrinkDown)) context.shrinkSelection(Direction::Down);
				if (ImGui::IsAnyPressed(config.input.shrinkUp)) context.shrinkSelection(Direction::Up);
				if (ImGui::IsAnyPressed(config.input.connectHolds)) context.connectHoldsInSelection();
				if (ImGui::IsAnyPressed(config.input.splitHold)) context.splitHoldInSelection();

				for (int i = 0; i < (int)TimelineMode::TimelineModeMax; ++i)
					if (ImGui::IsAnyPressed(*timelineModeBindings[i])) timeline.changeMode((TimelineMode)i, edit);

				if (ImGui::IsAnyPressed(config.input.insertSkill)) timeline.insertSkill(context, context.currentTick);
				if (ImGui::IsAnyPressed(config.input.insertFever)) timeline.beginInsertFever(context, context.currentTick);
			}
		}

		if (config.matchTimelineSizeToScreen)
		{
			int laneWidth = static_cast<int>(timeline.getWidth() * 0.45f / NUM_LANES);
			timeline.laneWidth = std::clamp(laneWidth, MIN_LANE_WIDTH, MAX_LANE_WIDTH);
		}
		else
		{
			timeline.laneWidth = config.timelineWidth;
		}

		timeline.notesHeight = config.matchNotesSizeToTimeline ? std::clamp(static_cast<int>(timeline.laneWidth), MIN_NOTES_HEIGHT, MAX_NOTES_HEIGHT) : config.notesHeight;

		if (config.backgroundBrightness != timeline.background.getBrightness())
			timeline.background.setBrightness(config.backgroundBrightness);

		updateBackground();

		if (config.seProfileIndex != context.audio.getSoundEffectsProfileIndex())
		{
			context.audio.stopSoundEffects(false);
			context.audio.setSoundEffectsProfileIndex(config.seProfileIndex);
		}

		if (settingsWindow.isEffectProfileChangePending)
		{
			preview.loadNoteEffects(context.scorePreviewDrawData.effectView);
			debugEffectView.loadEffects();
			settingsWindow.isEffectProfileChangePending = false;
		}

		if (settingsWindow.isSoundSetReloadPending)
		{
			context.audio.stopSoundEffects(false);
			context.audio.loadSoundProfile(Audio::soundEffectsProfileCount - 1);
			settingsWindow.isSoundSetReloadPending = false;
		}

		if (propertiesWindow.isPendingLoadMusic)
		{
			asyncLoadMusic(propertiesWindow.pendingLoadMusicFilename);
			propertiesWindow.isPendingLoadMusic = false;
			propertiesWindow.pendingLoadMusicFilename.clear();
		}

		if (config.autoSaveEnabled && autoSaveTimer.elapsedMinutes() >= config.autoSaveInterval)
		{
			autoSave();
			autoSaveTimer.reset();
		}

		if (recentFileNotFoundDialog.update() == DialogResult::Yes)
		{
			if (isArrayIndexInBounds(recentFileNotFoundDialog.removeIndex, config.recentFiles))
				config.recentFiles.erase(config.recentFiles.begin() + recentFileNotFoundDialog.removeIndex);
		}

		// Normal MMW saves the original layout every MikuMikuWorld opens; the other modes keep the extras
		NativeScoreSerializer::saveExtendedByDefault = config.chartMode == static_cast<int>(Canvas::Sekai::ChartMode::NormalMmw) ? false : config.mmwsExtended;
		{
			CANVAS_PROFILE("issues.process");
			issuesWindow.process(context);
		}
		updateEffectSounds();

		settingsWindow.update();
		if (Application::aboutRequested)
		{
			Application::aboutRequested = false;
			aboutDialog.open = true;
		}
		aboutDialog.update();
		serializeWindow.update(*this, context, timeline);
		videoExport.update(context, preview, renderer.get(), timeline);
		// a chart that comes with stages (opened / imported) shows the panel once
		if (context.score.stages.enabled && !stagesAutoOpened)
		{
			stagesAutoOpened = true;
			if (!panelOpen("stages"))
				setPanelOpen("stages", true);
		}
		else if (!context.score.stages.enabled)
			stagesAutoOpened = false;
		publishWindow.update();
		if (auto autoFix = autoFixWindow.update(context))
			applyAutoFix(*autoFix);
		editTools.update(context);
		if (bpmDetect.update(context))
		{
			// a new tempo map: same follow-up as any timing edit
			timeline.calculateMaxOffsetFromScore(context.score);
			issuesWindow.requestScan();
		}
		const std::string pulledChart = profileWindow.update();
		if (!pulledChart.empty())
		{
			Application::pendingLoadScoreFile = pulledChart;
			Application::windowState.resetting = true;
		}

		const bool timeline_just_created = (ImGui::FindWindowByName("###notes_timeline") == NULL);
		ImGui::Begin(IMGUI_TITLE(ICON_FA_MUSIC, "notes_timeline"), NULL, ImGuiWindowFlags_Static | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGuiID dockId = ImGui::GetWindowDockID();
		timelineRectMin = ImGui::GetWindowPos();
		timelineRectMax = timelineRectMin + ImGui::GetWindowSize();
		timeline.overviewIssues = issuesWindow.hasResults() ? &issuesWindow.getIssues() : nullptr;
		{
			CANVAS_PROFILE("timeline.update");
			timeline.update(context, edit, renderer.get());
		}
		ImGui::End();

		ImGuiWindowFlags previewWindowFlags =
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
		const bool previewShown = isFullScreenPreview() || panelOpen("score_preview");
		if (isFullScreenPreview())
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			previewWindowFlags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize;

			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });

			ImGui::Begin(IMGUI_TITLE(ICON_FA_OBJECT_GROUP, "score_preview_full"), NULL, previewWindowFlags);
			ImGui::PopStyleVar(2);
		}
		else if (panelOpen("score_preview"))
		{
			ImGui::Begin(IMGUI_TITLE(ICON_FA_OBJECT_GROUP, "score_preview"), &panelVisibility["score_preview"], previewWindowFlags);
		}

		if (previewShown)
		{
			{
				CANVAS_PROFILE("preview.update");
				preview.update(context, renderer.get());
			}
			CANVAS_PROFILE("preview.updateUI");
			preview.updateUI(timeline, context);
			ImGui::End();
			if (!panelOpen("score_preview"))
				setPanelOpen("score_preview", false);
		}

		if (config.debugEnabled)
		{
			debugWindow.update(context, timeline);
			debugEffectView.update(renderer.get());
		}

		ImGuiID sidePanelDockId = 0;
		if (beginPanel(ICON_FA_ALIGN_LEFT, "chart_properties"))
		{
			propertiesWindow.update(context);
		}
		sidePanelDockId = ImGui::GetWindowDockID();
		endPanel("chart_properties");

		ImGuiID bottomPanelDockId = 0;
		if (beginPanel(ICON_FA_WRENCH, "options"))
		{
			optionsWindow.update(context, edit, timeline.getMode());
		}
		bottomPanelDockId = ImGui::GetWindowDockID();
		endPanel("options");

		if (beginPanel(ICON_FA_DRAFTING_COMPASS, "presets"))
		{
			presetsWindow.update(context, presetManager);
		}
		endPanel("presets");

		// Panels Canvas added: with a layout saved by an older version they would appear floating
		// over the timeline, so the first time they show up they join the side panel instead.
		if (bottomPanelDockId || sidePanelDockId)
			ImGui::SetNextWindowDockID(bottomPanelDockId ? bottomPanelDockId : sidePanelDockId, ImGuiCond_FirstUseEver);
		// layers (hi-speed groups) are an extended-score feature: Normal MMW has none
		if (config.chartMode != static_cast<int>(Canvas::Sekai::ChartMode::NormalMmw))
		{
			if (beginPanel(ICON_FA_LAYER_GROUP, "layers"))
			{
				layersWindow.update(context);
			}
			endPanel("layers");
		}

		if (sidePanelDockId)
			ImGui::SetNextWindowDockID(sidePanelDockId, ImGuiCond_FirstUseEver);
		if (beginPanel(ICON_FA_LIST_OL, "song_difficulties"))
		{
			songWindow.update(context, songHost);
		}
		endPanel("song_difficulties");

		if (sidePanelDockId)
			ImGui::SetNextWindowDockID(sidePanelDockId, ImGuiCond_FirstUseEver);
		if (beginPanel(ICON_FA_FILM, "preview_animation"))
		{
			animationWindow.draw(context, timeline);
		}
		endPanel("preview_animation");

		if (sidePanelDockId)
			ImGui::SetNextWindowDockID(sidePanelDockId, ImGuiCond_FirstUseEver);
		if (beginPanel(ICON_FA_LAYER_GROUP, "stages"))
		{
			stageWindow.draw(context, timeline);
		}
		endPanel("stages");

		// the Issue List sits next to the timeline and preview unless the user moves it
		ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
		if (beginPanel(ICON_FA_LIST_UL, "chart_issues"))
		{
			issuesWindow.draw(context, timeline);
		}
		endPanel("chart_issues");

		// after every panel sharing its dock exists, so the timeline is the tab shown on first start
		if (timeline_just_created) ImGui::SetWindowFocus("###notes_timeline");

		drawChartTransition();

#ifdef _DEBUG
		if (showImGuiDemoWindow)
			ImGui::ShowDemoWindow(&showImGuiDemoWindow);
#endif
	}

	size_t ScoreEditor::updateRecentFilesList(const std::string& entry)
	{
		while (config.recentFiles.size() >= maxRecentFilesEntries)
			config.recentFiles.pop_back();

		// Remove the entry (if found) to the beginning of the vector
		auto it = std::find(config.recentFiles.begin(), config.recentFiles.end(), entry);
		if (it != config.recentFiles.end())
			config.recentFiles.erase(it);

		config.recentFiles.insert(config.recentFiles.begin(), entry);
		return config.recentFiles.size();
	}

	void ScoreEditor::reset()
	{
		timeline.setPlaying(context, false);

		songWindow.close();
		context.score = {};
		++context.revision;
		context.currentLayer = 0;
		context.workingData = {};
		context.history.clear();
		context.scoreStats.reset();
		context.scorePreviewDrawData.clear();
		context.audio.disposeMusic();
		context.waveformL.clear();
		context.waveformR.clear();
		context.clearSelection();

		// New score; nothing to save
		context.upToDate = true; 

		UI::setWindowTitle(windowUntitled);
	}

	void ScoreEditor::loadScore(std::string filename)
	{
		if (!IO::File::exists(filename))
			return;

		timeline.setPlaying(context, false);
		if (IO::endsWith(filename, Canvas::SONG_EXTENSION))
		{
			std::string error;
			if (songWindow.openSong(filename, context, songHost, error))
			{
				updateRecentFilesList(filename);
				onChartOpened();
			}
			else
				IO::messageBox(APP_NAME, error, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error, Application::windowState.windowHandle);
			return;
		}

		// a plain chart replaces the song
		songWindow.close();
		serializeWindow.deserialize(filename);
	}

	void ScoreEditor::loadMusic(std::string filename)
	{
		propertiesWindow.isLoadingMusic = true;
		context.waveformL.clear();
		context.waveformR.clear();
		
		Result result = context.audio.loadMusic(filename);
		if (result.isOk() || filename.empty())
		{
			context.workingData.musicFilename = filename;
		}
		else
		{
			std::string errorMessage = IO::formatString("%s\n%s: %s\n%s",
				getString("error_load_music_file"),
				getString("music_file"),
				filename.c_str(),
				result.getMessage().c_str()
			);

			IO::messageBox(APP_NAME, errorMessage, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error, Application::windowState.windowHandle);
		}
		
		context.waveformL.generateMipChainsFromSampleBuffer(context.audio.musicBuffer, 0);
		context.waveformR.generateMipChainsFromSampleBuffer(context.audio.musicBuffer, 1);

		if (timeline.isPlaying())
		{
			bool prevPauseBehaviour = config.returnToLastSelectedTickOnPause;
			config.returnToLastSelectedTickOnPause = false;
			timeline.setPlaying(context, false);
			timeline.setPlaying(context, true);

			config.returnToLastSelectedTickOnPause = prevPauseBehaviour;
		}

		propertiesWindow.isLoadingMusic = false;
	}

	void ScoreEditor::asyncLoadMusic(std::string filename)
	{
		if (loadMusicFuture.valid())
			loadMusicFuture.get();
		
		loadMusicFuture = std::async(&ScoreEditor::loadMusic, this, filename);
	}

	void ScoreEditor::open()
	{
		IO::FileDialog fileDialog{};
		fileDialog.parentWindowHandle = Application::windowState.windowHandle;
		fileDialog.title = "Open Chart";
		fileDialog.filters = {
			{ "All Supported Charts", Canvas::chartFilePatterns() },
			{ "Canvas Song", "*.canvassong" },
			{ "Canvas Editor Chart", "*.ce" },
			{ "MikuMikuWorld Score", "*.mmws;*.ccmmws;*.ucmmws;*.unchmmws" },
			IO::susFilter,
			{ "Project SEKAI MusicScoreData", "*.json" },
			{ "Universal Sekai Chart", "*.usc" },
			{ "Sonolus LevelData", "*.json.gz;*.gz;LevelData" },
			IO::allFilter
		};
		
		if (fileDialog.openFile() == IO::FileDialogResult::OK)
			loadScore(fileDialog.outputFilename);
	}

	bool ScoreEditor::trySave(std::string filename)
	{
		if (songWindow.isOpen())
		{
			std::string error;
			if (songWindow.saveAll(context, error))
				return true;
			IO::messageBox(APP_NAME, IO::formatString("%s\n%s: %s", getString("error_save_score_file"), getString("error"), error.c_str()),
				IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error, Application::windowState.windowHandle);
			return false;
		}
		return filename.empty() ? saveAs() : save(filename);
	}

	bool ScoreEditor::save(std::string filename)
	{
		try
		{
			context.score.metadata = context.workingData.toScoreMetadata();
			NativeScoreSerializer().serialize(context.score, filename);

			UI::setWindowTitle(IO::File::getFilename(filename));
			context.upToDate = true;
		}
		catch (const std::exception& err)
		{
			IO::messageBox(
				APP_NAME,
				IO::formatString("%s\n%s: %s", getString("error_save_score_file"), getString("error"), err.what()),
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Error,
				Application::windowState.windowHandle
			);

			return false;
		}

		return true;
	}

	bool ScoreEditor::saveAs()
	{
		IO::FileDialog fileDialog{};
		fileDialog.title = "Save Chart";
		fileDialog.filters = { IO::ceFilter, IO::mmwsFilter };
		fileDialog.defaultExtension = "ce";
		fileDialog.parentWindowHandle = Application::windowState.windowHandle;
		fileDialog.inputFilename = IO::File::getFilenameWithoutExtension(context.workingData.filename);

		if (fileDialog.saveFile() == IO::FileDialogResult::OK)
		{
			context.workingData.filename = fileDialog.outputFilename;
			bool saved = save(context.workingData.filename);
			if (saved)
				updateRecentFilesList(fileDialog.outputFilename);

			return saved;
		}

		return false;
	}

	void ScoreEditor::exportScore()
	{
		serializeWindow.serialize(context);
	}

	PublishWindow::Charts ScoreEditor::currentChartForPublish()
	{
		Score score = context.score;
		score.metadata = context.workingData.toScoreMetadata();
		const std::string difficulty = songWindow.isOpen() ? songWindow.currentDifficulty() : std::string("chart");
		return { { difficulty, std::move(score) } };
	}

	void ScoreEditor::autoPlaceSkillFever()
	{
		const auto markers = Canvas::SkillFever::generateOfficialStyle(context.score);
		if (markers.empty())
		{
			IO::messageBox(APP_NAME, getString("skill_fever_no_notes"), IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Information, Application::windowState.windowHandle);
			return;
		}

		if (Canvas::SkillFever::hasMarkers(context.score) &&
			IO::messageBox(APP_NAME, getString("skill_fever_replace_confirm"), IO::MessageBoxButtons::YesNo, IO::MessageBoxIcon::Question,
				Application::windowState.windowHandle) != IO::MessageBoxResult::Yes)
			return;

		Score prev = context.score;
		Canvas::SkillFever::applyMarkers(context.score, markers);
		context.pushHistory("Auto-place skills & FEVER", prev, context.score);
	}

	void ScoreEditor::applyAutoFix(const AutoFixWindow::Output& output)
	{
		timeline.setPlaying(context, false);
		Score prev = context.score;
		context.score = output.score;
		context.clearSelection();
		context.pushHistory("Auto fix chart issues", prev, context.score);
		timeline.calculateMaxOffsetFromScore(context.score);
		issuesWindow.requestScan();
	}

	void ScoreEditor::detectChartIssues()
	{
		setPanelOpen("chart_issues", true);
		issuesWindow.requestScan();
		ImGui::SetWindowFocus("###chart_issues");
	}

	void ScoreEditor::openAutoFix()
	{
		autoFixWindow.open(context);
	}

	void ScoreEditor::clearSkillFever()
	{
		if (!Canvas::SkillFever::hasMarkers(context.score))
			return;

		Score prev = context.score;
		Canvas::SkillFever::applyMarkers(context.score, {});
		context.pushHistory("Clear skills & FEVER", prev, context.score);
	}

	void ScoreEditor::drawMenubar()
	{
		ImGui::BeginMainMenuBar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 2));

		// Canvas Neo: the logo (or the user's icon) and the name before the menus
		if (Canvas::SekaiStyle::active())
		{
			const float barHeight = ImGui::GetWindowHeight();
			const float logoSize = std::max(12.0f, barHeight - 6.0f);
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const ImVec2 logoPos(start.x + 2.0f, ImGui::GetWindowPos().y + (barHeight - logoSize) * 0.5f);
			Canvas::SekaiStyle::drawLogo(ImGui::GetWindowDrawList(), logoPos, logoSize, (ImTextureID)(size_t)Application::appIconTexture);
			ImGui::SetCursorScreenPos(ImVec2(logoPos.x + logoSize + 6.0f, start.y));
			if (Canvas::SekaiStyle::headerFont)
				ImGui::PushFont(Canvas::SekaiStyle::headerFont, ImGui::GetFontSize() * 1.05f);
			ImGui::TextUnformatted("Canvas Editor");
			if (Canvas::SekaiStyle::headerFont)
				ImGui::PopFont();
			ImGui::SameLine(0, 14);
		}

		if (ImGui::BeginMenu(getString("file")))
		{
			if (ImGui::MenuItem(getString("new"), ToShortcutString(config.input.create)))
				Application::windowState.resetting = true;

			if (ImGui::MenuItem(getString("open"), ToShortcutString(config.input.open)))
			{
				Application::windowState.resetting = true;
				Application::windowState.shouldPickScore = true;
			}

			if (ImGui::BeginMenu(getString("open_recent")))
			{
				for (size_t index = 0; index < config.recentFiles.size(); index++)
				{
					const std::string& entry = config.recentFiles[index];
					if (ImGui::MenuItem(entry.c_str()))
					{
						if (IO::File::exists(entry))
						{
							Application::windowState.resetting = true;
							Application::pendingLoadScoreFile = entry;
						}
						else
						{
							recentFileNotFoundDialog.removeFilename = entry;
							recentFileNotFoundDialog.removeIndex = index;
							recentFileNotFoundDialog.open = true;
						}
					}
				}

				ImGui::Separator();
				if (ImGui::MenuItem(getString("clear"), nullptr, false, !config.recentFiles.empty()))
					config.recentFiles.clear();

				ImGui::EndMenu();
			}

			ImGui::Separator();
			if (ImGui::MenuItem(getString("save"), ToShortcutString(config.input.save)))
				trySave(context.workingData.filename);

			if (ImGui::MenuItem(getString("save_as"), ToShortcutString(config.input.saveAs)))
				saveAs();

			if (ImGui::MenuItem(getString("export"), ToShortcutString(config.input.exportScore)))
				exportScore();

			if (ImGui::MenuItem(getString("export_video"), nullptr, false, !videoExport.isRunning()))
				videoExport.open(context);

			if (ImGui::BeginMenu(getString("publish_to"), !publishWindow.isBusy()))
			{
				const auto names = publishWindow.targetNames();
				if (names.empty())
					ImGui::MenuItem(getString("targets_none_short"), nullptr, false, false);
				for (const auto& name : names)
				{
					if (songWindow.isOpen())
					{
						if (ImGui::BeginMenu(name.c_str()))
						{
							if (ImGui::MenuItem(getString("publish_this_difficulty")))
								publishWindow.publish(name, currentChartForPublish());
							if (ImGui::MenuItem(getString("publish_all_difficulties")))
								publishWindow.publish(name, songWindow.allCharts(context));
							ImGui::EndMenu();
						}
					}
					else if (ImGui::MenuItem(name.c_str()))
						publishWindow.publish(name, currentChartForPublish());
				}
				ImGui::Separator();
				if (ImGui::MenuItem(IO::concat(getString("export_targets"), "...", "").c_str()))
					publishWindow.openManager();
				ImGui::EndMenu();
			}

			const Canvas::ProfileLink profile = Canvas::ProfileLink::fromJson(config.profileLink);
			if (profile.enabled)
			{
				const std::string profileLabel = IO::formatString("%s%s", getString("profile_link"),
					profile.displayName.empty() ? "" : (" (" + profile.displayName + ")").c_str());
				if (ImGui::BeginMenu(profileLabel.c_str()))
				{
					if (ImGui::MenuItem(IO::concat(getString("profile_browse"), "...", "").c_str(), nullptr, false, profile.usable()))
						profileWindow.open();
					if (ImGui::MenuItem(getString("profile_push"), nullptr, false, !profile.pushTarget.empty() && !publishWindow.isBusy()))
						publishWindow.publish(profile.pushTarget, currentChartForPublish());
					if (profile.pushTarget.empty())
						UI::tooltip(getString("profile_push_needs_target"));
					ImGui::EndMenu();
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem(getString("exit"), ToShortcutString(ImGuiKey_F4, ImGuiMod_Alt)))
				Application::windowState.closing = true;

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("edit")))
		{
			if (ImGui::MenuItem(getString("undo"), ToShortcutString(config.input.undo), false, context.history.hasUndo()))
				context.undo();

			if (ImGui::MenuItem(getString("redo"), ToShortcutString(config.input.redo), false, context.history.hasRedo()))
				context.redo();

			ImGui::Separator();
			const bool hasHiSpeedSelection = timeline.hasSelectedHiSpeed(context);
			const bool hasSEVolumeSelection = timeline.hasSelectedSEVolume(context);
			const bool hasAnySelection = hasHiSpeedSelection || hasSEVolumeSelection || context.selectedNotes.size();
			if (ImGui::MenuItem(getString("delete"), ToShortcutString(config.input.deleteSelection), false, hasAnySelection))
			{
				const bool deletedHiSpeed = timeline.deleteSelectedHiSpeed(context);
				const bool deletedSEVolume = timeline.deleteSelectedSEVolume(context);
				if (!deletedHiSpeed && !deletedSEVolume)
					context.deleteSelection();
			}

			if (ImGui::MenuItem(getString("cut"), ToShortcutString(config.input.cutSelection), false, hasAnySelection))
			{
				if (!timeline.cutSelectedHiSpeed(context) && !timeline.cutSelectedSEVolume(context))
					context.cutSelection();
			}

			if (ImGui::MenuItem(getString("copy"), ToShortcutString(config.input.copySelection), false, hasAnySelection))
			{
				if (!timeline.copySelectedHiSpeed(context) && !timeline.copySelectedSEVolume(context))
					context.copySelection();
			}

			if (ImGui::MenuItem(getString("paste"), ToShortcutString(config.input.paste)))
			{
				if (!timeline.pasteHiSpeed(context, false) && !timeline.pasteSEVolume(context, false))
					context.paste(false);
			}

			ImGui::Separator();
			if (ImGui::MenuItem(getString("select_all"), ToShortcutString(config.input.selectAll)))
				context.selectAll();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("repeat_hold_mids"), nullptr, false, !timeline.isPlaying()))
				editTools.openRepeatMids(context, timeline.getDivision());
			UI::tooltip(getString("repeat_hold_mids_tooltip"));
			if (ImGui::MenuItem(getString("interpolate_hispeed"), nullptr, false, !timeline.isPlaying()))
				editTools.openInterpolate(context, timeline.selectedHiSpeeds());
			UI::tooltip(getString("interpolate_hispeed_tooltip"));

			ImGui::Separator();
			if (ImGui::MenuItem(getString("insert_skill"), ToShortcutString(config.input.insertSkill), nullptr, !timeline.isPlaying()))
				timeline.insertSkill(context, context.currentTick);

			if (ImGui::MenuItem(getString("insert_fever"), ToShortcutString(config.input.insertFever), nullptr, !timeline.isPlaying()))
				timeline.beginInsertFever(context, context.currentTick);

			ImGui::Separator();
			if (ImGui::MenuItem(getString("detect_issues"), ToShortcutString(config.input.detectIssues)))
				detectChartIssues();
			if (ImGui::MenuItem(getString("auto_fix_issues"), ToShortcutString(config.input.autoFixIssues), false, !timeline.isPlaying()))
				openAutoFix();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("settings"), ToShortcutString(config.input.openSettings)))
				settingsWindow.open = true;

			ImGui::EndMenu();
		}
		
		if (ImGui::BeginMenu(getString("chart")))
		{
			// (the Charting Mode menu is hidden for now; the code stays)
			if (ImGui::BeginMenu(getString("stages_menu")))
			{
				bool stagesOn = context.score.stages.enabled;
				if (ImGui::MenuItem(getString("stages_menu_enable"), nullptr, &stagesOn))
				{
					const Score before = context.score;
					context.score.stages.enabled = stagesOn;
					context.pushHistory(getString("stages"), before, context.score);
					if (stagesOn)
						setPanelOpen("stages", true);
				}
				if (ImGui::MenuItem(getString("stages_menu_open"), nullptr, false, context.score.stages.enabled))
					setPanelOpen("stages", true);
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem(getString("auto_place_skill_fever"), nullptr, false, !timeline.isPlaying()))
				autoPlaceSkillFever();
			UI::tooltip(getString("auto_place_skill_fever_tooltip"));

			if (ImGui::MenuItem(getString("clear_skill_fever"), nullptr, false, Canvas::SkillFever::hasMarkers(context.score)))
				clearSkillFever();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("bpm_detect"), nullptr, false, !timeline.isPlaying() && !context.workingData.musicFilename.empty()))
			{
				timeline.setPlaying(context, false);
				bpmDetect.open(context.workingData.musicFilename);
			}
			UI::tooltip(getString("bpm_detect_tooltip"));
			if (ImGui::MenuItem(getString("bpm_interpolate"), nullptr, false, !timeline.isPlaying()))
				bpmDetect.openInterpolate(context);
			UI::tooltip(getString("bpm_interpolate_tooltip"));

			ImGui::Separator();
			const auto& summary = issuesWindow.getSummary();
			const std::string issuesLabel = IO::formatString("%s (%d / %d)###check_chart", getString("detect_issues"), summary.errors, summary.warnings);
			if (ImGui::MenuItem(issuesLabel.c_str(), ToShortcutString(config.input.detectIssues)))
				detectChartIssues();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("view")))
		{
			ImGui::MenuItem(getString("show_step_outlines"), NULL, &timeline.drawHoldStepOutlines);
			ImGui::MenuItem(getString("hide_step_outlines_in_playback"), NULL, &config.hideStepOutlinesInPlayback);
			ImGui::MenuItem(getString("cursor_auto_scroll"), NULL, &config.followCursorInPlayback);
			ImGui::MenuItem(getString("return_to_last_tick"), NULL, &config.returnToLastSelectedTickOnPause);
			ImGui::MenuItem(getString("draw_waveform"), NULL, &config.drawWaveform);
			ImGui::MenuItem(getString("stop_at_end"), NULL, &config.stopAtEnd);
			ImGui::MenuItem(getString("preview_draw_toolbar"), NULL, &config.pvDrawToolbar);
			ImGui::MenuItem(getString("preview_draw_score_hud"), NULL, &config.pvDrawScoreHud);
			ImGui::MenuItem(getString("preview_draw_life_hud"), NULL, &config.pvDrawLifeHud);
			ImGui::MenuItem(getString("preview_draw_combo"), NULL, &config.pvDrawCombo);
			ImGui::MenuItem(getString("preview_draw_perfect"), NULL, &config.pvDrawPerfect);
			drawPreviewEffectMenuItems();

			ImGui::Separator();
			bool sekaiInterface = config.interfaceStyle == 0;
			if (ImGui::MenuItem(getString("sekai_mode"), nullptr, &sekaiInterface))
				config.interfaceStyle = sekaiInterface ? 0 : 1;
			UI::tooltip(getString("sekai_mode_tooltip"));
			if (ImGui::MenuItem(getString("sekai_shuffle_background"), nullptr, false,
				config.sekaiMode && config.sekaiBackgrounds && Canvas::Sekai::assets().isStoriesFolder()))
				sekaiBackground = Canvas::Sekai::pickBackground(sekaiRng);

			ImGui::Separator();
			if (ImGui::BeginMenu(getString("panels")))
			{
				static const std::pair<const char*, const char*> panelList[] = {
					{ ICON_FA_OBJECT_GROUP, "score_preview" }, { ICON_FA_LIST_UL, "chart_issues" }, { ICON_FA_ALIGN_LEFT, "chart_properties" },
					{ ICON_FA_LIST_OL, "song_difficulties" }, { ICON_FA_WRENCH, "options" }, { ICON_FA_DRAFTING_COMPASS, "presets" },
					{ ICON_FA_LAYER_GROUP, "layers" }, { ICON_FA_FILM, "preview_animation" }, { ICON_FA_LAYER_GROUP, "stages" } };
				for (const auto& [icon, id] : panelList)
				{
					bool open = panelOpen(id);
					if (ImGui::MenuItem(IO::concat(icon, getString(id), " ").c_str(), nullptr, &open))
						setPanelOpen(id, open);
				}
				ImGui::Separator();
				ImGui::MenuItem(getString("overview"), nullptr, &config.overviewEnabled);
				ImGui::EndMenu();
			}

			if (ImGui::MenuItem(getString("reset_layout")))
			{
				ImGuiManager::layoutResetRequested = true;
				panelVisibility.clear();
				config.panels = nlohmann::json::object();
			}

			bool isPreviewFullScreen = isFullScreenPreview();
			if (ImGui::MenuItem(getString("fullscreen_preview"), ToShortcutString(config.input.togglePreviewFullWindow), &isPreviewFullScreen))
				preview.setFullWindow(isPreviewFullScreen);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("presets")))
		{
			if (ImGui::MenuItem(getString("create_preset"), NULL, false, !context.selectedNotes.empty()))
				presetsWindow.openCreatePresetDialog();
			
			if (ImGui::MenuItem(getString("import_preset"), NULL, false, true))
				openImportPresetDialog();
			
			if (ImGui::MenuItem(getString("reload_presets"), NULL, false, true))
			{
				presetManager.presets.clear();
				loadPresets();
			}
			
			ImGui::Separator();
			if (ImGui::MenuItem(getString("open_presets_folder"), NULL, false, true))
			{
				if (!std::filesystem::exists(presetManager.getPresetsPath()))
					std::filesystem::create_directory(presetManager.getPresetsPath());
				IO::openInShell(presetManager.getPresetsPath().u8string());
			}
			
			ImGui::EndMenu();
		}
		
		if (config.debugEnabled)
		{
			if (ImGui::BeginMenu(getString("debug")))
			{
#ifdef _DEBUG
				ImGui::MenuItem("ImGui Demo Window", NULL, &showImGuiDemoWindow);
#endif

				if (ImGui::MenuItem("Auto Save"))
					autoSave();

				if (ImGui::MenuItem("Delete Old Auto Save (1)"))
					deleteOldAutoSave(1);

				if (ImGui::MenuItem("Delete Old Auto Save (Max)"))
					deleteOldAutoSave(config.autoSaveMaxCount);

				bool audioRunning = context.audio.isEngineStarted();
				if (ImGui::MenuItem(audioRunning ? "Stop Audio" : "Start Audio",
					audioRunning ? ICON_FA_VOLUME_UP : ICON_FA_VOLUME_MUTE))
				{
					if (audioRunning)
						context.audio.stopEngine();
					else
						context.audio.startEngine();
				}

				ImGui::EndMenu();
			}
		}

		if (ImGui::BeginMenu(getString("window")))
		{
			if (ImGui::MenuItem(getString("vsync"), NULL, &Application::windowState.vsync))
				glfwSwapInterval(Application::windowState.vsync);

			ImGui::MenuItem(getString("show_fps"), NULL, &config.showFPS);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("help")))
		{
			if (ImGui::MenuItem(getString("help"), ToShortcutString(config.input.openHelp)))
				help();

			if (ImGui::MenuItem(getString("replay_intro")))
				Application::introReplayRequested = true;

			if (ImGui::MenuItem(getString("about")))
				aboutDialog.open = true;

			ImGui::EndMenu();
		}

		if (config.showFPS)
		{
			std::string fps = IO::formatString("%.3fms (%.1fFPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
			ImGui::SetCursorPosX(ImGui::GetWindowSize().x - ImGui::CalcTextSize(fps.c_str()).x - ImGui::GetStyle().WindowPadding.x);
			ImGui::Text(fps.c_str());
		}

		ImGui::PopStyleVar();
		ImGui::EndMainMenuBar();
	}

	namespace
	{
		std::string clockText(double seconds)
		{
			const long long s = static_cast<long long>(std::max(0.0, seconds));
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
			return buffer;
		}

		// "Session" counts from when Canvas opened; "Total" is every session so far (saved with the
		// settings). Right-click to reset the total.
		void drawChartingTimer()
		{
			using Clock = std::chrono::steady_clock;
			static const Clock::time_point opened = Clock::now();
			static double before = config.chartingSeconds;
			const double session = std::chrono::duration<double>(Clock::now() - opened).count();
			config.chartingSeconds = before + session;

			const std::string text = IO::concat(ICON_FA_CLOCK, getString("timer_session"), " ") + " " + clockText(session) +
				"    " + getString("timer_total") + " " + clockText(config.chartingSeconds);
			const float width = ImGui::CalcTextSize(text.c_str()).x;
			const float x = ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x - 8.0f;
			ImGui::SameLine();
			if (ImGui::GetCursorPosX() + 16.0f > x)
				return;	// no room left on the toolbar
			ImGui::SetCursorPosX(x);
			ImGui::SetCursorPosY((ImGui::GetWindowHeight() - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::TextDisabled("%s", text.c_str());
			UI::tooltip(getString("timer_tooltip"));
			if (ImGui::BeginPopupContextItem("##charting_timer"))
			{
				if (ImGui::MenuItem(getString("timer_reset_total")))
					before = -session;
				ImGui::EndPopup();
			}
		}
	}

	void ScoreEditor::drawToolbar()
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 toolbarSize{ viewport->WorkSize.x, UI::toolbarHeight() };

		// keep toolbar on top in main viewport
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(toolbarSize, ImGuiCond_Always);

		// toolbar style
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##app_toolbar", NULL, ImGuiWindowFlags_Toolbar);

		// Canvas Neo groups the buttons on capsules; Classic keeps the separators
		const bool neo = Canvas::SekaiStyle::active();
		Canvas::SekaiStyle::ToolbarBar bar;
		auto separator = [&]() { if (neo) bar.group(); else UI::toolbarSeparator(); };
		if (neo)
		{
			ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 4.0f, (toolbarSize.y - UI::toolbarBtnSize.y * 1.15f) * 0.5f));
			bar.begin();
			bar.group();
		}
		
		if (UI::toolbarButton(ICON_FA_FILE, getString("new"), ToShortcutString(config.input.create)))
		{
			Application::windowState.resetting = true;
		}

		if (UI::toolbarButton(ICON_FA_FOLDER_OPEN, getString("open"), ToShortcutString(config.input.open)))
		{
			Application::windowState.resetting = true;
			Application::windowState.shouldPickScore = true;
		}

		if (UI::toolbarButton(ICON_FA_SAVE, getString("save"), ToShortcutString(config.input.save)))
			trySave(context.workingData.filename);

		if (UI::toolbarButton(ICON_FA_FILE_EXPORT, getString("export"), ToShortcutString(config.input.exportScore)))
			exportScore();

		separator();

		const bool hasHiSpeedSelection = timeline.hasSelectedHiSpeed(context);
		const bool hasSEVolumeSelection = timeline.hasSelectedSEVolume(context);
		const bool hasAnySelection = hasHiSpeedSelection || hasSEVolumeSelection || context.selectedNotes.size() > 0;
		if (UI::toolbarButton(ICON_FA_CUT, getString("cut"), ToShortcutString(config.input.cutSelection), hasAnySelection))
		{
			if (!timeline.cutSelectedHiSpeed(context) && !timeline.cutSelectedSEVolume(context))
				context.cutSelection();
		}

		if (UI::toolbarButton(ICON_FA_COPY, getString("copy"), ToShortcutString(config.input.copySelection), hasAnySelection))
		{
			if (!timeline.copySelectedHiSpeed(context) && !timeline.copySelectedSEVolume(context))
				context.copySelection();
		}

		if (UI::toolbarButton(ICON_FA_PASTE, getString("paste"), ToShortcutString(config.input.paste)))
		{
			if (!timeline.pasteHiSpeed(context, false) && !timeline.pasteSEVolume(context, false))
				context.paste(false);
		}

		separator();

		if (UI::toolbarButton(ICON_FA_UNDO, getString("undo"), ToShortcutString(config.input.undo), context.history.hasUndo()))
			context.undo();

		if (UI::toolbarButton(ICON_FA_REDO, getString("redo"), ToShortcutString(config.input.redo), context.history.hasRedo()))
			context.redo();

		separator();

		// the SEKAI interface shows the game's own Score Maker icons for the tools
		const bool gameIcons = neo && Canvas::SekaiStyle::hasAssetPack();
		for (int i = 0; gameIcons && i < (int)TimelineMode::TimelineModeMax; ++i)
		{
			const TimelineMode mode = static_cast<TimelineMode>(i);
			std::string sprite;
			bool flip = false;
			switch (mode)
			{
			case TimelineMode::InsertTap: sprite = "notes/notes_icon_normal"; break;
			case TimelineMode::InsertLong: sprite = "notes/notes_icon_long"; break;
			case TimelineMode::InsertLongMid: sprite = edit.stepType == HoldStepType::Hidden ? "notes/notes_icon_long_amang_clear" : "notes/notes_icon_long_among"; break;
			case TimelineMode::InsertFlick:
				sprite = edit.flickType == FlickType::Default ? "notes/notes_icon_frick" : "notes/notes_icon_frick_l";
				flip = edit.flickType == FlickType::Right;
				break;
			case TimelineMode::MakeCritical: sprite = "notes/notes_icon_normal_crtc"; break;
			case TimelineMode::MakeFriction: sprite = "notes/notes_icon_trace"; break;
			case TimelineMode::InsertGuide: sprite = "notes/notes_icon_guide"; break;
			case TimelineMode::InsertBPM: sprite = "ui/icon_bpm_change_h112_wh"; break;
			case TimelineMode::InsertTimeSign: sprite = "ui/icon_beat_h112_wh"; break;
			case TimelineMode::InsertHiSpeed: sprite = "ui/icon_notes_speed_h112_wh"; break;
			default: break;
			}
			const bool selected = (int)timeline.getMode() == i;
			const std::string id = IO::concat("##tool", timelineModes[i], "_");
			// font icons (select, SE volume) get the same size and centred disc as the note icons
			const bool clicked = sprite.empty()
				? Canvas::SekaiStyle::glyphButton(id.c_str(), mode == TimelineMode::Select ? ICON_FA_MOUSE_POINTER : ICON_FA_VOLUME_UP, UI::toolbarBtnSize * 1.15f, selected)
				: Canvas::SekaiStyle::spriteButton(id.c_str(), sprite, UI::toolbarBtnSize * 1.15f, selected, true, flip);
			std::string tip = getString(timelineModes[i]);
			if (const char* shortcut = ToShortcutString(*timelineModeBindings[i]); shortcut && *shortcut)
				tip.append(" (").append(shortcut).append(")");
			UI::tooltip(tip.c_str());
			ImGui::SameLine();
			if (clicked)
				timeline.changeMode(mode, edit);
		}

		for (int i = 0; !gameIcons && i < (int)TimelineMode::TimelineModeMax; ++i)
		{
			if (i == (int)TimelineMode::InsertSEVolume)
			{
				if (UI::toolbarButton(ICON_FA_VOLUME_UP, getString(timelineModes[i]), ToShortcutString(*timelineModeBindings[i]), true, (int)timeline.getMode() == i))
					timeline.changeMode((TimelineMode)i, edit);
				continue;
			}

			std::string img{ IO::concat("timeline_tools", timelineModes[i], "_") };
			int sprIndex = (int)timelineSprites[i];
			if (i == (int)TimelineMode::InsertFlick)
			{
				img.append(IO::concat("", flickTypes[(int)edit.flickType], "_"));
				sprIndex = (int)flickToolSprites[(int)edit.flickType - 1];
			}
			else if (i == (int)TimelineMode::InsertLongMid)
			{
				img.append(IO::concat("", stepTypes[(int)edit.stepType], "_"));
				sprIndex = (int)stepToolSprites[(int)edit.stepType];
			}

			if (UI::toolbarImageButton("timeline_tools", sprIndex, getString(timelineModes[i]), ToShortcutString(*timelineModeBindings[i]), true, (int)timeline.getMode() == i))
				timeline.changeMode((TimelineMode)i, edit);
		}

		if (neo)
		{
			// quick actions that are new in 1.2.1, on their own capsule
			bar.group();
			if (UI::toolbarButton(ICON_FA_MAGIC, getString("auto_fix_issues"), ToShortcutString(config.input.autoFixIssues)))
				openAutoFix();
			if (UI::toolbarButton(ICON_FA_FILM, getString("export_video"), "", !videoExport.isRunning()))
				videoExport.open(context);
			bar.end();
		}
		drawChartingTimer();

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		ImGui::End();
	}

	void ScoreEditor::updateEffectSounds()
	{
		const auto options = ScorePreviewWindow::overlayOptions(false);
		const std::string key = IO::formatString("%d|%d%d%d%d|%d|%d", context.revision, options.skills, options.fever, options.allPerfect,
			options.cutins, static_cast<int>(options.mode), Canvas::Preview::deckRevision());
		if (key != effectCuesKey)
		{
			effectCues = Canvas::Preview::effectCues(context.score, options);
			effectCuesKey = key;
		}
		effectPlayer.update(context.audio.getEngine(), timeline.isPlaying() && config.pvEffectSounds,
			context.getTimeAtCurrentTick(), effectCues, config.pvEffectVolume);
	}

	void ScoreEditor::applyAutomation(AutomationOptions& options, int frame)
	{
		// the chart opens on the first frames; act once it is there
		if (frame == 2 && !options.music.empty())
			asyncLoadMusic(options.music);
		if (frame == 2 && options.liveMode >= 0)
			config.pvLiveMode = config.videoLiveMode = options.liveMode;
		if (frame == 2 && options.importCardSlot >= 0)
		{
			// a test of "Get from sekai.best": waits for it and prints the result
			Canvas::Preview::CardImport import;
			if (import.start(options.importCardSlot, options.importCardLink, true))
				while (import.busy())
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::printf("import-card: %s\n", import.poll().c_str());
			std::fflush(stdout);
		}
		if (frame == 4)
		{
			if (options.tick >= 0)
				context.currentTick = options.tick;
			if (options.ticksAfterEnd >= 0)
			{
				int last = 0;
				for (const auto& [id, note] : context.score.notes)
					last = std::max(last, note.tick);
				context.currentTick = last + options.ticksAfterEnd;
			}
			if (options.settings)
				settingsWindow.open = true;
			if (options.openDialog == "settings-preview")
			{
				settingsWindow.open = true;
				settingsWindow.selectPreviewTab = true;
			}
			if (options.openDialog == "settings-canvas")
			{
				settingsWindow.open = true;
				settingsWindow.selectCanvasTab = true;
			}
			if (options.openDialog == "autofix" || options.openDialog == "autofix-run")
			{
				openAutoFix();
				if (options.openDialog == "autofix-run")
					autoFixWindow.fixWhenReady();
			}
			else if (options.openDialog == "repeat")
			{
				// like selecting the first hold and pressing Repeat Hold Mids
				for (const auto& [id, hold] : context.score.holdNotes)
				{
					context.selectedNotes.insert(id);
					break;
				}
				editTools.openRepeatMids(context, timeline.getDivision());
			}
			else if (options.openDialog == "interpolate")
				editTools.openInterpolate(context, timeline.selectedHiSpeeds());
			else if (options.openDialog == "stages")
			{
				// stages on, panel open; a chart without stages gets two demo ones so the tables have content
				if (!context.score.stages.enabled && context.score.stages.empty())
					Canvas::Stages::fillDemo(context.score);
				context.score.stages.enabled = true;
				setPanelOpen("stages", true);
			}
			if (options.play)
				timeline.setPlaying(context, true);
			if (!options.exportVideo.empty())
				videoExport.startAutomated(context, timeline, options.exportVideo);
		}
		if (frame >= 4 && frame <= 8 && !options.focus.empty())
			ImGui::SetWindowFocus(("###" + options.focus).c_str());
		if (!options.exportVideo.empty() && frame > 8 && !videoExport.isRunning())
		{
			std::printf("%s\n", videoExport.lastResult().c_str());
			glfwSetWindowShouldClose(glfwGetCurrentContext(), 1);
		}
	}

	void ScoreEditor::help()
	{
		IO::openInShell(Canvas::Branding::HELP_URL);
	}

	void ScoreEditor::autoSave()
	{
#ifdef _WIN32
		std::wstring wAutoSaveDir = IO::mbToWideStr(autoSavePath);
		std::filesystem::create_directory(wAutoSaveDir);
#else
		std::filesystem::create_directory(autoSavePath);
#endif

		context.score.metadata = context.workingData.toScoreMetadata();
		NativeScoreSerializer().serialize(context.score, autoSavePath + "/canvas_auto_save_" + Utilities::getCurrentDateTime() + CE_EXTENSION);
		
		int mmwsCount = 0;
#ifdef _WIN32
		for (const auto& file : std::filesystem::directory_iterator(wAutoSaveDir))
#else
		for (const auto& file : std::filesystem::directory_iterator(autoSavePath))
#endif
		{
			std::string extension = file.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			mmwsCount += static_cast<int>(extension == MMWS_EXTENSION || extension == CE_EXTENSION);
		}
		
		if (mmwsCount > config.autoSaveMaxCount)
			deleteOldAutoSave(mmwsCount - config.autoSaveMaxCount);
	}

	int ScoreEditor::deleteOldAutoSave(int count)
	{
		std::wstring wAutoSaveDir = IO::mbToWideStr(autoSavePath);
		if (!std::filesystem::exists(wAutoSaveDir))
			return 0;

		// get mmws files
		using entry = std::filesystem::directory_entry;
		std::vector<entry> deleteFiles;
		for (const auto& file : std::filesystem::directory_iterator(wAutoSaveDir))
		{
			std::string extension = file.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			if (extension == MMWS_EXTENSION || extension == CE_EXTENSION)
				deleteFiles.push_back(file);
		}

		// sort files by modification date
		std::sort(deleteFiles.begin(), deleteFiles.end(), [](const entry& f1, const entry& f2) {
			return f1.last_write_time() < f2.last_write_time();
		});

		int deleteCount = std::min(static_cast<int>(deleteFiles.size()), count);
		for (int i = 0; i < deleteCount; i++)
			std::filesystem::remove(deleteFiles[i]);
		
		return deleteCount;
	}

	void ScoreEditor::loadPresets()
	{
		if (loadPresetsFuture.valid())
			loadPresetsFuture.get();
		
		loadPresetsFuture = std::async(std::launch::async, [this]()
		{
			presetsWindow.notifyPresetsLoading();
			presetManager.loadPresets();
			presetsWindow.notifyPresetsLoaded(presetManager);
		});
	}

	void ScoreEditor::importPreset(const std::string& path)
	{
		if (importPresetFuture.valid())
			importPresetFuture.get();

		presetsWindow.notifyPresetsLoading();
		importPresetFuture = std::async(std::launch::async, [this, path]()
		{
			Result result = presetManager.importPreset(path);
			presetsWindow.notifyPresetsLoaded(presetManager);

			if (result.getStatus() != ResultStatus::Success)
			{
				IO::MessageBoxIcon icon = result.getStatus() == ResultStatus::Error ?
					IO::MessageBoxIcon::Error :
					IO::MessageBoxIcon::Warning;
			
				IO::messageBox(APP_NAME, result.getMessage(), IO::MessageBoxButtons::Ok, icon, Application::windowState.windowHandle);
			}
		});
	}

	void ScoreEditor::openImportPresetDialog()
	{
		IO::FileDialog fileDialog{"Import Preset", { IO::presetFilter }, "", "", ".json", 0, Application::windowState.windowHandle};
		if (fileDialog.openFile() == IO::FileDialogResult::OK)
			importPreset(fileDialog.outputFilename);
	}
}
