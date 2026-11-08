#include "SekaiTransition.h"
#include <algorithm>
#include <cmath>

namespace Canvas::Sekai
{
	namespace
	{
		// TRIANGLE_COLORS: 0xff00ff, 0x00ffff, 0xffff00
		constexpr ImU32 triangleColors[] = { IM_COL32(255, 0, 255, 255), IM_COL32(0, 255, 255, 255), IM_COL32(255, 255, 0, 255) };
		constexpr float FRAME = 1.0f / 60.0f;	// PIXI ticker delta 1 = one 60 fps frame
	}

	float Transition::random(float a, float b)
	{
		return std::uniform_real_distribution<float>(std::min(a, b), std::max(a, b))(rng);
	}

	void Transition::start(const ImVec2& min, const ImVec2& max, unsigned int seed)
	{
		areaMin = min;
		areaMax = max;
		rng.seed(seed ? seed : std::random_device{}());

		const float width = max.x - min.x, height = max.y - min.y;
		scale = std::max(0.1f, height / 1080.0f);
		flashTime = 0.0f;
		triangles.clear();

		// spawnTriangles()
		for (int i = 0; i < config.maxTriangles; ++i)
		{
			Triangle t;
			t.size = random(config.sizeMin, config.sizeMax) * scale;
			t.color = triangleColors[std::min(2, static_cast<int>(random(0, 3)))];
			t.filled = random(0, 1) > 0.5f;

			if (random(0, 1) > 0.5f)	// "bottom"
			{
				t.position.x = min.x + random(0, 1) * width / 2 + width / 2;
				t.position.y = min.y + height + (random(0, 500) + 100) * scale;
			}
			else						// "right"
			{
				t.position.y = min.y + random(0, 1) * height / 2 + height / 2;
				t.position.x = min.x + width + (random(0, 500) + 100) * scale;
			}

			t.velocity = { random(config.velocityXMin, config.velocityXMax) * scale, random(config.velocityYMin, config.velocityYMax) * scale };
			t.rotationSpeed = random(config.rotationSpeedMin, config.rotationSpeedMax);
			t.maxLifetime = random(config.lifetimeMin, config.lifetimeMax) * config.timeScale;
			t.rotation = random(0, 1) * 6.2831853f;
			triangles.push_back(t);
		}
	}

	void Transition::update(float deltaSeconds)
	{
		flashTime += deltaSeconds;

		// with a smaller timeScale everything moves proportionally faster, so the burst covers
		// the same ground in less time
		const float frames = deltaSeconds / FRAME / std::max(0.05f, config.timeScale);
		for (auto it = triangles.begin(); it != triangles.end();)
		{
			Triangle& t = *it;
			t.position.x += t.velocity.x * frames;
			t.position.y += t.velocity.y * frames;
			t.rotation += t.rotationSpeed * frames;
			t.lifetime += deltaSeconds;

			if (t.lifetime > config.decelerateAfter * config.timeScale)
			{
				const float slow = std::pow(config.friction, frames);
				t.velocity.x *= slow;
				t.velocity.y *= slow;
			}

			if (t.lifetime >= t.maxLifetime)
				it = triangles.erase(it);
			else
				++it;
		}
	}

	float Transition::flashAlpha() const
	{
		// Flash.update(): linear fade in, then linear fade out
		const float fadeIn = config.flashFadeIn * config.timeScale, fadeOut = config.flashFadeOut * config.timeScale;
		float alpha;
		if (flashTime < fadeIn)
			alpha = std::max(flashTime / fadeIn, 0.0f);
		else
			alpha = std::clamp((fadeIn + fadeOut - flashTime) / fadeOut, 0.0f, 1.0f);
		return alpha * config.flashStrength;
	}

	bool Transition::finished() const
	{
		return triangles.empty() && flashTime >= (config.flashFadeIn + config.flashFadeOut) * config.timeScale;
	}

	void Transition::draw(ImDrawList* drawList, float opacity) const
	{
		if (!drawList)
			return;

		drawList->PushClipRect(areaMin, areaMax, true);

		const float flash = flashAlpha() * opacity;
		if (flash > 0.0f)
			drawList->AddRectFilled(areaMin, areaMax, IM_COL32(255, 255, 255, static_cast<int>(flash * 255)));

		for (const Triangle& t : triangles)
		{
			// alpha = 1 - lifetime / maxLifetime
			const float alpha = std::clamp(1.0f - t.lifetime / t.maxLifetime, 0.0f, 1.0f) * opacity;
			if (alpha <= 0.0f)
				continue;

			// polygon [-s/2, s/2], [s/2, s/2], [0, -s/2] rotated around the pivot (0, 0)
			const float h = t.size / 2, c = std::cos(t.rotation), s = std::sin(t.rotation);
			auto point = [&](float x, float y) { return ImVec2{ t.position.x + x * c - y * s, t.position.y + x * s + y * c }; };
			const ImVec2 a = point(-h, h), b = point(h, h), d = point(0, -h);

			const ImU32 color = (t.color & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << IM_COL32_A_SHIFT);
			if (t.filled)
				drawList->AddTriangleFilled(a, b, d, color);
			else
				drawList->AddTriangle(a, b, d, color, config.outlineWidth * scale);
		}

		drawList->PopClipRect();
	}
}
