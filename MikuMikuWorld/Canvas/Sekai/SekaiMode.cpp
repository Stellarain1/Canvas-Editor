#include "SekaiMode.h"
#include "SekaiAssets.h"
#include "UiSounds.h"
#include "../../ImGui/imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace Canvas::Sekai
{
	ImVec4 Palette::vec(ImU32 color, float alpha)
	{
		ImVec4 v = ImGui::ColorConvertU32ToFloat4(color);
		v.w *= alpha;
		return v;
	}

	void applyStyle(ImGuiStyle& style)
	{
		using namespace Palette;

		// .window { border-radius: 20px } / button.btn-regular { border-radius: 30px }
		style.WindowRounding = 14;
		style.ChildRounding = 10;
		style.FrameRounding = 12;
		style.PopupRounding = 14;
		style.GrabRounding = 12;
		style.TabRounding = 8;
		style.ScrollbarRounding = 12;
		style.WindowBorderSize = 0;
		style.PopupBorderSize = 0;
		style.ChildBorderSize = 1;
		style.FrameBorderSize = 0;
		style.WindowPadding = { 10, 8 };
		style.FramePadding = { 8, 3 };
		style.ItemSpacing = { 6, 5 };
		style.ScrollbarSize = 12;
		style.GrabMinSize = 10;

		ImVec4* c = style.Colors;
		c[ImGuiCol_Text] = vec(Black);
		c[ImGuiCol_TextDisabled] = vec(Black, 0.5f);
		c[ImGuiCol_WindowBg] = vec(LightGray);
		c[ImGuiCol_ChildBg] = vec(LightGray);
		c[ImGuiCol_PopupBg] = vec(LightGray);
		c[ImGuiCol_Border] = vec(OptionBorder);
		c[ImGuiCol_BorderShadow] = vec(Black, 0.12f);
		c[ImGuiCol_FrameBg] = vec(White);
		c[ImGuiCol_FrameBgHovered] = vec(LightMikuClicked);
		c[ImGuiCol_FrameBgActive] = vec(WhiteClicked);
		c[ImGuiCol_TitleBg] = vec(OptionBorder);
		c[ImGuiCol_TitleBgActive] = vec(LightMiku);
		c[ImGuiCol_TitleBgCollapsed] = vec(OptionBorder);
		c[ImGuiCol_MenuBarBg] = vec(White);
		c[ImGuiCol_ScrollbarBg] = vec(LightGray);
		c[ImGuiCol_ScrollbarGrab] = vec(Black, 0.25f);
		c[ImGuiCol_ScrollbarGrabHovered] = vec(LightMiku);
		c[ImGuiCol_ScrollbarGrabActive] = vec(DarkMiku);
		c[ImGuiCol_CheckMark] = vec(DarkMiku);
		c[ImGuiCol_SliderGrab] = vec(LightMiku);
		c[ImGuiCol_SliderGrabActive] = vec(DarkMiku);
		// button.btn-white / :active
		c[ImGuiCol_Button] = vec(White);
		c[ImGuiCol_ButtonHovered] = vec(LightMikuClicked);
		c[ImGuiCol_ButtonActive] = vec(WhiteClicked);
		c[ImGuiCol_Header] = vec(LightMiku, 0.45f);
		c[ImGuiCol_HeaderHovered] = vec(LightMiku, 0.65f);
		c[ImGuiCol_HeaderActive] = vec(DarkMiku, 0.75f);
		c[ImGuiCol_Separator] = vec(OptionBorder);
		c[ImGuiCol_SeparatorHovered] = vec(LightPink);
		c[ImGuiCol_SeparatorActive] = vec(DarkPink);
		c[ImGuiCol_ResizeGrip] = vec(LightMiku, 0.3f);
		c[ImGuiCol_ResizeGripHovered] = vec(LightMiku, 0.7f);
		c[ImGuiCol_ResizeGripActive] = vec(DarkMiku);
		c[ImGuiCol_Tab] = vec(OptionBorder, 0.7f);
		c[ImGuiCol_TabHovered] = vec(LightPink);
		c[ImGuiCol_TabActive] = vec(LightMiku);
		c[ImGuiCol_TabUnfocused] = vec(OptionBorder, 0.5f);
		c[ImGuiCol_TabUnfocusedActive] = vec(LightMiku, 0.7f);
		c[ImGuiCol_DockingPreview] = vec(LightMiku, 0.6f);
		c[ImGuiCol_DockingEmptyBg] = vec(LightGray);
		c[ImGuiCol_PlotLines] = vec(DarkPink);
		c[ImGuiCol_PlotLinesHovered] = vec(LightPink);
		c[ImGuiCol_PlotHistogram] = vec(DarkPink);
		c[ImGuiCol_PlotHistogramHovered] = vec(LightPink);
		c[ImGuiCol_TableHeaderBg] = vec(OptionBorder, 0.6f);
		c[ImGuiCol_TableBorderStrong] = vec(OptionBorder);
		c[ImGuiCol_TableBorderLight] = vec(OptionBorder, 0.6f);
		c[ImGuiCol_TableRowBg] = vec(White);
		c[ImGuiCol_TableRowBgAlt] = vec(LightGray, 0.6f);
		c[ImGuiCol_TextSelectedBg] = vec(LightMiku, 0.5f);
		c[ImGuiCol_DragDropTarget] = vec(DarkPink);
		c[ImGuiCol_NavHighlight] = vec(DarkPink);
		// .screen { background-color: #000000bb }
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0xbb / 255.0f);
	}

	namespace
	{
		bool isEditingSurface(const ImGuiWindow* window)
		{
			if (!window)
				return false;
			const ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
			const char* name = root->Name ? root->Name : "";
			// clicking notes on the timeline / preview should not click
			return std::strstr(name, "###notes_timeline") || std::strstr(name, "###score_preview");
		}

		// Scale every vertex and clip rect of `window` (and its children) around `center`, fading
		// it to `alpha`.
		void scaleWindow(ImGuiWindow* modal, float scale, float alpha = 1.0f)
		{
			ImGuiContext& g = *GImGui;
			const ImVec2 center = modal->Pos + modal->Size * 0.5f;
			for (ImGuiWindow* window : g.Windows)
			{
				if (!window->Active || window->RootWindow != modal)
					continue;

				ImDrawList* list = window->DrawList;
				for (ImDrawVert& vertex : list->VtxBuffer)
				{
					vertex.pos = center + (vertex.pos - center) * scale;
					if (alpha < 1.0f)
					{
						const ImU32 a = static_cast<ImU32>(((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
						vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
					}
				}
				for (ImDrawCmd& cmd : list->CmdBuffer)
				{
					cmd.ClipRect.x = center.x + (cmd.ClipRect.x - center.x) * scale;
					cmd.ClipRect.y = center.y + (cmd.ClipRect.y - center.y) * scale;
					cmd.ClipRect.z = center.x + (cmd.ClipRect.z - center.x) * scale;
					cmd.ClipRect.w = center.y + (cmd.ClipRect.w - center.y) * scale;
				}
			}
		}
	}

	namespace
	{
		bool tapRequested = false;
		bool slideRequested = false;
	}

	void noteTap() { tapRequested = true; }
	void noteSlide() { slideRequested = true; }

	void ModeHooks::endFrame(bool sounds, float volume, bool animateDialogs)
	{
		ImGuiContext& g = *GImGui;
		const double now = ImGui::GetTime();
		const bool tapped = tapRequested, slid = slideRequested;
		tapRequested = slideRequested = false;

		// every pop-up that is open right now: dialogs, combo lists, context menus. Menus (the menu bar
		// and its submenus) stay quiet; the click that opens one plays the tap sound instead.
		std::set<ImGuiID> current;
		bool opened = false;
		for (const ImGuiPopupData& popup : g.OpenPopupStack)
		{
			if (!popup.Window)
				continue;
			const bool modal = (popup.Window->Flags & ImGuiWindowFlags_Modal) != 0;
			if (!modal && (popup.Window->Flags & ImGuiWindowFlags_ChildMenu))
				continue;
			current.insert(popup.PopupId);

			auto it = openPopups.find(popup.PopupId);
			if (it == openPopups.end())
			{
				openPopups[popup.PopupId] = { now, modal };
				opened = true;
				if (sounds)
					uiSounds().play(UiSound::Open, volume);
			}
			else if (modal && animateDialogs)
			{
				// the game's dialogs pop open: grow from 85% with a small overshoot while fading in, 0.2 s
				const float t = static_cast<float>((now - it->second.since) / 0.2);
				if (t < 1.0f)
				{
					const float c1 = 1.70158f, c3 = c1 + 1.0f;
					const float back = 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
					scaleWindow(popup.Window, 0.85f + 0.15f * back, std::min(1.0f, t * 2.5f));
				}
			}
		}

		bool closedDialog = false;
		for (auto it = openPopups.begin(); it != openPopups.end();)
		{
			if (current.count(it->first))
			{
				++it;
				continue;
			}
			closedDialog |= it->second.modal;
			it = openPopups.erase(it);
		}
		if (closedDialog && sounds)
			uiSounds().play(UiSound::Close, volume);

		if (!sounds)
			return;

		// sliders, drag boxes and scrollbars tick while they move
		if (slid && (lastSlide < 0 || now - lastSlide >= 0.07))
		{
			uiSounds().play(UiSound::Slide, volume);
			lastSlide = now;
		}

		// App.tsx: any button click plays select.wav (close buttons play close.wav instead)
		const bool clicked = g.ActiveId != 0 && g.ActiveIdIsJustActivated && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!isEditingSurface(g.ActiveIdWindow);
		if ((clicked || tapped) && !closedDialog && !opened && !slid)
			uiSounds().play(UiSound::Select, volume);
	}

	ModeHooks& hooks()
	{
		static ModeHooks instance;
		return instance;
	}

	std::string pickBackground(std::mt19937& rng)
	{
		const auto& list = assets().backgrounds();
		if (list.empty())
			return {};
		return list[std::uniform_int_distribution<size_t>(0, list.size() - 1)(rng)];
	}
}
