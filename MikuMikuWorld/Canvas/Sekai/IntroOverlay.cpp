#include "IntroOverlay.h"
#include "SekaiMode.h"
#include "UiSounds.h"
#include "../../Rendering/Texture.h"
#include "../../IO.h"
#include <algorithm>
#include <cmath>

namespace Canvas::Sekai
{
	namespace
	{
		float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
		float phase(float t, float begin, float length) { return clamp01((t - begin) / length); }
		float easeOutCubic(float x) { return 1.0f - std::pow(1.0f - x, 3.0f); }
		float easeInCubic(float x) { return x * x * x; }

		ImU32 withAlpha(ImU32 color, float alpha)
		{
			const float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f * clamp01(alpha);
			return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
		}

		TransitionConfig introTransition()
		{
			TransitionConfig config;
			config.timeScale = 0.4f;		// the same burst, 2.5x quicker
			config.flashStrength = 0.55f;	// a softer flash so the band stays readable
			return config;
		}

		// .screen text band: public/img/SceneText_Background.png is #444466 at 80% from x 422 to
		// 1499 and y 480 to 600 on the 1920x1080 stage, fading out at both ends
		void drawSceneTextBand(ImDrawList* list, const ImVec2& center, float width, float height, float alpha)
		{
			const ImU32 solid = withAlpha(IM_COL32(0x44, 0x44, 0x66, 204), alpha);
			const ImU32 clear = withAlpha(IM_COL32(0x44, 0x44, 0x66, 0), alpha);
			const float fade = width * 0.14f;
			const ImVec2 min{ center.x - width / 2, center.y - height / 2 }, max{ center.x + width / 2, center.y + height / 2 };
			list->AddRectFilledMultiColor(min, { min.x + fade, max.y }, clear, solid, solid, clear);
			list->AddRectFilled({ min.x + fade, min.y }, { max.x - fade, max.y }, solid);
			list->AddRectFilledMultiColor({ max.x - fade, min.y }, max, solid, clear, clear, solid);
		}

		void drawCenteredText(ImDrawList* list, ImFont* font, float size, const ImVec2& center, ImU32 color, const std::string& text, float shadowAlpha)
		{
			if (text.empty())
				return;
			const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
			const ImVec2 pos{ std::floor(center.x - extent.x / 2), std::floor(center.y - extent.y / 2) };
			if (shadowAlpha > 0)
				list->AddText(font, size, pos + ImVec2{ 0, size * 0.06f }, IM_COL32(0x22, 0x22, 0x33, static_cast<int>(shadowAlpha * 255)), text.c_str());
			list->AddText(font, size, pos, color, text.c_str());
		}
	}

	IntroOverlay::IntroOverlay() : transition(introTransition()) {}
	IntroOverlay::~IntroOverlay() = default;

	void IntroOverlay::start(IntroOptions opts)
	{
		options = std::move(opts);
		options.duration = std::max(1.5f, options.duration);
		startTime = lastTime = -1.0;
		endTime = options.duration;
		running = true;
		transitionStarted = playedOpen = playedClose = false;

		if (options.backgroundImage != backgroundPath)
		{
			background.reset();
			backgroundPath = options.backgroundImage;
			if (!backgroundPath.empty() && IO::File::exists(backgroundPath))
			{
				background = std::make_unique<MikuMikuWorld::Texture>(backgroundPath);
				if (background->getID() == 0)
					background.reset();
			}
		}
	}

	void IntroOverlay::skip()
	{
		if (!running || startTime < 0)
			return;
		const float t = static_cast<float>(ImGui::GetTime() - startTime);
		endTime = std::min(endTime, t + 0.45f);
	}

