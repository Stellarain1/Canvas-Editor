#pragma once
#include "UI.h"
#include <string>

struct GLFWwindow;

namespace MikuMikuWorld
{
	class Result;

	class ImGuiManager
	{
	private:
		std::string configFilename{};
		
		BaseTheme theme{};
		int accentColor{ 1 };
		float styleScale{ 1.0f };
		bool sekaiStyle{ false };
		std::string appliedThemeKey{};
		std::string titleFontPath{};
		float fontDpiScale{ 1.0f };
		int builtFontStyle{ -1 };

	public:
		// set from anywhere (View > Reset Layout); the dock layout is rebuilt next frame
		static inline bool layoutResetRequested = false;

		// Display font for the intro / SEKAI Mode titles: FOT-Rodin from the SEKAI-Stories
		// folder when it is configured and SEKAI Mode is on, otherwise null (use the UI font).
		static inline ImFont* titleFont = nullptr;

		ImGuiManager();

		Result initialize(GLFWwindow* window);
		void initializeLayout();
		void shutdown();
		void begin();
		void loadFont(const std::string& filename, float size);
		void loadIconFont(const std::string& filename, int start, int end, float size);
		void buildFonts(float dpiScale = 1.0f);
		// fonts follow the interface style (the game font for SEKAI): rebuild outside a frame when it changed
		bool fontsOutdated() const;
		// Adds / drops the title font; call outside of a frame. `path` empty = none.
		void updateTitleFont(const std::string& path);
		void draw(GLFWwindow* window);

		void setBaseTheme(BaseTheme theme);
		// content scale of the monitor the window is on (1 = 100%); rebuilds the style when it changes
		void setDpiScale(float dpiScale);
		inline float getDpiScale() const { return styleScale; }
		void applyAccentColor(int colIndex);

		inline constexpr BaseTheme getBaseTheme() const { return theme; }
		inline constexpr bool isSekaiStyle() const { return sekaiStyle; }
		// true when the theme settings (base, SEKAI Mode, custom theme, density, scale) changed
		inline bool themeOutdated() const { return themeKey() != appliedThemeKey; }
		std::string themeKey() const;
		inline constexpr int getAccentColor() const { return accentColor; }
	};
}