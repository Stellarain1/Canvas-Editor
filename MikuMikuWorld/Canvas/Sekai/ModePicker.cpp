#include "ModePicker.h"
#include "UiSounds.h"
#include "../UI/SekaiStyle.h"
#include "../../IconsFontAwesome5.h"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace Canvas::Sekai
{
	namespace
	{
		float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
		float easeOutCubic(float x) { x = clamp01(x); return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); }

		ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, static_cast<int>(clamp01(a) * 255.0f + 0.5f)); }

		struct Card
		{
			ChartMode mode;
			const char* icon;
			const char* title;
			const char* line;		// what it is, in one line
			const char* source;		// where it comes from
			int r, g, b;			// the card's colour
		};

		const Card CARDS[CHART_MODE_COUNT] = {
			{ ChartMode::Extended, ICON_FA_LAYER_GROUP, "Extended Score", "Layers, speed groups, colours.", "MMW4CC / UntitledCharts", 0x33, 0xcc, 0xbb },
			{ ChartMode::NormalMmw, ICON_FA_MUSIC, "Normal MMW", "The classic tools. Opens in any MMW.", "MikuMikuWorld", 0xff, 0x77, 0xaa },
			{ ChartMode::DynamicStages, ICON_FA_CUBES, "Dynamic Stages", "Stages that move, turn and fade.", "Next SEKAI", 0x88, 0x66, 0xff },
		};

		void centeredText(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text)
		{
			const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
			dl->AddText(font, size, ImVec2(std::floor(center.x - extent.x * 0.5f), std::floor(center.y - extent.y * 0.5f)), color, text);
		}
	}

	const char* chartModeName(ChartMode mode)
	{
		const int i = std::clamp(static_cast<int>(mode), 0, CHART_MODE_COUNT - 1);
		return CARDS[i].title;
	}

	bool ModePicker::update(ImFont* titleFont, bool sounds, float volume)
	{
		if (!visible)
			return false;
		const double now = ImGui::GetTime();
		if (openedAt < 0.0)
			openedAt = now;
		const float t = static_cast<float>(now - openedAt);
		const float appear = easeOutCubic(t / 0.35f);

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 min = viewport->Pos, size = viewport->Size, max = min + size;
		// sized by the window: a 1080p window is the reference, never smaller than half of it
		const float k = std::clamp(std::min(size.x / 1920.0f, size.y / 1080.0f) * 1.35f, 0.55f, 1.6f);

		ImGui::SetNextWindowPos(min);
		ImGui::SetNextWindowSize(size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowFocus();
		ImGui::Begin("##canvas_mode_picker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImFont* font = titleFont ? titleFont : (Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont());
		ImFont* body = ImGui::GetFont();

		// the editor behind, dimmed
		dl->AddRectFilled(min, max, rgba(0x18, 0x18, 0x2a, 0.72f * appear));

		const char* question = "What are you Charting today?";
		const float titlePx = 46.0f * k;
		const float slide = (1.0f - appear) * 24.0f * k;

		// cards side by side, or stacked when the window is narrow
		const float cardW = 330.0f * k, cardH = 250.0f * k, gap = 28.0f * k;
		const bool stacked = size.x < cardW * 3 + gap * 4;
		const float cw = stacked ? std::min(size.x - gap * 2, 520.0f * k) : cardW;
		const float ch = stacked ? cardH * 0.52f : cardH;
		const float blockW = stacked ? cw : cardW * 3 + gap * 2;
		const float blockH = stacked ? ch * 3 + gap * 2 : cardH;
		const float totalH = titlePx * 2.2f + blockH + 70.0f * k;
		const float top = min.y + std::max(gap, (size.y - totalH) * 0.5f) + slide;

		centeredText(dl, font, titlePx, ImVec2(min.x + size.x * 0.5f, top + titlePx * 0.5f + 2.0f * k), rgba(0x10, 0x10, 0x20, 0.5f * appear), question);
		centeredText(dl, font, titlePx, ImVec2(min.x + size.x * 0.5f, top + titlePx * 0.5f), rgba(0xff, 0xff, 0xff, appear), question);

		const float cardsTop = top + titlePx * 2.0f;
		const float left = min.x + (size.x - blockW) * 0.5f;
		int clicked = -1;
		for (int i = 0; i < CHART_MODE_COUNT; ++i)
		{
			const Card& c = CARDS[i];
			// one after another, 60 ms apart
			const float in = easeOutCubic((t - 0.08f - i * 0.06f) / 0.3f);
			ImVec2 a = stacked ? ImVec2(left, cardsTop + i * (ch + gap)) : ImVec2(left + i * (cardW + gap), cardsTop);
			a.y += (1.0f - in) * 30.0f * k;
			const ImVec2 b = a + ImVec2(cw, ch);

			ImGui::SetCursorScreenPos(a);
			ImGui::PushID(i);
			if (ImGui::InvisibleButton("##card", ImVec2(cw, ch)))
				clicked = i;
			const bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			hover[i] += ((hovered ? 1.0f : 0.0f) - hover[i]) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
			const float h = hover[i];
			const ImVec2 lift(0.0f, -6.0f * k * h);
			const float alpha = in * appear;
			const float round = 18.0f * k;

			// shadow, card, colour band, glow when hovered
			dl->AddRectFilled(a + ImVec2(0, 8.0f * k), b + ImVec2(0, 8.0f * k), rgba(0, 0, 0, 0.22f * alpha), round);
			dl->AddRectFilled(a + lift, b + lift, rgba(0xff, 0xff, 0xff, 0.97f * alpha), round);
			const float band = stacked ? ch : ch * 0.42f;
			const ImVec2 bandMax = stacked ? ImVec2(a.x + ch, b.y) : ImVec2(b.x, a.y + band);
			dl->AddRectFilled(a + lift, bandMax + lift, rgba(c.r, c.g, c.b, alpha), round,
				stacked ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersTop);
			// diagonal stripes in the band, like the game's headers
			dl->PushClipRect(a + lift, bandMax + lift, true);
			for (float x = a.x - band; x < bandMax.x; x += 22.0f * k)
				dl->AddLine(ImVec2(x, bandMax.y) + lift, ImVec2(x + band, a.y) + lift, rgba(0xff, 0xff, 0xff, 0.10f * alpha), 6.0f * k);
			dl->PopClipRect();
			if (h > 0.01f)
				dl->AddRect(a + lift - ImVec2(3, 3) * k, b + lift + ImVec2(3, 3) * k, rgba(c.r, c.g, c.b, 0.9f * h * alpha), round + 3.0f * k, 0, 3.0f * k);

			// number key, icon, title, one line, source
			const ImVec2 iconCenter = stacked ? ImVec2(a.x + ch * 0.5f, a.y + ch * 0.5f) : ImVec2(a.x + cw * 0.5f, a.y + band * 0.5f);
			centeredText(dl, body, 54.0f * k * (stacked ? 0.8f : 1.0f), iconCenter + lift, rgba(0xff, 0xff, 0xff, alpha), c.icon);
			const std::string key = std::to_string(i + 1);
			dl->AddText(font, 18.0f * k, a + lift + ImVec2(14.0f, 10.0f) * k, rgba(0xff, 0xff, 0xff, 0.8f * alpha), key.c_str());

			const float textLeft = stacked ? a.x + ch + 22.0f * k : 0.0f;
			auto line = [&](ImFont* f, float px, float y, ImU32 color, const char* text) {
				if (stacked)
					dl->AddText(f, px, ImVec2(textLeft, y) + lift, color, text);
				else
					centeredText(dl, f, px, ImVec2(a.x + cw * 0.5f, y + px * 0.5f) + lift, color, text);
			};
			const float y0 = stacked ? a.y + ch * 0.16f : a.y + band + 18.0f * k;
			line(font, 28.0f * k, y0, rgba(0x44, 0x44, 0x66, alpha), c.title);
			line(body, 17.0f * k, y0 + 42.0f * k, rgba(0x55, 0x55, 0x77, alpha), c.line);
			line(body, 14.0f * k, y0 + 70.0f * k, rgba(c.r, c.g, c.b, alpha), c.source);
		}

		// keys 1 / 2 / 3 pick too
		for (int i = 0; i < CHART_MODE_COUNT && clicked < 0; ++i)
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false) || ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad1 + i), false))
				clicked = i;

		// under the cards: open an existing chart instead
		const char* openText = ICON_FA_FOLDER_OPEN "  Open a chart...";
		const float openPx = 18.0f * k;
		const ImVec2 openSize = body->CalcTextSizeA(openPx, FLT_MAX, 0.0f, openText);
		const ImVec2 openPos(min.x + (size.x - openSize.x) * 0.5f, cardsTop + blockH + 30.0f * k);
		ImGui::SetCursorScreenPos(openPos);
		const bool openClicked = ImGui::InvisibleButton("##open_chart", openSize);
		const bool openHovered = ImGui::IsItemHovered();
		dl->AddText(body, openPx, openPos, rgba(0xff, 0xff, 0xff, (openHovered ? 1.0f : 0.75f) * appear), openText);
		if (openHovered)
			dl->AddLine(openPos + ImVec2(0, openSize.y + 2.0f), openPos + ImVec2(openSize.x, openSize.y + 2.0f), rgba(0xff, 0xff, 0xff, appear), 1.5f);
		ImGui::End();

		if (clicked >= 0 || openClicked)
		{
			if (sounds)
				uiSounds().play(UiSound::Select, volume);
			picked = clicked >= 0 ? CARDS[clicked].mode : picked;
			openRequested = openClicked;
			visible = false;
			return true;
		}
		return false;
	}
}
