#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../ImGui/imgui.h"
#include <string>

namespace MikuMikuWorld
{
	class ScoreContext;
}

// The start animation of the chart preview: jacket, difficulty, title and credits on a plain
// purple-grey screen, like TootieJin's pjsekai-overlay-APPEND intro (main_jp.exo for the modern
// HUD, v1-skin_jp.exo for the 2.8.0 one). All timing is his: 60 fps frames, the intro is frames
// 1..300, so 5 seconds, and the chart starts right after it.
//
// The drawing only needs ImGui, so the tests can build this file on its own.
namespace Canvas::Preview
{
	struct OverlayOptions;

	inline constexpr float INTRO_SECONDS = 5.0f;

	struct IntroInfo
	{
		std::string title, artist, charter;
		std::string difficulty;		// "MASTER"; empty = no difficulty bar text
		int level{ 0 };				// play level, shown after the difficulty when set
		std::string extra;			// optional label (modern HUD, above the title)
		std::string credits[2];		// lines under the title; an empty line is not drawn
		ImTextureID jacket{};		// 0 = no jacket (a dark square is drawn)
		// art from assets/intro (0 = skipped)
		ImTextureID gradient{}, sheet{}, person{};
		ImVec2 sheetSize{ 70, 47 }, personSize{ 25, 34 }, gradientSize{ 1334, 750 };
		// backgrounds (assets/intro/background.png, background-v1.png), the tile of the difficulty
		// ({difficulty}_bg.png) and start_bg.png, which the 2.8.0 intro tiles over the screen
		ImTextureID background{}, tile{}, startBg{};
		ImVec2 backgroundSize{ 1024, 1024 };
		ImFont* font{};				// null = ImGui's current font
	};

	// Where the intro is at: frames of the exo at 60 fps.
	struct IntroState
	{
		float frame{};			// 0..300
		float flash{};			// white flash over everything: 1 -> 0 during the first second
		float fade{ 1 };		// the content's opacity: 1 until frame 270, 0 at 300
		float drift{};			// modern: the difficulty tile slides out from behind the jacket, 0 -> 1 over 112 frames
		float tilt{};			// 2.8.0: the tile's turn in degrees (0 -> -4.6 during frames 46..60)
		float sweep{};			// the light streak: 0 -> 1 over each 120 frame sweep (frames 61..180, 181..300), -1 = none
	};
	IntroState introState(float seconds);

	// Draws the intro over the frame at pos/size, `seconds` after it started. Returns true while it
	// covers the whole frame (the caller can skip the rest of the HUD).
	bool drawIntro(ImDrawList* dl, ImVec2 pos, ImVec2 size, float seconds, const IntroInfo& info, bool ui280);

	// The intro of the chart at the cursor: plays over the first INTRO_SECONDS of the chart, only
	// when options.intro is on. Fills IntroInfo from the score's metadata, and the difficulty
	// from options (or setIntroDifficulty). True = the frame is covered.
	bool drawIntroFor(ImDrawList* dl, ImVec2 pos, ImVec2 size, const MikuMikuWorld::ScoreContext& context, const OverlayOptions& options);

	// The editor tells the overlay which difficulty is open (SongWindow), for charts that do not
	// carry it. OverlayOptions::introDifficulty wins when it is set.
	void setIntroDifficulty(const std::string& difficulty, int level);

	// An exported video with the intro starts this many seconds before the chart does, so the chart
	// (and its music) only begins once the intro has played out
	inline constexpr double INTRO_LEAD_IN = 9.0;
	// The exporter gives the intro its own clock (video time, 0 at the first frame) because chart time
	// stands still at 0 during the lead-in. Negative = use the chart's time (the normal case).
	void setIntroClock(double seconds);
}
