#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../ImGui/imgui.h"
#include <string>

// The SEKAI interface (default since 1.2.1): Canvas dressed like Project SEKAI's own menus and
// Score Maker. White and gray panels, the game's colours (#444466 text, Miku mint accents,
// #EBEBF2 bases), buttons / inputs / tabs / dialogs drawn with the game's own 9-slice sprites
// from res/sekai (falling back to drawn shapes without the asset pack), the SEKAI sky with its
// floating circles and triangles behind the timeline, and the game's pop-up feel for dialogs
// (see SekaiMode for the open animation and sounds). Settings > Theme > Interface Style
// switches to Classic (the MikuMikuWorld look).
//
// It paints through the skin hooks added to the bundled Dear ImGui (GImGuiSkin).
namespace Canvas::SekaiStyle
{
	struct Palette
	{
		bool dark{ false };
		ImVec4 accent, accentDark, accentLight, onAccent;
		ImVec4 base, panel, card, frame, border, line, text, textDim, pink;
	};

	void apply(ImGuiStyle& style, const ImVec4& accent, bool dark);
	void uninstall();
	// Frees the textures the style cached (call while the GL context still exists).
	void releaseGlResources();
	bool active();
	const Palette& palette();

	ImU32 col(const ImVec4& color, float alpha = 1.0f);
	ImVec4 mix(const ImVec4& a, const ImVec4& b, float t);
	float luminance(const ImVec4& color);

	// fonts: FOT-Rodin EB (titles) from the asset pack, else M PLUS Rounded 1c; may be null
	inline ImFont* headerFont = nullptr;

	// ---- game sprites (res/sekai), packed into one texture on first use --------------------------
	struct Sprite
	{
		ImTextureID texture{};
		ImVec2 uv0, uv1;
		ImVec2 size;		// pixels
		ImVec4 border;		// 9-slice insets in pixels: left, top, right, bottom
		ImVec2 contentUv0, contentUv1, contentSize;	// the opaque part (transparent margins trimmed)
		bool valid() const { return size.x > 0; }
	};
	// "ui/btn_round_h56_wh", "notes/notes_icon_normal" ... (null when the pack is not there)
	const Sprite* sprite(const std::string& name);
	bool hasAssetPack();
	// Stretches the sprite over [a, b] keeping its corners; `cornerScale` scales the corners.
	void nineSlice(ImDrawList* drawList, const Sprite& sprite, ImVec2 a, ImVec2 b, ImU32 tint, float cornerScale);
	// Draws the sprite fitted (aspect kept) and centred in [a, b].
	void spriteFit(ImDrawList* drawList, const Sprite& sprite, ImVec2 a, ImVec2 b, ImU32 tint = IM_COL32_WHITE);
	// An image button showing a game sprite (note tools); true when clicked.
	bool spriteButton(const char* id, const std::string& spriteName, ImVec2 size, bool selected, bool enabled = true, bool flipX = false);
	// a toolbar button with a font icon, the same size and look as spriteButton (the icon's ink is
	// centred, not its advance box)
	bool glyphButton(const char* id, const char* glyph, ImVec2 size, bool selected, bool enabled = true);

	// The SEKAI sky behind the timeline: bg_sekai with drifting circles and triangles.
	// avoidX0..avoidX1: the lane column, where the shapes fade out
	void drawBackdrop(ImDrawList* drawList, ImVec2 min, ImVec2 max, double time, float avoidX0 = 0, float avoidX1 = 0);

	// App logo: the custom icon texture when one is set, else Canvas' SEKAI prism.
	void drawLogo(ImDrawList* drawList, ImVec2 min, float size, ImTextureID customIcon = ImTextureID{});

	// Toolbar groups on white rounded bases, like the Score Maker's button rows.
	class ToolbarBar
	{
	public:
		void begin();
		void group();
		void endGroup();
		void end();

	private:
		ImDrawList* drawList{ nullptr };
		ImVec2 groupStart{};
		bool inGroup{ false };
	};

	void sectionTitle(const char* text);
	void badge(const char* text, const ImVec4& color);
}
