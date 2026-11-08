#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Score Preview Animation: keyframes that move, scale, turn and fade parts of the Score Preview (each
// HUD piece, the stage, the whole picture), plus effects at chart ticks (shake, flash, fade). It lives
// in the chart (Score::animation, saved in the .ce extension block), so undo, the preview and video
// export all see the same thing.
namespace Canvas::Animation
{
	enum class Ease : int { Linear, In, Out, InOut, Back, Bounce, Step, Count };
	const char* easeName(Ease ease);
	// 0..1 -> 0..1 (Back overshoots past 1 on the way)
	float ease(Ease ease, float t);

	struct Target
	{
		const char* id;			// stored in the chart
		const char* label;		// string key for the UI
		bool hud;				// a HUD piece (drawn by the live overlay) vs. the stage / whole screen
	};
	const std::vector<Target>& targets();

	struct Key
	{
		int tick{};
		float x{}, y{};				// offset in 1080p pixels, right / down
		float scale{ 1 };
		float rotation{};			// degrees, clockwise
		float opacity{ 1 };
		Ease ease{ Ease::InOut };	// how the track gets here from the key before
	};

	struct Track
	{
		std::string target;
		std::vector<Key> keys;		// sorted by tick, one key per tick
	};

	enum class EffectKind : int { Shake, Flash, Fade, Count };
	const char* effectName(EffectKind kind);

	struct Effect
	{
		EffectKind kind{};
		int tick{};
		float duration{ 0.5f };			// seconds
		float strength{ 14.0f };		// shake: 1080p pixels; fade: how opaque the colour ends up (0..1)
		float speed{ 28.0f };			// shake: wobbles per second
		uint32_t color{ 0xffffffff };	// flash / fade colour, ImGui order (0xAABBGGRR)
	};

	struct PreviewAnimation
	{
		std::vector<Track> tracks;
		std::vector<Effect> effects;	// sorted by tick

		bool empty() const;
		const Track* track(const std::string& target) const;
		Track& trackFor(const std::string& target);
		// adds or replaces the key at key.tick
		void setKey(const std::string& target, const Key& key);
		void sort();
	};

	struct Transform
	{
		float x{}, y{}, scale{ 1 }, rotation{}, opacity{ 1 };
		bool identity() const;
	};

	Transform evaluate(const PreviewAnimation& anim, const std::string& target, int tick);

	using TickToSeconds = std::function<double(int tick)>;

	// what the effects add at `seconds`: a shake offset for the whole picture (1080p pixels) and the
	// colours laid over it (fade underneath, flash on top; alpha 0 = nothing)
	struct EffectState
	{
		float shakeX{}, shakeY{};
		uint32_t fade{}, flash{};
	};
	EffectState effectsAt(const PreviewAnimation& anim, double seconds, const TickToSeconds& toSeconds);

	// the screen's transform with the shake folded in
	Transform screenAt(const PreviewAnimation& anim, int tick, double seconds, const TickToSeconds& toSeconds);

	// JSON for the chart file; fromJson ignores what it does not know
	std::string toJson(const PreviewAnimation& anim);
	bool fromJson(const std::string& text, PreviewAnimation& anim);
}