	void IntroOverlay::update()
	{
		if (!running)
			return;

		const double now = ImGui::GetTime();
		if (startTime < 0)
			startTime = lastTime = now;
		const float t = static_cast<float>(now - startTime);
		const float dt = static_cast<float>(std::min(0.1, now - lastTime));
		lastTime = now;

		if (t >= endTime)
		{
			running = false;
			return;
		}

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 min = viewport->Pos, max = viewport->Pos + viewport->Size;
		const float W = viewport->Size.x, H = viewport->Size.y;
		const float stage = H / 1080.0f;

		// an invisible window on top that swallows input; any click or key skips the intro
		ImGui::SetNextWindowPos(min);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowFocus();
		ImGui::Begin("##canvas_intro", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
		bool skipRequested = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
		for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && !skipRequested; ++key)
		{
			if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY)
				continue;
			skipRequested = ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false);
		}
		ImGui::End();
		if (skipRequested)
			skip();

		if (!transitionStarted)
		{
			transition.start(min, max);
			transitionStarted = true;
		}
		transition.update(dt);

		const float T = options.duration;
		const float overall = clamp01((endTime - t) / 0.45f);		// final fade (or skip)
		const float bandOpen = easeOutCubic(phase(t, 0.25f, 0.35f));
		const float closeAt = T - 0.9f;
		const float bandClose = easeInCubic(phase(t, closeAt, 0.3f));
		const float band = bandOpen * (1.0f - bandClose);

		if (options.sounds && !playedOpen && t >= 0.25f && overall > 0.5f)
		{
			uiSounds().play(UiSound::Open, options.volume);
			playedOpen = true;
		}
		if (options.sounds && !playedClose && t >= closeAt && endTime >= T)
		{
			uiSounds().play(UiSound::Close, options.volume);
			playedClose = true;
		}

		ImDrawList* list = ImGui::GetForegroundDrawList(viewport);

		// backdrop
		const float backdrop = phase(t, 0.0f, 0.2f) * overall;
		if (background)
		{
			const float imageAspect = static_cast<float>(background->getWidth()) / std::max(1, background->getHeight());
			const float screenAspect = W / std::max(1.0f, H);
			ImVec2 uv0{ 0, 0 }, uv1{ 1, 1 };
			if (imageAspect > screenAspect)
			{
				const float crop = (1.0f - screenAspect / imageAspect) / 2;
				uv0.x = crop; uv1.x = 1 - crop;
			}
			else
			{
				const float crop = (1.0f - imageAspect / screenAspect) / 2;
				uv0.y = crop; uv1.y = 1 - crop;
			}
			list->AddImage((ImTextureID)(size_t)background->getID(), min, max, uv0, uv1, withAlpha(IM_COL32_WHITE, backdrop));
			list->AddRectFilled(min, max, withAlpha(IM_COL32(0, 0, 0, 90), backdrop));
		}
		else
		{
			list->AddRectFilledMultiColor(min, max,
				withAlpha(IM_COL32(0x1f, 0x1d, 0x36, 255), backdrop), withAlpha(IM_COL32(0x2b, 0x25, 0x4a, 255), backdrop),
				withAlpha(IM_COL32(0x3d, 0x2f, 0x5e, 255), backdrop), withAlpha(IM_COL32(0x26, 0x2b, 0x4d, 255), backdrop));
		}

		// SEKAI transition: flash + triangles
		transition.draw(list, overall);

		// scene text band + text
		const ImVec2 center{ min.x + W / 2, min.y + H / 2 };
		if (band > 0.001f)
			drawSceneTextBand(list, center, W * 0.561f * band, 120.0f * stage * (0.55f + 0.45f * band), overall);

		ImFont* font = options.font ? options.font : ImGui::GetFont();
		const float textAlpha = phase(t, 0.45f, 0.3f) * (1.0f - phase(t, closeAt, 0.2f)) * overall;
		if (textAlpha > 0.0f)
		{
			const float rise = (1.0f - easeOutCubic(phase(t, 0.45f, 0.35f))) * 10.0f * stage;
			drawCenteredText(list, font, std::max(24.0f, 44.0f * stage), center + ImVec2{ 0, rise },
				withAlpha(IM_COL32_WHITE, textAlpha), options.text, textAlpha * 0.35f);
		}

		const float subAlpha = phase(t, 0.7f, 0.3f) * (1.0f - phase(t, closeAt, 0.2f)) * overall;
		if (subAlpha > 0.0f && !options.subtext.empty())
		{
			// a slimmer band of its own so the line reads on light backgrounds too
			const float subSize = std::max(14.0f, 22.0f * stage);
			const ImVec2 subCenter = center + ImVec2{ 0, 60.0f * stage + subSize * 0.85f + 10.0f * stage };
			const float subWidth = ImGui::GetFont()->CalcTextSizeA(subSize, FLT_MAX, 0.0f, options.subtext.c_str()).x;
			drawSceneTextBand(list, subCenter, (subWidth + subSize * 3) / 0.72f, subSize * 1.7f, subAlpha * 0.85f);
			drawCenteredText(list, ImGui::GetFont(), subSize, subCenter, withAlpha(IM_COL32(0xeb, 0xeb, 0xf2, 245), subAlpha), options.subtext, 0.0f);
		}
	}
}
