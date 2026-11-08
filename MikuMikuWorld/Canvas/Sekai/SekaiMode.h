#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../ImGui/imgui.h"
#include <map>
#include <random>
#include <string>

// SEKAI Mode: Canvas dressed like SEKAI-Stories (lezzthanthree/SEKAI-Stories).
//  - colours from src/css/_colors.scss, rounded pill buttons and soft light panels
//  - "open" / "close" sounds for dialogs and "select" for clicks (src/hooks/useAudioManager)
//  - dialogs pop open by scaling from 0 to 100% in 0.1 s (the .window open_window keyframes)
//  - SEKAI transition triangles when a chart is opened, and SEKAI-Stories backgrounds behind
//    the timeline when the assets folder is set
namespace Canvas::Sekai
{
	// src/css/_colors.scss
	namespace Palette
	{
		inline constexpr ImU32 White = IM_COL32(0xff, 0xff, 0xff, 0xff);
		inline constexpr ImU32 LightGray = IM_COL32(0xeb, 0xeb, 0xf2, 0xff);
		inline constexpr ImU32 Black = IM_COL32(0x44, 0x44, 0x66, 0xff);
		inline constexpr ImU32 LightMiku = IM_COL32(0x77, 0xee, 0xdd, 0xff);
		inline constexpr ImU32 DarkMiku = IM_COL32(0x00, 0xcc, 0xbb, 0xff);
		inline constexpr ImU32 LightPink = IM_COL32(0xff, 0x77, 0xac, 0xff);
		inline constexpr ImU32 Orange = IM_COL32(0xff, 0xbb, 0x00, 0xff);
		inline constexpr ImU32 DarkPink = IM_COL32(0xff, 0x55, 0x99, 0xff);
		inline constexpr ImU32 Red = IM_COL32(0xff, 0x2b, 0x2b, 0xff);
		inline constexpr ImU32 WhiteClicked = IM_COL32(0xa1, 0xf4, 0xec, 0xff);
		inline constexpr ImU32 LightMikuClicked = IM_COL32(0xe3, 0xfc, 0xf8, 0xff);
		inline constexpr ImU32 OptionBorder = IM_COL32(0xd1, 0xd1, 0xd1, 0xff);

		ImVec4 vec(ImU32 color, float alpha = 1.0f);
	}

	// Replaces colours, rounding and spacing of `style` with the SEKAI-Stories look.
	void applyStyle(ImGuiStyle& style);

	// Something the player tapped that Canvas cannot see as a click (buttons inside the timeline and
	// the preview, which are otherwise left silent because clicking notes must not click).
	void noteTap();
	// A slider / drag box / scrollbar moved; plays the slide sound (a few times a second at most).
	void noteSlide();

	class ModeHooks
	{
	public:
		// Call once per frame after all windows were submitted and before ImGui::Render().
		void endFrame(bool sounds, float volume, bool animateDialogs);

	private:
		struct Popup
		{
			double since{ 0 };
			bool modal{ false };
		};
		std::map<ImGuiID, Popup> openPopups;
		double lastSlide{ -1 };
	};

	ModeHooks& hooks();

	// A random SEKAI-Stories background (empty when the assets folder is not set).
	std::string pickBackground(std::mt19937& rng);
}
