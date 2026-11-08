#include "Application.h"
#include "ApplicationConfiguration.h"
#include "ImGui/imgui_impl_glfw.h"
#include "ImGui/imgui_impl_opengl3.h"
#include "../Depends/glad/include/glad/glad.h"
#include "../Depends/GLFW/include/GLFW/glfw3.h"
#include "File.h"
#include "UI.h"
#include "Utilities.h"
#include "IconsFontAwesome5.h"
#include "Colors.h"
#include "Canvas/Sekai/SekaiMode.h"
#include "Canvas/UI/Themes.h"
#include "Canvas/UI/SekaiStyle.h"

namespace MikuMikuWorld
{
	constexpr const char* IMGUI_CONFIG_FILENAME = "imgui_config.ini";

	ImGuiManager::ImGuiManager()
	{

	}

	Result ImGuiManager::initialize(GLFWwindow* window)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		configFilename = Application::getUserDataDir() + IMGUI_CONFIG_FILENAME;

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |=
			ImGuiConfigFlags_DockingEnable |
			ImGuiConfigFlags_ViewportsEnable;
		// the app scales itself from the content scale of the monitor its window is on
		// (Application::update); letting ImGui rescale on top of that made everything grow
		// or shrink twice after a display scale / resolution change
		io.ConfigDpiScaleFonts = false;
		io.ConfigDpiScaleViewports = false;

		io.ConfigWindowsMoveFromTitleBarOnly = true;
		io.ConfigViewportsNoDefaultParent = false;
		io.ConfigViewportsNoAutoMerge = true;
		io.ConfigDebugHighlightIdConflicts = false;
		io.IniFilename = configFilename.c_str();

		if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
			return Result(ResultStatus::Error, "Failed to initialize ImGui GLFW implementation.");

		if (!ImGui_ImplOpenGL3_Init("#version 150"))
			return Result(ResultStatus::Error, "Failed to initialize ImGui OpenGL implementation.");

		setBaseTheme(BaseTheme::DARK);

