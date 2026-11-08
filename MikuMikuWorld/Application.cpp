#include "Application.h"
#include "Canvas/Core/ChartIO.h"
#include "Canvas/Core/PngWriter.h"
#include "Canvas/Video/MoviePlayer.h"
#include "Canvas/Sekai/SekaiAssets.h"
#include "Canvas/Sekai/SekaiMode.h"
#include "Canvas/Preview/LiveOverlay.h"
#include "Canvas/UI/SekaiStyle.h"
#include "Canvas/Sekai/UiSounds.h"
#include "ResourceManager.h"
#include "IO.h"
#include "Utilities.h"
#include "Localization.h"
#include "ApplicationConfiguration.h"
#include "ScoreSerializer.h"
#include "NoteSkin.h"
#include "Canvas/Core/Profiler.h"
#include "Canvas/Preview/CustomSkins.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace MikuMikuWorld
{
#ifndef MIKUMIKUWORLD_VERSION
#define MIKUMIKUWORLD_VERSION "1.0.0"
#endif

	std::string Application::version{ "1.0.0" };
	std::string Application::appDir{ "" };
	static std::string userDataDir{ "" };
	std::string Application::pendingLoadScoreFile{ "" };
	WindowState Application::windowState{};
	AutomationOptions Application::automation{};

	AutomationOptions AutomationOptions::parse(std::vector<std::string>& args)
	{
		AutomationOptions options;
		std::vector<std::string> files;
		for (size_t i = 0; i < args.size(); ++i)
		{
			const std::string& arg = args[i];
			auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string{}; };
			if (arg == "--screenshot")
				options.screenshot = next();
			else if (arg == "--frames")
				options.frames = std::max(2, std::atoi(next().c_str()));
			else if (arg == "--size")
			{
				const std::string size = next();
				const size_t x = size.find('x');
				if (x != std::string::npos)
				{
					options.width = std::atoi(size.substr(0, x).c_str());
					options.height = std::atoi(size.substr(x + 1).c_str());
				}
			}
			else if (arg == "--focus")
				options.focus = next();
			else if (arg == "--tick")
			{
				const std::string value = next();
				if (value.rfind("end", 0) == 0)
					options.ticksAfterEnd = std::max(0, std::atoi(value.c_str() + 3));
				else
					options.tick = std::atoi(value.c_str());
			}
			else if (arg == "--music")
				options.music = next();
			else if (arg == "--mode")
			{
				const std::string mode = next();
				options.liveMode = mode == "coop" ? 1 : mode == "carnival" ? 2 : 0;
			}
			else if (arg == "--play")
				options.play = true;
			else if (arg == "--bench")
				options.bench = options.play = true;
			else if (arg == "--bench-idle")
				options.bench = options.benchIdle = true;
			else if (arg == "--bench-gpu")
				options.bench = options.benchGpu = options.play = true;
			else if (arg == "--settings")
				options.settings = true;
			else if (arg == "--open")
				options.openDialog = next();
			else if (arg == "--settings-scroll")
				options.settingsScroll = static_cast<float>(std::atof(next().c_str()));
			else if (arg == "--export-video")
				options.exportVideo = next();
			else if (arg == "--import-card")
			{
				options.importCardSlot = std::atoi(next().c_str()) - 1;
				options.importCardLink = next();
			}
			else
				files.push_back(arg);
		}
		args = files;
		options.enabled = !options.screenshot.empty() || !options.exportVideo.empty() || options.bench;
		IO::suppressMessageBoxes = options.enabled;
		return options;
	}

	namespace
	{
		std::string ensureTrailingSlash(std::string path)
		{
			if (!path.empty() && path.back() != '/')
				path.push_back('/');

			return path;
		}

		std::string resolveUserDataDir(const std::string& resourceDir)
		{
#if defined(__APPLE__)
			const char* homeDir = std::getenv("HOME");
			if (homeDir && *homeDir)
				return ensureTrailingSlash(std::string(homeDir) + "/Library/Application Support/" + Canvas::Branding::USER_DATA_FOLDER);
#elif !defined(_WIN32)
			// Linux / BSD: follow the XDG base directory spec so an installed (read-only) copy still works
			const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
			if (xdgConfig && *xdgConfig)
				return ensureTrailingSlash(std::string(xdgConfig) + "/" + Canvas::Branding::USER_DATA_FOLDER);

			const char* homeDir = std::getenv("HOME");
			if (homeDir && *homeDir)
				return ensureTrailingSlash(std::string(homeDir) + "/.config/" + Canvas::Branding::USER_DATA_FOLDER);
#endif
			// Windows: portable install, settings live next to the executable
			return resourceDir;
		}

		void ensureDirectoryExists(const std::string& path)
		{
			if (path.empty())
				return;

			std::error_code error;
#ifdef _WIN32
			std::filesystem::create_directories(IO::mbToWideStr(path), error);
#else
			std::filesystem::create_directories(path, error);
#endif
		}
	}

	Application::Application() : 
		initialized{ false }, language{ "" }
	{
	}

	Result Application::initialize(const std::string& root)
	{
		if (initialized)
			return Result(ResultStatus::Success, "App is already initialized");

		appDir = ensureTrailingSlash(root);
		userDataDir = resolveUserDataDir(appDir);
		ensureDirectoryExists(userDataDir);
		version = getVersion();
		language = "";

		const std::string userConfigPath = userDataDir + APP_CONFIG_FILENAME;
		const std::string bundledConfigPath = appDir + APP_CONFIG_FILENAME;
		if (IO::File::exists(userConfigPath))
			config.read(userConfigPath);
		else
			config.read(bundledConfigPath);
		if (automation.width > 0 && automation.height > 0)
		{
			config.windowSize = Vector2(static_cast<float>(automation.width), static_cast<float>(automation.height));
			config.windowPos = Vector2(40, 40);
			config.maximized = false;
			config.fullScreen = false;
		}
		readSettings();

		Result result = initOpenGL();
		if (!result.isOk())
			return result;

		imgui = std::make_unique<ImGuiManager>();
		result = imgui->initialize(window);
		if (!result.isOk())
			return result;

		imgui->setBaseTheme(config.baseTheme);
		imgui->applyAccentColor(config.accentColor);

		loadResources();

		editor = std::make_unique<ScoreEditor>();
		editor->loadPresets();

		initialized = true;
		return Result::Ok();
	}

	const std::string& Application::getAppDir()
	{
		return appDir;
	}

	const std::string& Application::getUserDataDir()
	{
		return userDataDir;
	}

	std::string Application::getVersion()
	{
#if defined(CANVAS_VERSION)
		// "1.2.2b": the label CMake bakes in (the exe's file version can't hold the letter)
		return CANVAS_VERSION;
#elif defined(_WIN32)
		wchar_t modulePath[MAX_PATH]{};
		GetModuleFileNameW(NULL, modulePath, MAX_PATH);
		std::wstring filename = modulePath;

		DWORD  verHandle = 0;
		UINT   size = 0;
		LPBYTE lpBuffer = NULL;
		DWORD  verSize = GetFileVersionInfoSizeW(filename.c_str(), &verHandle);

		int major = 0, minor = 0, build = 0, rev = 0;
		if (verSize != NULL)
		{
			LPSTR verData = new char[verSize];

			if (GetFileVersionInfoW(filename.c_str(), verHandle, verSize, verData))
			{
				if (VerQueryValue(verData, TEXT("\\"), (VOID FAR * FAR*) & lpBuffer, &size))
				{
					if (size)
					{
						VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
						if (verInfo->dwSignature == 0xfeef04bd)
						{
							major = (verInfo->dwFileVersionMS >> 16) & 0xffff;
							minor = (verInfo->dwFileVersionMS >> 0) & 0xffff;
							rev = (verInfo->dwFileVersionLS >> 16) & 0xffff;
						}
					}
				}
			}
			delete[] verData;
		}

		return IO::formatString("%d.%d.%d", major, minor, rev);
#else
		return MIKUMIKUWORLD_VERSION;
#endif
	}

	const std::string& Application::getAppVersion()
	{
		return version;
	}

	void Application::dispose()
	{
		if (initialized)
		{
			editor->uninitialize();
			// the editor owns GL objects (framebuffers, textures): free them while the context exists,
			// not from a static destructor after exit(), where the GL entry points are already gone (a
			// crash on Linux after every close)
			editor.reset();
			Canvas::Preview::releaseGlResources();
			Canvas::SekaiStyle::releaseGlResources();
			imgui->shutdown();
			glfwDestroyWindow(window);
			glfwTerminate();
		}
		initialized = false;
	}

	void Application::readSettings()
	{
		windowState.position = config.windowPos;
		windowState.size = config.windowSize;
		windowState.maximized = config.maximized;
		windowState.vsync = config.vsync;
		windowState.fullScreen = config.fullScreen;
		UI::accentColors[0] = config.userColor.toImVec4();
	}

	void Application::writeSettings()
	{
		config.maximized = windowState.maximized;
		config.vsync = windowState.vsync;
		config.windowPos = windowState.position;
		config.windowSize = windowState.size;
		config.fullScreen = windowState.fullScreen;
		config.userColor = Color::fromImVec4(UI::accentColors[0]);

		if (editor)
		{
			editor->writeSettings();
			config.write(userDataDir + APP_CONFIG_FILENAME);
		}
	}

	void Application::appendOpenFile(const std::string& filename)
	{
		pendingOpenFiles.push_back(filename);
		windowState.dragDropHandled = false;
	}

	void Application::handlePendingOpenFiles()
	{
		std::string scoreFile{};
		std::string musicFile{};
		std::string movieFile{};

		for (auto it = pendingOpenFiles.rbegin(); it != pendingOpenFiles.rend(); ++it)
		{
			std::string extension = IO::File::getFileExtension(*it);
			std::transform(extension.begin(), extension.end(), extension.begin(), tolower);

			if (Audio::isSupportedFileFormat(extension))
				musicFile = *it;
			else if (Canvas::Video::MoviePlayer::isMovieFile(*it))
			{
				if (movieFile.empty())
					movieFile = *it;
			}
			else if (Canvas::looksLikeChartFile(*it))
				scoreFile = *it;

			if (!scoreFile.empty() && !musicFile.empty())
				break;
		}

		if (!scoreFile.empty())
		{
			windowState.resetting = true;
			pendingLoadScoreFile = scoreFile;
		}

		if (!musicFile.empty())
			editor->asyncLoadMusic(musicFile);

		// a video dropped on the editor becomes the Score Preview's 2DMV
		if (!movieFile.empty())
		{
			config.pvMovieFile = movieFile;
			config.pvDrawMovie = true;
		}

		pendingOpenFiles.clear();
		windowState.dragDropHandled = true;
	}

	void Application::update()
	{
#ifdef __APPLE__
		static bool aboutHooked = false;
		if (!aboutHooked)
			aboutHooked = hookMacAboutMenu();
#endif
		if (config.language != language)
		{
			std::string locale = config.language == "auto" ? Utilities::getSystemLocale() : config.language;
			
			// Try to set the selected language and fallback to default (en) on failure
			if (!Localization::setLanguage(locale))
				Localization::setLanguage("en");

			language = config.language;
		}

#ifdef __APPLE__
		// GLFW reports Retina backing scale here, but the UI already renders in logical points
		// (the framebuffer is scaled by the backend). Applying it again makes the UI oversized.
		float dpiScale = 1.0f;
#else
		// the monitor the window is on, not the primary one: dragging the window to another
		// screen, or changing Windows' display scale / resolution, rescales the UI
		float dpiX = 1.0f, dpiY = 1.0f;
		glfwGetWindowContentScale(window, &dpiX, &dpiY);
		float dpiScale = (dpiX + dpiY) * 0.5f;
		if (!(dpiScale > 0.0f))
			dpiScale = 1.0f;
#endif
		// the SEKAI interface is what SEKAI Mode was: its sounds, pop-ups and backgrounds go with it
		config.sekaiMode = config.interfaceStyle == 0;
		if (windowState.lastDpiScale == 0.0f || imgui->fontsOutdated())
			imgui->buildFonts();
		if (dpiScale != windowState.lastDpiScale)
		{
			imgui->setDpiScale(dpiScale);
			windowState.lastDpiScale = dpiScale;
		}

		if (!appIconLoaded || config.appIconPath != appliedIconPath)
			loadAppIcon();

		// SEKAI Mode assets (fonts / sounds / backgrounds from a local SEKAI-Stories folder)
		Canvas::Sekai::assets().refresh(config.sekaiAssetsDir);
		imgui->updateTitleFont(config.sekaiMode ? Canvas::Sekai::assets().font("FOT-RodinNTLGPro-DB.otf") : std::string{});

		imgui->begin();

		UI::updateBtnSizesDpiScaling(dpiScale * config.uiScale);

		if (!windowState.dragDropHandled)
			handlePendingOpenFiles();

		imgui->initializeLayout();

		if (config.accentColor != imgui->getAccentColor())
			imgui->applyAccentColor(config.accentColor);

		if (config.userColor != Color::fromImVec4(UI::accentColors[0]) && config.accentColor == 0)
			imgui->applyAccentColor(config.accentColor);

		if (config.baseTheme != imgui->getBaseTheme() || imgui->themeOutdated())
			imgui->setBaseTheme(config.baseTheme);

		if ((windowState.closing || windowState.resetting) && !editor->isUpToDate() && !unsavedChangesDialog.open)
		{
			unsavedChangesDialog.open = true;
			ImGui::OpenPopup(MODAL_TITLE("unsaved_changes"));
		}

		auto unsavedChangesResult = unsavedChangesDialog.update();

		if (windowState.closing)
		{
			if (!editor->isUpToDate())
			{
				switch (unsavedChangesResult)
				{
				case DialogResult::Yes:
					editor->trySave(editor->getWorkingFilename().data());
					glfwSetWindowShouldClose(window, 1);
					break;

				case DialogResult::No:
					glfwSetWindowShouldClose(window, 1);
					break;

				case DialogResult::Cancel:
					windowState.closing = false;
					break;

				default:
					break;
				}
			}
			else
			{
				glfwSetWindowShouldClose(window, 1);
			}
		}

		if (windowState.resetting)
		{
			if (!editor->isUpToDate())
			{
				switch (unsavedChangesResult)
				{
				case DialogResult::Yes:
					editor->trySave(editor->getWorkingFilename().data());
					break;

				case DialogResult::Cancel:
					windowState.resetting = shouldPickScore = false;
					pendingLoadScoreFile.clear();
					break;

				default:
					break;
				}
			}

			// Already saved or clicked save changes or discard changes
			if (editor->isUpToDate() || (unsavedChangesResult != DialogResult::Cancel && unsavedChangesResult != DialogResult::None))
			{
				if (windowState.shouldPickScore)
				{
					editor->open();
					windowState.shouldPickScore = false;
				}
				else if (pendingLoadScoreFile.size())
				{
					editor->loadScore(pendingLoadScoreFile);
					pendingLoadScoreFile.clear();
				}
				else
				{
					editor->reset();
				}

				windowState.resetting = false;
			}
		}

		UI::refreshFrameSizes();
		{
			// any input keeps the app at full speed for a second
			const ImGuiIO& io = ImGui::GetIO();
			bool input = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
				ImGui::IsAnyMouseDown() || io.InputQueueCharacters.Size > 0 || io.WantTextInput;
			for (int key = ImGuiKey_NamedKey_BEGIN; !input && key < ImGuiKey_NamedKey_END; ++key)
				input = ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
			if (input)
				lastActivity = glfwGetTime();
		}
		{
			CANVAS_PROFILE("editor.update");
			editor->update();
		}

		bool isFullScreen = config.fullScreen;
		if (ImGui::IsAnyPressed(config.input.toggleFullscreen)) setFullScreen(!isFullScreen);

		if (!editor->isFullScreenPreview())
		{
			ImGui::BeginMainMenuBar();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 2));

			if (ImGui::BeginMenu(getString("window")))
			{
				if (ImGui::MenuItem(getString("fullscreen"), ToShortcutString(config.input.toggleFullscreen), &isFullScreen))
					setFullScreen(isFullScreen);

				ImGui::EndMenu();
			}

			ImGui::PopStyleVar();
			ImGui::EndMainMenuBar();
		}

		if (automation.active())
			editor->applyAutomation(automation, automationFrame);

		// startup: "What are you Charting today?" (the old intro only plays when asked for again)
		if (!introShown)
		{
			const bool chartGiven = !pendingLoadScoreFile.empty() || windowState.resetting;
			// the picker is hidden for now (code kept): the editor loads straight in
			(void)chartGiven;
			if (automation.openDialog == "modepicker")
				modePicker.open();
			introShown = true;
		}
		if (introReplayRequested)
		{
			startIntro();
			introReplayRequested = false;
		}
		modePickerRequested = false;
		intro.update();
		if (modePicker.update(ImGuiManager::titleFont, config.sekaiMode && config.sekaiSounds, config.sekaiSoundVolume))
		{
			config.chartMode = static_cast<int>(modePicker.picked);
			if (modePicker.openRequested)
			{
				windowState.resetting = true;
				windowState.shouldPickScore = true;
			}
		}

		Canvas::Sekai::hooks().endFrame(config.sekaiMode && config.sekaiSounds && !intro.active(), config.sekaiSoundVolume, config.sekaiMode);

		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		{
			CANVAS_PROFILE_GPU("imgui.render");
			imgui->draw(window);
		}
		if (automation.active())
			runAutomation();
		glfwSwapBuffers(window);
	}

	void Application::runAutomation()
	{
		++automationFrame;
		if (automation.screenshot.empty() || automationFrame < automation.frames)
			return;

		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glReadBuffer(GL_BACK);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		for (size_t i = 3; i < pixels.size(); i += 4)
			pixels[i] = 255;
		Canvas::writePng(automation.screenshot, width, height, pixels.data(), true);
		std::printf("%s\n", Canvas::Sekai::uiSounds().summary().c_str());
		automation.screenshot.clear();
		if (automation.exportVideo.empty())
			glfwSetWindowShouldClose(window, 1);
	}

	void Application::startIntro()
	{
		Canvas::Sekai::IntroOptions options;
		options.text = config.introTextOverride.empty() ? Canvas::Branding::INTRO_TEXT : config.introTextOverride;
		options.subtext = Canvas::Branding::INTRO_SUBTEXT;
		options.duration = Canvas::Branding::INTRO_DURATION_SECONDS;
		options.font = ImGuiManager::titleFont;
		options.sounds = config.sekaiMode && config.sekaiSounds;
		options.volume = config.sekaiSoundVolume;
		if (config.sekaiMode && config.sekaiBackgrounds)
			options.backgroundImage = Canvas::Sekai::assets().image("Background_Between_Worlds.jpg");
		intro.start(options);
	}

	void Application::loadResources()
	{
		ResourceManager::loadShader(appDir + "res/shaders/basic2d");
		ResourceManager::loadShader(appDir + "res/shaders/masking");
		ResourceManager::loadShader(appDir + "res/shaders/particles");

		// TODO: Do not set the note skin texture indexes manually!
		const std::string notes01TexDir = appDir + "res/notes/01/";
		ResourceManager::loadTexture(notes01TexDir + "notes.png");
		ResourceManager::loadTexture(notes01TexDir + "longNoteLine.png");
		ResourceManager::loadTexture(notes01TexDir + "touchLine_eff.png");
		noteSkins.add("Notes 01", 0, 1, 2);

		const std::string notes02TexDir = appDir + "res/notes/02/";
		ResourceManager::loadTexture(notes02TexDir + "notes.png");
		ResourceManager::loadTexture(notes02TexDir + "longNoteLine.png");
		ResourceManager::loadTexture(notes02TexDir + "touchLine_eff.png");
		noteSkins.add("Notes 02", 3, 4, 5);
		// the user's own (Canvas/Preview/CustomSkins) after the two built-in ones

		// colour sheet for guides beyond green/yellow (neutral, red, blue, purple, cyan, black)
		ResourceManager::loadTexture(appDir + "res/notes/guide_colors.png");

		const std::string editorAssetsDir = appDir + "res/editor/";
		ResourceManager::loadTexture(editorAssetsDir + "timeline_tools.png");
		ResourceManager::loadTexture(editorAssetsDir + "note_stats.png");
		ResourceManager::loadTexture(editorAssetsDir + "stage.png");
		const std::string comboAssetsDir = editorAssetsDir + "combo/";
		ResourceManager::loadTexture(comboAssetsDir + "pt.png");
		ResourceManager::loadTexture(comboAssetsDir + "pe.png");
		for (char digit = '0'; digit <= '9'; ++digit)
		{
			ResourceManager::loadTexture(comboAssetsDir + std::string("p") + digit + ".png");
			ResourceManager::loadTexture(comboAssetsDir + std::string("b") + digit + ".png");
		}
		const std::string hudAssetsDir = editorAssetsDir + "hud/";
		ResourceManager::loadTexture(hudAssetsDir + "score/bg.png");
		ResourceManager::loadTexture(hudAssetsDir + "score/bar.png");
		ResourceManager::loadTexture(hudAssetsDir + "score/fg.png");
		for (char rank : std::string("dcbas"))
		{
			ResourceManager::loadTexture(hudAssetsDir + std::string("score/rank/chr/") + rank + ".png");
			ResourceManager::loadTexture(hudAssetsDir + std::string("score/rank/txt/en/") + rank + ".png");
		}
		for (char digit : std::string("0123456789n"))
		{
			ResourceManager::loadTexture(hudAssetsDir + std::string("score/digit/") + digit + ".png");
			ResourceManager::loadTexture(hudAssetsDir + std::string("score/digit/s") + digit + ".png");
			ResourceManager::loadTexture(hudAssetsDir + std::string("life/digit/") + digit + ".png");
			ResourceManager::loadTexture(hudAssetsDir + std::string("life/digit/s") + digit + ".png");
		}
		ResourceManager::loadTexture(hudAssetsDir + "life/bg.png");
		ResourceManager::loadTexture(hudAssetsDir + "life/normal.png");
		ResourceManager::loadTexture(hudAssetsDir + "judge/perfect.png");

		ResourceManager::loadTransforms(appDir + "res/effect/transform.txt");

		Canvas::CustomSkins::loadNoteSkins();

		// Load more languages here
		Localization::loadDefault();
		Localization::load("ja", u8"日本語", appDir + "res/i18n/ja.csv");
		Localization::load("zh-cn", u8"简体中文（中国大陆）", appDir + "res/i18n/zh-cn.csv");
		Localization::load("zh-tw", u8"繁體中文（台灣）", appDir + "res/i18n/zh-tw.csv");
	}

	void Application::setFullScreen(bool fullScreen)
	{
		Application::windowState.fullScreen = config.fullScreen = fullScreen;

		GLFWmonitor* mainMonitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(mainMonitor);
		if (fullScreen)
		{
			if (Application::windowState.maximized)
				glfwSetWindowAttrib(window, GLFW_MAXIMIZED, GLFW_FALSE);

			glfwSetWindowMonitor(window, mainMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
		}
		else
		{
			int width = Application::windowState.maximized ? mode->width : Application::windowState.size.x;
			int height = Application::windowState.maximized ? mode->height : Application::windowState.size.y;

			glfwSetWindowMonitor(
				window,
				NULL,
				Application::windowState.position.x,
				Application::windowState.position.y,
				width,
				height,
				mode->refreshRate
			);
		}
	}

	// CPU time the whole process has used so far, every thread (seconds); --bench reports it
	static double processCpuSeconds()
	{
#ifdef _WIN32
		FILETIME created, exited, kernel, user;
		if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
			return 0.0;
		auto seconds = [](const FILETIME& f) { return ((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7; };
		return seconds(kernel) + seconds(user);
#else
		return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
	}

	// busy CPU time of the whole machine (every process, ffmpeg included), in seconds of one core
	static double systemBusySeconds()
	{
#ifdef _WIN32
		FILETIME idle, kernel, user;
		if (!GetSystemTimes(&idle, &kernel, &user))
			return 0.0;
		auto seconds = [](const FILETIME& f) { return ((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7; };
		return seconds(kernel) + seconds(user) - seconds(idle);	// kernel time includes idle
#else
		return 0.0;
#endif
	}

	// sleeps until `when` with sub-millisecond precision (Windows' plain Sleep is ~15 ms coarse)
	static void waitUntil(std::chrono::steady_clock::time_point when)
	{
		using namespace std::chrono;
#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
		static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		const auto left = when - steady_clock::now();
		if (timer && left > microseconds(1500))
		{
			LARGE_INTEGER due;
			due.QuadPart = -static_cast<LONGLONG>(duration_cast<nanoseconds>(left - microseconds(500)).count() / 100);
			if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0))
				WaitForSingleObject(timer, INFINITE);
		}
		while (steady_clock::now() < when)
			std::this_thread::yield();
#else
		std::this_thread::sleep_until(when);
#endif
	}

	void Application::run()
	{
#ifdef _WIN32
		HWND hwnd = glfwGetWin32Window(window);

		/*
			Override the current GLFW/Imgui window procedure and store it in the GLFW window user pointer
		
			NOTE: For this to be safe, it should be only called AFTER ImGui is initialized
			so that the WndProc ImGui is expecting matches with our own WndProc
		*/
		glfwSetWindowUserPointer(window, (void*)::GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
		::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)wndProc);

		windowState.windowHandle = hwnd;
		windowState.windowTimerId = ::SetTimer(hwnd,
			reinterpret_cast<UINT_PTR>(&windowState.windowTimerId), USER_TIMER_MINIMUM, nullptr);

		::DragAcceptFiles(hwnd, TRUE);
#else
		windowState.windowHandle = nullptr;
#endif

		using FrameClock = std::chrono::steady_clock;
		FrameClock::time_point nextFrame = FrameClock::now();
		FrameClock::time_point idleFrom = nextFrame;
		// --bench: how long frames really take while the chart plays (vsync off, GPU waited for)
		std::vector<double> benchTimes;
		double benchCpuStart = 0.0, benchSystemStart = 0.0;
		FrameClock::time_point benchWallStart{};
		if (automation.bench)
		{
			glfwSwapInterval(0);
			Canvas::Profiler::enabled = true;
			if (automation.benchGpu)
				Canvas::Profiler::sync = [] { glFinish(); };
		}
		while (!glfwWindowShouldClose(window))
		{
			const auto frameStart = FrameClock::now();
			// Idle (no input for a second, nothing playing): 30 frames a second, 10 in the background,
			// instead of drawing flat out. (A plain wait for events would not do: the window's own
			// resize timer posts a message every ~15 ms.) The first input after that is at most a frame late.
			const bool idle = (!automation.active() || automation.benchIdle) && editor && !editor->isBusy() && !intro.active() &&
				glfwGetTime() - lastActivity > 1.0;
			if (idle)
			{
				const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED);
				waitUntil(idleFrom + std::chrono::duration_cast<FrameClock::duration>(std::chrono::duration<double>(focused ? 1.0 / 30.0 : 1.0 / 10.0)));
			}
			idleFrom = FrameClock::now();
			glfwPollEvents();
			update();
			if (automation.bench)
			{
				{
					CANVAS_PROFILE_GPU("swap");
				}
				glFinish();
				// the first frames load fonts, skins and clips: not what we are measuring
				if (automationFrame == 30)
				{
					Canvas::Profiler::entries().clear();
					benchCpuStart = processCpuSeconds();
					benchSystemStart = systemBusySeconds();
					benchWallStart = FrameClock::now();
				}
				if (automationFrame > 30)
					benchTimes.push_back(std::chrono::duration<double, std::milli>(FrameClock::now() - frameStart).count());
				if (automationFrame >= automation.frames + 30)
				{
					std::sort(benchTimes.begin(), benchTimes.end());
					double sum = 0;
					for (double t : benchTimes)
						sum += t;
					const double mean = benchTimes.empty() ? 0.0 : sum / benchTimes.size();
					const double p95 = benchTimes.empty() ? 0.0 : benchTimes[std::min(benchTimes.size() - 1, benchTimes.size() * 95 / 100)];
					// CPU: all threads (decoders included) as a share of one core
					const double wall = std::chrono::duration<double>(FrameClock::now() - benchWallStart).count();
					const double cpu = wall > 0 ? (processCpuSeconds() - benchCpuStart) / wall * 100.0 : 0.0;
					const double system = wall > 0 ? (systemBusySeconds() - benchSystemStart) / wall * 100.0 : 0.0;
					std::printf("bench: %zu frames, mean %.2f ms (%.0f fps), p95 %.2f ms, worst %.2f ms, cpu %.0f%% of a core (whole PC %.0f%%)\n", benchTimes.size(), mean,
						mean > 0 ? 1000.0 / mean : 0.0, p95, benchTimes.empty() ? 0.0 : benchTimes.back(), cpu, system);
					for (const auto& [name, e] : Canvas::Profiler::entries())
						std::printf("  %-22s %7.2f ms/frame  (%ld calls)\n", name.c_str(), e.totalMs / std::max<size_t>(1, benchTimes.size()), e.calls);
					std::fflush(stdout);
					glfwSetWindowShouldClose(window, 1);
				}
			}
			// VSync off with a cap: wait out the rest of the frame (120, 144, 240 Hz...)
			if (!windowState.vsync && config.fpsLimit > 0 && !automation.active())
			{
				const auto frame = std::chrono::duration_cast<FrameClock::duration>(std::chrono::duration<double>(1.0 / config.fpsLimit));
				nextFrame += frame;
				const auto now = FrameClock::now();
				if (nextFrame < now - frame)
					nextFrame = now;	// fell behind (a slow frame): don't rush to catch up
				waitUntil(nextFrame);
			}
			else
				nextFrame = FrameClock::now();
		}

		// an automated run must not overwrite the user's window size and settings
		if (!automation.active())
			writeSettings();
	}

	bool Application::attemptSave()
	{
		return editor && editor->trySave(editor->getWorkingFilename().data());
	}

	bool Application::isEditorUpToDate() const
	{
		return editor->isUpToDate();
	}
}
