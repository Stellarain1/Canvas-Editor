#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../ImGui/imgui.h"
#include <random>
#include <vector>

// Port of SEKAI-Stories' "SEKAI transition" (src/utils/SekaiTransition.ts and
// src/model/SekaiTransitionParticles.ts, GPL-3.0): a burst of magenta / cyan / yellow triangles
// flying in from the bottom-right, filled or outlined, spinning and slowing down, over a white
// flash. SEKAI-Stories draws it with PIXI on a 1920x1080 stage; here it is drawn with ImGui into
// any rectangle, and every size / speed is scaled from that 1920x1080 reference.
namespace Canvas::Sekai
{
	struct TransitionConfig
	{
		// the original CONFIG values
		int maxTriangles{ 100 };
		float sizeMin{ 50 }, sizeMax{ 200 };
		float velocityXMin{ -5 }, velocityXMax{ -15 };	// px per 60 fps frame
		float velocityYMin{ -5 }, velocityYMax{ -15 };
		float rotationSpeedMin{ -0.14f }, rotationSpeedMax{ 0.14f };	// rad per frame
		float lifetimeMin{ 3 }, lifetimeMax{ 8 };		// seconds
		float decelerateAfter{ 1 };						// seconds
		float friction{ 0.99f };						// per frame after decelerateAfter
		float outlineWidth{ 10 };
		float flashFadeIn{ 1 }, flashFadeOut{ 3 };		// seconds
		float flashStrength{ 1.0f };					// peak alpha of the white flash

		// Everything time-based multiplied by this: 1 = SEKAI-Stories' ambient loop,
		// smaller = a quicker one-shot burst (the startup intro uses ~0.4).
		float timeScale{ 1.0f };
	};

	class Transition
	{
	public:
		explicit Transition(TransitionConfig config = {}) : config(config) {}

		// Start a burst inside `area` (screen coordinates).
		void start(const ImVec2& areaMin, const ImVec2& areaMax, unsigned int seed = 0);
		void update(float deltaSeconds);
		void draw(ImDrawList* drawList, float opacity = 1.0f) const;

		bool finished() const;
		float flashAlpha() const;

	private:
		struct Triangle
		{
			ImVec2 position;
			ImVec2 velocity;		// px per frame
			float rotation{};
			float rotationSpeed{};	// rad per frame
			float size{};
			float lifetime{};
			float maxLifetime{};
			ImU32 color{};
			bool filled{};
		};

		TransitionConfig config;
		std::vector<Triangle> triangles;
		ImVec2 areaMin{}, areaMax{};
		float scale{ 1.0f };
		float flashTime{ 0.0f };
		std::mt19937 rng;

		float random(float a, float b);
	};
}