		return Result::Ok();
	}

	void ImGuiManager::setBaseTheme(BaseTheme theme)
	{
		ImGuiStyle* style = &ImGui::GetStyle();
		this->theme = theme;
		// the SEKAI interface (the default) replaces the older SEKAI Mode palette
		sekaiStyle = false;

		// SEKAI: the game's palette, sprites and drawing hooks
		if (config.interfaceStyle == 0)
		{
			const int index = std::clamp(config.accentColor, 0, static_cast<int>(UI::accentColors.size()) - 1);
			Canvas::SekaiStyle::apply(*style, UI::accentColors[index], theme == BaseTheme::DARK);
			accentColor = config.accentColor;
			if (!config.customTheme.empty())
				Canvas::Themes::apply(config.customTheme, *style);
			Canvas::Themes::applyDensity(*style, config.uiDensity);
			const float neoScale = styleScale * config.uiScale;
			if (neoScale != 1.0f)
				style->ScaleAllSizes(neoScale);
			style->FontScaleMain = config.uiScale;
			style->FontScaleDpi = styleScale;
			appliedThemeKey = themeKey();
			return;
		}
		Canvas::SekaiStyle::uninstall();
		*style = ImGuiStyle();
		style->FramePadding.x		= 4;
		style->FramePadding.y		= 2;
		style->ItemSpacing.x		= 2;
		style->ItemSpacing.y		= 4;
		style->WindowPadding.x		= 6;
		style->WindowRounding		= 4;
		style->WindowBorderSize		= 1;
		style->FrameBorderSize		= 0;
		style->FrameRounding		= 1;
		style->ScrollbarRounding	= 6;
		style->ChildRounding		= 2;
		style->PopupRounding		= 2;
		style->GrabRounding			= 1;
		style->TabRounding			= 1;
		style->ScrollbarSize		= 12;
		style->GrabMinSize			= 8;

		ImVec4* colors = style->Colors;

		if (theme == BaseTheme::LIGHT)
		{
			ImGui::StyleColorsLight();
			colors[ImGuiCol_Text] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
			colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);
			colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.80f, 0.85f);
			colors[ImGuiCol_BorderShadow] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
			colors[ImGuiCol_FrameBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
			colors[ImGuiCol_FrameBgHovered] = ImVec4(0.85f, 0.85f, 0.85f, 0.78f);
			colors[ImGuiCol_FrameBgActive] = ImVec4(0.85f, 0.85f, 0.85f, 0.67f);
			colors[ImGuiCol_TitleBgActive] = ImVec4(0.9f, 0.9f, 0.9f, 1.00f);
			colors[ImGuiCol_MenuBarBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
			colors[ImGuiCol_CheckMark] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
			colors[ImGuiCol_Button] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
			colors[ImGuiCol_ButtonHovered] = ImVec4(0.7f, 0.7f, 0.7f, 1.00f);
			colors[ImGuiCol_ButtonActive] = ImVec4(0.66f, 0.84f, 0.95f, 1.00f);
			colors[ImGuiCol_PlotHistogram] = ImVec4(0.66f, 0.84f, 0.95f, 1.00f);
			colors[ImGuiCol_Header] = ImVec4(0.97f, 0.97f, 0.97f, 1.00f);
			colors[ImGuiCol_HeaderHovered] = ImVec4(0.97f, 0.97f, 0.97f, 1.00f);
			colors[ImGuiCol_HeaderActive] = ImVec4(0.97f, 0.97f, 0.97f, 1.00f);
			colors[ImGuiCol_Separator] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
			colors[ImGuiCol_SeparatorActive] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
			colors[ImGuiCol_Tab] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
			colors[ImGuiCol_TabHovered] = ImVec4(0.99f, 0.48f, 0.88f, 0.80f);
			colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.44f, 0.75f, 1.00f);
			colors[ImGuiCol_TabUnfocused] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
			colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
			colors[ImGuiCol_ChildBg] = ImVec4(0.9f, 0.9f, 0.9f, 1.00f);
			colors[ImGuiCol_PopupBg] = ImVec4(0.9f, 0.9f, 0.9f, 1.00f);
			colors[ImGuiCol_TitleBg] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
			colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
			colors[ImGuiCol_ScrollbarBg] = ImVec4(0.9f, 0.9f, 0.9f, 1.00f);
			colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
			colors[ImGuiCol_TableHeaderBg] = ImVec4(0.9f, 0.9f, 0.9f, 1.00f);
			colors[ImGuiCol_TableRowBg] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
			colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.91f, 0.91f, 0.91f, 1.00f);
		}
		else
		{
			ImGui::StyleColorsDark();
			colors[ImGuiCol_WindowBg] = ImVec4(0.196f, 0.196f, 0.196f, 1.00f);
			colors[ImGuiCol_Border] = ImVec4(0.10f, 0.10f, 0.10f, 0.85f);
			colors[ImGuiCol_BorderShadow] = ImVec4(0.10f, 0.10f, 0.10f, 0.35f);
			colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
			colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.15f, 0.78f);
			colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.67f);
			colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
			colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
			colors[ImGuiCol_CheckMark] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
			colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
			colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
			colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.44f, 0.75f, 1.00f);
			colors[ImGuiCol_PlotHistogram] = ImVec4(0.16f, 0.44f, 0.75f, 1.00f);
			colors[ImGuiCol_Header] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
			colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
			colors[ImGuiCol_HeaderActive] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
			colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
			colors[ImGuiCol_SeparatorActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
			colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
			colors[ImGuiCol_TabHovered] = ImVec4(0.19f, 0.48f, 0.88f, 0.80f);
			colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.44f, 0.75f, 1.00f);
			colors[ImGuiCol_TabUnfocused] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
			colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
			colors[ImGuiCol_ChildBg] = ImVec4(0.196f, 0.196f, 0.196f, 1.00f);
			colors[ImGuiCol_PopupBg] = ImVec4(0.196f, 0.196f, 0.196f, 1.00f);
			colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
			colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
			colors[ImGuiCol_ScrollbarBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
			colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
			colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
			colors[ImGuiCol_TableRowBg] = ImVec4(0.2f, 0.2f, 0.2f, 1.00f);
			colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
		}

		this->theme = theme;
		applyAccentColor(config.accentColor);

		// Canvas layers: SEKAI Mode, a custom theme, density and UI scale (in that order)
		if (sekaiStyle)
			Canvas::Sekai::applyStyle(*style);
		if (!config.customTheme.empty())
			Canvas::Themes::apply(config.customTheme, *style);
		Canvas::Themes::applyDensity(*style, config.uiDensity);

		const float scale = styleScale * config.uiScale;
		if (scale != 1.0f)
			style->ScaleAllSizes(scale);
		style->FontScaleMain = config.uiScale;
		style->FontScaleDpi = styleScale;
		appliedThemeKey = themeKey();
	}

	std::string ImGuiManager::themeKey() const
	{
		const int index = std::clamp(config.accentColor, 0, static_cast<int>(UI::accentColors.size()) - 1);
		const ImVec4 accent = UI::accentColors[index];
		return IO::formatString("%d|%d|%s|%d|%.3f|%.3f|%d|%d|%.3f,%.3f,%.3f", static_cast<int>(config.baseTheme), config.sekaiMode ? 1 : 0,
			config.customTheme.c_str(), config.uiDensity, config.uiScale, styleScale, config.interfaceStyle, config.accentColor,
			accent.x, accent.y, accent.z);
	}

	void ImGuiManager::shutdown()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiManager::begin()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiManager::setDpiScale(float dpiScale)
	{
		dpiScale = std::clamp(dpiScale, 0.5f, 4.0f);
		if (std::abs(dpiScale - styleScale) < 0.001f)
			return;
		// sizes are rebuilt from the base style at the new scale (setBaseTheme reads styleScale)
		styleScale = dpiScale;
		setBaseTheme(theme);
	}

	void ImGuiManager::draw(GLFWwindow* window)
	{
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backupCurrentContext = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(window);
		}
	}

	void ImGuiManager::loadFont(const std::string& filename, float size)
	{
		if (!IO::File::exists(filename))
			return;

		ImGui::GetIO().Fonts->AddFontFromFileTTF(filename.c_str(), size, nullptr);
	}

	void ImGuiManager::loadIconFont(const std::string& filename, int start, int end, float size)
	{
		if (!IO::File::exists(filename))
			return;

		static ImFontConfig fontConfig{};
		fontConfig.MergeMode = true;
		fontConfig.GlyphMinAdvanceX = 13.0f;
		fontConfig.PixelSnapH = false;
		static const ImWchar iconRanges[] = { (ImWchar)start, (ImWchar)end, 0 };
		ImGui::GetIO().Fonts->AddFontFromFileTTF(filename.c_str(), size, &fontConfig, iconRanges);
	}

	bool ImGuiManager::fontsOutdated() const
	{
		return builtFontStyle != config.interfaceStyle;
	}

	void ImGuiManager::buildFonts(float dpiScale)
	{
		// clear existing fonts on rebuild
		ImGuiIO& io = ImGui::GetIO();
		if (!io.Fonts->Fonts.empty())
			io.Fonts->Clear();

		io.FontDefault = nullptr;

		// fonts are built at their base size once; style.FontScaleDpi scales them to the monitor
		// (Dear ImGui 1.92 rasterises glyphs at the size they are drawn)
		(void)dpiScale;
		const std::string fonts = Application::getAppDir() + "res/fonts/";
		const std::string pack = Application::getAppDir() + "res/sekai/fonts/";
		const std::string noto = fonts + "NotoSansCJK-Regular.ttc";
		auto addFallback = [&](float size) {
			// glyphs the game font lacks (Chinese UI text, symbols) come from Noto
			ImFontConfig merge{};
			merge.MergeMode = true;
			if (IO::File::exists(noto))
				io.Fonts->AddFontFromFileTTF(noto.c_str(), size, &merge);
		};

		// the SEKAI interface reads like the game: FOT-Rodin DB for text, EB for titles
		builtFontStyle = config.interfaceStyle;
		const bool gameFonts = config.interfaceStyle == 0 && IO::File::exists(pack + "FOT-RodinNTLGPro-DB.otf");
		if (gameFonts)
		{
			loadFont(pack + "FOT-RodinNTLGPro-DB.otf", 16);
			addFallback(16);
		}
		else
			loadFont(noto, 16);
		loadIconFont(fonts + "fa-solid-900.ttf", ICON_MIN_FA, ICON_MAX_FA, 12);

		Canvas::SekaiStyle::headerFont = nullptr;
		const std::string headerPath = gameFonts ? pack + "FOT-RodinNTLGPro-EB.otf" : fonts + "MPLUSRounded1c-ExtraBold.ttf";
		if (IO::File::exists(headerPath))
		{
			Canvas::SekaiStyle::headerFont = io.Fonts->AddFontFromFileTTF(headerPath.c_str(), 18, nullptr);
			if (Canvas::SekaiStyle::headerFont)
				addFallback(18);
		}

		fontDpiScale = 1.0f;
		titleFont = nullptr;
		const std::string wanted = titleFontPath;
		titleFontPath.clear();
		updateTitleFont(wanted);
	}

	void ImGuiManager::updateTitleFont(const std::string& path)
	{
		if (path == titleFontPath)
			return;

		ImGuiIO& io = ImGui::GetIO();
		if (titleFont)
		{
			io.Fonts->RemoveFont(titleFont);
			titleFont = nullptr;
		}

		titleFontPath = path;
		if (!path.empty() && IO::File::exists(path))
			titleFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 44, nullptr);
	}

	void ImGuiManager::initializeLayout()
	{
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBackground;

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 viewportOffset{ 0, UI::toolbarHeight() };
		ImGui::SetNextWindowPos(viewport->WorkPos + viewportOffset);
		ImGui::SetNextWindowSize(viewport->WorkSize - viewportOffset);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("InvisibleWindow", nullptr, windowFlags); // This is basically the background window that contains all the dockable windows
		ImGui::PopStyleVar(3);

		std::string dockStrId{ "InvisibleWindowDockSpace" };

		ImGuiID dockSpaceId = ImGui::GetID(dockStrId.c_str());
		if (layoutResetRequested)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			layoutResetRequested = false;
		}

		if (!ImGui::DockBuilderGetNode(dockSpaceId))
		{
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize - viewportOffset);

			ImGuiID dockMainId = dockSpaceId;
			ImGuiID midRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
			ImGuiID bottomRightId = ImGui::DockBuilderSplitNode(midRightId, ImGuiDir_Down, 0.3f, nullptr, &midRightId);

			ImGui::DockBuilderDockWindow("###notes_timeline", dockMainId);
			ImGui::DockBuilderDockWindow("###score_preview", dockMainId);
			ImGui::DockBuilderDockWindow("###chart_properties", midRightId);
			ImGui::DockBuilderDockWindow("###chart_issues", dockMainId);
			ImGui::DockBuilderDockWindow("###song_difficulties", midRightId);
			ImGui::DockBuilderDockWindow("###options", bottomRightId);
			ImGui::DockBuilderDockWindow("###layers", bottomRightId);
			ImGui::DockBuilderDockWindow("###presets", bottomRightId);

			ImGui::DockBuilderFinish(dockMainId);
		}

		ImGui::DockSpace(dockSpaceId, ImVec2(), ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::End();
	}

	void ImGuiManager::applyAccentColor(int colIndex)
	{
		if (Canvas::SekaiStyle::active())
		{
			// the SEKAI interface derives its whole palette from the accent
			accentColor = colIndex;
			setBaseTheme(theme);
			return;
		}

		if (sekaiStyle)
		{
			// SEKAI Mode has its own palette
			accentColor = colIndex;
			return;
		}

		ImVec4* colors = ImGui::GetStyle().Colors;
		const ImVec4 color = UI::accentColors[colIndex];
		const ImVec4 darkColor = generateDarkColor(color);
		const ImVec4 lightColor = generateHighlightColor(color);

		colors[ImGuiCol_SliderGrab] = color;
		colors[ImGuiCol_SliderGrabActive] = darkColor;
		colors[ImGuiCol_ButtonActive] = darkColor;
		colors[ImGuiCol_SeparatorHovered] = lightColor;
		colors[ImGuiCol_TabHovered] = lightColor;
		colors[ImGuiCol_TabActive] = color;
		colors[ImGuiCol_CheckMark] = color;
		colors[ImGuiCol_PlotHistogram] = color;
		colors[ImGuiCol_PlotHistogramHovered] = darkColor;

		accentColor = colIndex;
	}
}
