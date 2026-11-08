#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../ImGui/imgui.h"
#include "../Core/PreviewAnimation.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	class ScoreContext;
	class Texture;
	struct Score;
}

// The live UI drawn over the Score Preview (and burnt into exported videos): score, life,
// combo, judgement, skill cut-ins, FEVER, ALL PERFECT, and the co-op / Cheerful Carnival
// extras. Positions follow the game's 1920x1080 layout (taken from pjsekai-overlay-APPEND's
// AviUtl project), scaled to the preview the way the game letterboxes.
//
// Everything is skinnable: a score skin is a folder with skin.json (element positions, scales,
// colours, texts) and images under the same relative names as res/editor/hud. Whatever a skin
// leaves out comes from the built-in skin, and the built-in effects are drawn, so nothing
// needs game assets. With a pjsekai-overlay-APPEND folder set, its official art is used.
namespace Canvas::Preview
{
	enum class LiveMode { Solo = 0, Coop = 1, Carnival = 2 };

	// pixels per 1080p HUD pixel: the game's own scale, the 16:9 frame fitted into the picture. Score,
	// life, combo and skill all scale together, so a small preview gets a small HUD (not a cramped big one)
	inline float hudScale(float width, float height) { return width / 1920.0f < height / 1080.0f ? width / 1920.0f : height / 1080.0f; }

	struct OverlayOptions
	{
		bool score{ true }, life{ true }, combo{ true }, judge{ true };
		bool skills{ true }, fever{ true }, allPerfect{ true };
		bool scoreDelta{ true };	// the "+800" that pops out beside the score
		bool cutins{ false };		// the card cut-in (and its voice) when a skill goes off
		bool apCombo{ true };		// the rainbow ALL PERFECT combo (an auto-played preview is all perfect)
		bool superFever{ true };	// FEVER ends in SUPER FEVER!! (the gauge filled to the top), else FEVER!
		bool ui280{ false };		// the game's UI before 3.0 (what a 2.8.0 client shows): APPEND's v1 skin
		bool intro{ true };			// the start animation over the first seconds of the chart (IntroOverlay.h)
		std::string introDifficulty;	// "MASTER" etc. for the intro bar; empty = unknown
		int introLevel{ 0 };
		LiveMode mode{ LiveMode::Solo };
	};

	// engine coordinates (lane x in -6..6, depth with 1 = judgement line, 0 = vanishing point)
	// to a pixel inside the frame; empty = the lane edges are not known
	using Projector = std::function<ImVec2(float laneX, float depth)>;

	struct SkinElement
	{
		float x{ 0 }, y{ 0 }, scale{ 1 };
		bool visible{ true };
	};

	class ScoreSkin
	{
	public:
		std::string name;			// shown in the settings
		std::string folder;			// skin folder; empty for the built-in skin
		std::string overlayDir;		// pjsekai-overlay-APPEND root when its art is used
		std::map<std::string, SkinElement> elements;
		std::map<std::string, ImVec4> colors;
		std::map<std::string, std::string> texts;

		// Defaults of every element and colour (the game's layout).
		static const std::map<std::string, SkinElement>& defaultElements();
		static const std::map<std::string, ImVec4>& defaultColors();
		static const std::map<std::string, std::string>& defaultTexts();

		SkinElement element(const std::string& key) const;
		ImVec4 color(const std::string& key) const;
		std::string text(const std::string& key) const;

		// "score/bg.png" etc.: the skin's file, else the overlay folder's, else res/editor/hud.
		std::string file(const std::string& relative) const;
		// Texture of file(relative), loaded once; null when there is no such image anywhere.
		const MikuMikuWorld::Texture* texture(const std::string& relative) const;

		bool load(const std::string& folder);
		bool save() const;
		void clearCache() const;

	private:
		mutable std::map<std::string, std::shared_ptr<MikuMikuWorld::Texture>> cache;
		mutable std::map<std::string, std::string> fileCache;
	};

	// The skin selected in the settings (config.pvScoreSkin / config.overlayAssetsDir).
	ScoreSkin& activeSkin();
	// Folder that holds user skins (<user data>/score_skins).
	std::string skinsFolder();
	std::vector<std::string> listSkins();
	// Copies the current skin (its json and every image it uses) into a new folder.
	bool createSkinFromCurrent(const std::string& name, std::string& error);
	// The skin settings changed (or were edited in place): drop cached textures.
	void reloadSkins();
	// Frees every GL texture and movie the overlay cached. Call before the GL context goes away: a
	// static destructor after exit() would call into a context that is already gone.
	void releaseGlResources();

	// When a chart's skills / FEVER / ALL PERFECT happen, in chart seconds.
	struct EffectTimes
	{
		std::vector<double> skills;
		double feverChance{ -1 }, feverStart{ -1 }, feverEnd{ -1 };
		double lastNote{ -1 };
		double allPerfect{ -1 };	// when the ALL PERFECT appears
	};
	EffectTimes effectTimes(const MikuMikuWorld::Score& score);
	inline constexpr double SKILL_DURATION = 5.0;
	inline constexpr double ALL_PERFECT_DELAY = 0.6;
	inline constexpr double ALL_PERFECT_DURATION = 5.0;
	// the picture fades to black from here (seconds after ALL PERFECT!! shows), over this long
	inline constexpr double AP_FADE_START = 6.0;
	inline constexpr double AP_FADE_LENGTH = 4.0;

	// Draws the whole live UI for the current tick of `context` into the frame at pos/size.
	void drawLiveOverlay(ImDrawList* drawList, ImVec2 pos, ImVec2 size, const MikuMikuWorld::ScoreContext& context,
		const OverlayOptions& options, const Projector& projector);

	// FEVER's glowing frame round the whole editor window while FEVER runs (preview only).
	void drawEditorFeverFrame(ImDrawList* drawList, ImVec2 min, ImVec2 max, const MikuMikuWorld::ScoreContext& context, const OverlayOptions& options);

	// Score Preview Animation on ImGui geometry. A transform turns and scales round `pivot`, then moves by
	// x/y * unit (unit = display pixels per 1080p pixel); opacity scales the vertex alpha.
	ImVec2 animatePoint(ImVec2 p, const Animation::Transform& t, ImVec2 pivot, float unit);
	// applies `t` to what `dl` drew from vertex `from` on; no pivot = the middle of what was drawn
	void animateVertices(ImDrawList* dl, int from, const Animation::Transform& t, float unit, const ImVec2* pivot = nullptr);
	// the chart's flash / fade colours over the frame (drawn even with the HUD off)
	void drawAnimationEffects(ImDrawList* dl, ImVec2 pos, ImVec2 size, const MikuMikuWorld::ScoreContext& context);

	// pjsekai-overlay-APPEND's movie clips (ALL PERFECT, FEVER) drawn additively when its folder
	// is set; otherwise the built-in versions are drawn. `waitForFrames` for video export.
	void setMovieFrameWaiting(bool waitForFrames);
}
