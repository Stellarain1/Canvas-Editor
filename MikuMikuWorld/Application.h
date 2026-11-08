#pragma once
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define IMGUI_DEFINE_MATH_OPERATORS

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "ScoreEditor.h"
#include "ImGuiManager.h"
#include "Canvas/Sekai/IntroOverlay.h"
#include "Canvas/Sekai/ModePicker.h"

#ifdef _WIN32
LRESULT CALLBACK wndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif

#ifdef __APPLE__
// main.cpp (Objective-C++): the Dock icon; empty path = the bundle's icon
void setMacDockIcon(const std::string& path);
// main.cpp: points the app menu's "About Canvas Editor" at Canvas's own About window (the standard
// panel could not be closed); false until GLFW has built the menu
bool hookMacAboutMenu();
#endif

namespace MikuMikuWorld
{
	class Result;

	struct WindowState
	{
		bool resetting = false;
		bool vsync = true;
		bool showPerformanceMetrics = false;
		bool maximized = false;
		bool closing = false;
		bool shouldPickScore = false;
		bool dragDropHandled = true;
		bool windowDragging = false;
		bool fullScreen = false;
		float lastDpiScale = 0.0f;
		void* windowHandle{ 0 };
		Vector2 position{};
		Vector2 size{};
#ifdef _WIN32
		UINT_PTR windowTimerId{};
#else
		uintptr_t windowTimerId{};
#endif
	};

	// Command-line automation, mostly for checking the UI without a person at the keyboard:
	//   CanvasEditor --screenshot out.png [--frames 120] [--size 1600x900] [--focus score_preview]
	//                [--tick 1920] [--play] [--settings] [--open autofix|repeat|interpolate] [chart files...]
	// renders `frames` frames, saves the window to out.png and quits.
	struct AutomationOptions
	{
		std::string screenshot;
		int frames{ 120 };
		int width{ 0 }, height{ 0 };
		std::string focus;			// window to bring to front ("score_preview", "chart_issues"...)
		int tick{ -1 };				// cursor position once the chart is open
		int ticksAfterEnd{ -1 };	// --tick end+N: N ticks after the last note
		std::string music;			// --music file
		int liveMode{ -1 };			// --mode solo|coop|carnival
		bool play{ false };			// start playback (screenshot shows the preview moving)
		bool bench{ false };
		bool benchGpu{ false };
		bool benchIdle{ false };	// --bench-idle: paused, nobody touching it (the idle frame rate is allowed)		// --bench-gpu: also time GPU work per section (waits for the GPU, so slower overall)		// --bench: play with vsync off for --frames frames, print frame times, quit
		bool settings{ false };		// open the settings window
		float settingsScroll{ 0 };	// --settings-scroll N: scroll the Canvas settings tab down N pixels
		std::string openDialog;		// --open autofix | repeat | interpolate: bring that dialog up
		std::string exportVideo;	// render the preview video to this file and quit
		int importCardSlot{ -1 };	// --import-card N link: fetch a sekai.best card into slot N (1-5) first
		std::string importCardLink;
		bool enabled{ false };		// set once at startup: the run is automated even after the screenshot is taken
		bool active() const { return enabled; }

		// Pulls the options out of `args`; what is left are files to open.
		static AutomationOptions parse(std::vector<std::string>& args);
	};

	class Application
	{
	private:
		int automationFrame{ 0 };
		std::string appliedIconPath;
		bool appIconLoaded{ false };
		void runAutomation();
		GLFWwindow* window{ nullptr };
		std::unique_ptr<ScoreEditor> editor;
		std::unique_ptr<ImGuiManager> imgui;
		UnsavedChangesDialog unsavedChangesDialog;
		Canvas::Sekai::IntroOverlay intro;
		Canvas::Sekai::ModePicker modePicker;
		bool introShown{ false };

		void startIntro();

		bool initialized{ false };
		double lastActivity{ 0.0 };	// last mouse / keyboard input (glfwGetTime), for the idle frame rate
		bool shouldPickScore{ false };
		std::string language;

		std::vector<std::string> pendingOpenFiles;

		static std::string version;
		static std::string appDir;

		Result initOpenGL();
		std::string getVersion();

	public:
		static WindowState windowState;
		// texture of the app icon (Settings > Canvas > App Icon, else res/canvas_icon.png) for the menu bar logo
		static inline unsigned int appIconTexture = 0;
		static AutomationOptions automation;
		static std::string pendingLoadScoreFile;
		// Help > Replay Intro
		static inline bool introReplayRequested = false;
		// Chart > Charting Mode > Choose...
		static inline bool modePickerRequested = false;
		// Help > Check for Updates...
		static inline bool aboutRequested = false;	// the macOS app menu's About item

		Application();

		Result initialize(const std::string& root);
		void run();
		void update();
		void appendOpenFile(const std::string& filename);
		void handlePendingOpenFiles();
		void readSettings();
		void writeSettings();
		void loadResources();
		// window / taskbar icon and the menu bar logo from config.appIconPath (or res/canvas_icon.png)
		void loadAppIcon();
		void dispose();
		void setFullScreen(bool fullScreen);
		bool attemptSave();
		bool isEditorUpToDate() const;

		GLFWwindow* getGlfwWindow() { return window; }
		ScoreEditor* getEditor() { return editor.get(); }

		static const std::string& getAppDir();
		static const std::string& getUserDataDir();
		static const std::string& getAppVersion();
	};
}
