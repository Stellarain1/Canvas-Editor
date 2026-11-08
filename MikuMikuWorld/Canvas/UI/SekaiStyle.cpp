#include "../Sekai/SekaiMode.h"
#include "SekaiStyle.h"
#include "../../ImGui/imgui_internal.h"
#include "../../Application.h"
#include "../../Colors.h"
#include "../../File.h"
#include "../../Rendering/Texture.h"
#include "stb_image.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

namespace fs = std::filesystem;

namespace Canvas::SekaiStyle
{
	namespace
	{
		Palette current;
		bool installed = false;
		// the colours apply() gave each ImGuiCol: a button pushed with its own colour (swatches,
		// BPM labels) is not one of ours and keeps that colour
		ImVec4 styleColors[ImGuiCol_COUNT];

		ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
		ImVec4 withAlpha(ImVec4 c, float a) { c.w = a; return c; }

		float alphaOf(ImU32 color) { return ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f; }

		// our own colours with ImGui's current alpha (BeginDisabled / style.Alpha) applied
		ImU32 styled(const ImVec4& color, float alpha = 1.0f)
		{
			ImVec4 c = color;
			c.w *= alpha * GImGui->Style.Alpha;
			return ImGui::ColorConvertFloat4ToU32(c);
		}

		// ---- sprite atlas ----------------------------------------------------------------------------

		struct Atlas
		{
			bool built{ false };
			unsigned int texture{ 0 };
			std::map<std::string, Sprite> sprites;
		};

		Atlas& atlas()
		{
			static Atlas a;
			return a;
		}

		std::string packDir()
		{
			return MikuMikuWorld::Application::getAppDir() + "res/sekai/";
		}

		void buildAtlas()
		{
			Atlas& a = atlas();
			a.built = true;
			std::ifstream in(fs::u8path(packDir() + "sprites.json"));
			if (!in)
				return;
			const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
			if (!json.is_object())
				return;

			struct Item
			{
				std::string name;
				int w{}, h{};
				unsigned char* pixels{};
				ImVec4 border;
				int x{}, y{};
				int cx0{}, cy0{}, cx1{}, cy1{};	// opaque part
			};
			std::vector<Item> items;
			std::vector<std::string> names;
			for (const auto& [name, value] : json.items())
				names.push_back(name);
			// the backdrop shapes live in bg/
			for (const char* shape : { "bg/circle_white", "bg/circle_pink", "bg/triangle_pink", "bg/triangle_mint" })
				names.push_back(shape);
			for (const std::string& name : names)
			{
				Item item;
				item.name = name;
				const std::string file = packDir() + name + ".png";
				item.pixels = stbi_load(file.c_str(), &item.w, &item.h, nullptr, 4);
				if (!item.pixels)
					continue;
				item.cx0 = item.w; item.cy0 = item.h; item.cx1 = 0; item.cy1 = 0;
				for (int py = 0; py < item.h; ++py)
					for (int px = 0; px < item.w; ++px)
						if (item.pixels[(static_cast<size_t>(py) * item.w + px) * 4 + 3] > 8)
						{
							item.cx0 = std::min(item.cx0, px); item.cy0 = std::min(item.cy0, py);
							item.cx1 = std::max(item.cx1, px + 1); item.cy1 = std::max(item.cy1, py + 1);
						}
				if (item.cx1 <= item.cx0)
				{
					item.cx0 = item.cy0 = 0;
					item.cx1 = item.w;
					item.cy1 = item.h;
				}
				if (json.contains(name) && json[name].contains("border"))
				{
					const auto& b = json[name]["border"];	// Unity order: left, bottom, right, top
					item.border = ImVec4(b[0].get<float>(), b[3].get<float>(), b[2].get<float>(), b[1].get<float>());
				}
				items.push_back(item);
			}
			if (items.empty())
				return;

			// shelf packing, tallest first, 2 px of extruded edge around every sprite
			std::sort(items.begin(), items.end(), [](const Item& l, const Item& r) { return l.h > r.h; });
			constexpr int WIDTH = 2048, PAD = 2;
			int x = 0, y = 0, shelf = 0;
			for (Item& item : items)
			{
				if (x + item.w + PAD * 2 > WIDTH)
				{
					x = 0;
					y += shelf;
					shelf = 0;
				}
				item.x = x + PAD;
				item.y = y + PAD;
				x += item.w + PAD * 2;
				shelf = std::max(shelf, item.h + PAD * 2);
			}
			int height = 1;
			while (height < y + shelf)
				height <<= 1;

			std::vector<unsigned char> pixels(static_cast<size_t>(WIDTH) * height * 4, 0);
			for (const Item& item : items)
			{
				for (int py = -PAD; py < item.h + PAD; ++py)
					for (int px = -PAD; px < item.w + PAD; ++px)
					{
						const int sx = std::clamp(px, 0, item.w - 1), sy = std::clamp(py, 0, item.h - 1);
						const unsigned char* src = item.pixels + (static_cast<size_t>(sy) * item.w + sx) * 4;
						unsigned char* dst = pixels.data() + (static_cast<size_t>(item.y + py) * WIDTH + item.x + px) * 4;
						std::copy(src, src + 4, dst);
					}
				stbi_image_free(item.pixels);
			}

			glGenTextures(1, &a.texture);
			glBindTexture(GL_TEXTURE_2D, a.texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			glGenerateMipmap(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);

			for (const Item& item : items)
			{
				Sprite s;
				s.texture = static_cast<ImTextureID>(a.texture);
				s.uv0 = ImVec2(static_cast<float>(item.x) / WIDTH, static_cast<float>(item.y) / height);
				s.uv1 = ImVec2(static_cast<float>(item.x + item.w) / WIDTH, static_cast<float>(item.y + item.h) / height);
				s.size = ImVec2(static_cast<float>(item.w), static_cast<float>(item.h));
				s.border = item.border;
				s.contentUv0 = ImVec2(static_cast<float>(item.x + item.cx0) / WIDTH, static_cast<float>(item.y + item.cy0) / height);
				s.contentUv1 = ImVec2(static_cast<float>(item.x + item.cx1) / WIDTH, static_cast<float>(item.y + item.cy1) / height);
				s.contentSize = ImVec2(static_cast<float>(item.cx1 - item.cx0), static_cast<float>(item.cy1 - item.cy0));
				a.sprites[item.name] = s;
			}
		}

		// ---- frames ------------------------------------------------------------------------------------

		enum class Kind { Button, ButtonHovered, ButtonActive, Selected, Frame, FrameHovered, FrameActive, Header, HeaderHovered, HeaderActive, Title, Other };

		Kind classify(ImU32 fill)
		{
			struct Entry { ImGuiCol col; Kind kind; };
			static const Entry entries[] = {
				{ ImGuiCol_TabSelected, Kind::Selected },
				{ ImGuiCol_ButtonActive, Kind::ButtonActive }, { ImGuiCol_ButtonHovered, Kind::ButtonHovered }, { ImGuiCol_Button, Kind::Button },
				{ ImGuiCol_FrameBgActive, Kind::FrameActive }, { ImGuiCol_FrameBgHovered, Kind::FrameHovered }, { ImGuiCol_FrameBg, Kind::Frame },
				{ ImGuiCol_HeaderActive, Kind::HeaderActive }, { ImGuiCol_HeaderHovered, Kind::HeaderHovered }, { ImGuiCol_Header, Kind::Header },
				{ ImGuiCol_TitleBg, Kind::Title }, { ImGuiCol_TitleBgActive, Kind::Title }, { ImGuiCol_TitleBgCollapsed, Kind::Title },
			};
			for (const Entry& e : entries)
				if (ImGui::GetColorU32(styleColors[e.col]) == fill)
					return e.kind;
			return Kind::Other;
		}

		// the game's white pill button: base sprite (shadow baked in) tinted, or a drawn stand-in
		void pill(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImVec4& tint, float rounding, const char* spriteName = "ui/btn_round_h56_wh")
		{
			const Sprite* s = sprite(spriteName);
			if (s && s->valid())
			{
				// the sprite is 72 px tall for a 56 px button: give it room for its shadow
				const float h = b.y - a.y;
				const float scale = h / 56.0f;
				const ImVec2 grow(8.0f * scale, 8.0f * scale);
				nineSlice(dl, *s, a - ImVec2(grow.x, grow.y * 0.5f), b + ImVec2(grow.x, grow.y * 1.5f), styled(tint), std::min(scale, 1.0f));
				return;
			}
			dl->AddRectFilled(a + ImVec2(0, 2), b + ImVec2(0, 2), styled(ImVec4(0.27f, 0.27f, 0.4f, 1), 0.18f), rounding);
			dl->AddRectFilled(a, b, styled(tint), rounding);
		}

		void frameHook(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill, bool borders, float rounding)
		{
			const Palette& p = current;
			const float fillAlpha = alphaOf(fill);
			switch (classify(fill))
			{
			case Kind::Button:
				if (fillAlpha <= 0.001f)
					return;	// transparent buttons (icons, toolbars) stay transparent until hovered
				pill(dl, a, b, p.card, rounding);
				dl->AddRect(a, b, styled(p.border, 0.9f), rounding, 0, 1.0f);
				return;
			case Kind::ButtonHovered:
			{
				if (const Sprite* glow = sprite("ui/eff_btn_round_h80_a70_wh"); glow && glow->valid())
				{
					const float h = b.y - a.y;
					nineSlice(dl, *glow, a - ImVec2(h * 0.35f, h * 0.3f), b + ImVec2(h * 0.35f, h * 0.3f), styled(p.accent, 0.55f), std::min(h / 80.0f, 1.0f));
				}
				pill(dl, a, b, mix(p.card, p.accent, 0.10f), rounding);
				dl->AddRect(a, b, styled(p.accent, 0.85f), rounding, 0, 1.5f);
				return;
			}
			case Kind::ButtonActive:
				pill(dl, a + ImVec2(0, 1), b + ImVec2(0, 1), mix(p.card, p.accent, 0.28f), rounding);
				dl->AddRect(a + ImVec2(0, 1), b + ImVec2(0, 1), styled(p.accent), rounding, 0, 1.5f);
				return;
			case Kind::Selected:
				// toggled tools and the chosen option: the game's mint button
				if (sprite("ui/btn_round_h80_gn"))
					pill(dl, a, b, ImVec4(1, 1, 1, 1), rounding, "ui/btn_round_h80_gn");
				else
					pill(dl, a, b, p.accent, rounding);
				dl->AddRect(a, b, styled(p.accentDark, 0.9f), rounding, 0, 1.5f);
				return;
			case Kind::Frame:
			case Kind::FrameHovered:
			case Kind::FrameActive:
			{
				const Kind k = classify(fill);
				dl->AddRectFilled(a, b, fill, rounding);
				const ImVec4 edge = k == Kind::Frame ? p.border : p.accent;
				dl->AddRect(a, b, styled(edge, k == Kind::FrameHovered ? 0.7f : 1.0f), rounding, 0, k == Kind::FrameActive ? 1.8f : 1.0f);
				return;
			}
			case Kind::Header:
			case Kind::HeaderHovered:
			case Kind::HeaderActive:
			{
				const float r = std::max(rounding, 6.0f);
				dl->AddRectFilled(a, b, fill, r);
				const float inset = std::min(5.0f, (b.y - a.y) * 0.25f);
				dl->AddRectFilled(ImVec2(a.x + 2.0f, a.y + inset), ImVec2(a.x + 5.0f, b.y - inset), styled(p.accent), 2.0f);
				return;
			}
			case Kind::Title:
				dl->AddRectFilled(a, b, fill, rounding);
				if (borders)
					dl->AddRect(a, b, styled(p.border), rounding, 0, 1.0f);
				return;
			case Kind::Other:
			default:
				// coloured labels (BPM / time signature / hi-speed) and swatches keep their colour
				dl->AddRectFilled(a + ImVec2(0, 1.5f), b + ImVec2(0, 1.5f), styled(ImVec4(0.27f, 0.27f, 0.4f, 1), 0.15f * fillAlpha), rounding);
				dl->AddRectFilled(a, b, fill, rounding);
				if (borders && GImGui->Style.FrameBorderSize > 0.0f)
					dl->AddRect(a, b, styled(ImVec4(1, 1, 1, 1), 0.6f * fillAlpha), rounding, 0, 1.0f);
				return;
			}
		}

		bool tabHook(ImDrawList* dl, const ImRect& bb, ImGuiTabItemFlags flags, ImU32)
		{
			const Palette& p = current;
			const bool selected = flags & ImGuiTabItemFlags_SkinSelected;
			const bool hovered = flags & ImGuiTabItemFlags_SkinHovered;
			const bool focused = flags & ImGuiTabItemFlags_SkinFocused;
			const ImVec2 a(bb.Min.x, bb.Min.y + 2.0f), b(bb.Max.x, bb.Max.y);
			const float r = std::min(10.0f, (b.y - a.y) * 0.5f);
			const ImVec4 fill = selected ? p.card : hovered ? mix(p.card, p.accent, 0.12f) : mix(p.base, p.border, p.dark ? 0.25f : 0.45f);
			dl->AddRectFilled(a, b, styled(fill), r, ImDrawFlags_RoundCornersTop);
			if (selected)
			{
				// the game's dialog tabs: a coloured line under the chosen one
				dl->AddRectFilled(ImVec2(a.x + r * 0.6f, b.y - 3.0f), ImVec2(b.x - r * 0.6f, b.y), styled(focused ? p.accent : mix(p.accent, p.border, 0.5f)), 1.5f);
			}
			else
				dl->AddRect(a, b + ImVec2(0, 1), styled(p.border, 0.6f), r, ImDrawFlags_RoundCornersTop, 1.0f);
			return true;
		}

		ImU32 tabLabelHook(ImGuiTabItemFlags, bool selected)
		{
			return selected ? styled(current.text) : styled(mix(current.text, current.textDim, 0.45f));
		}

		void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 color)
		{
			dl->AddQuadFilled(c + ImVec2(0, -r), c + ImVec2(r, 0), c + ImVec2(0, r), c + ImVec2(-r, 0), color);
		}

		void windowHook(ImGuiWindow* window, const ImRect& titleBar, bool titleHighlight)
		{
			const Palette& p = current;
			const ImGuiWindowFlags flags = window->Flags;
			ImDrawList* dl = window->DrawList;
			const ImVec2 pos = window->Pos;
			const ImVec2 size = window->Size;
			const float r = window->WindowRounding;

			// menus, combos, tooltips: a mint line along the top edge
			if ((flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) && !(flags & ImGuiWindowFlags_Modal))
			{
				dl->AddRectFilled(ImVec2(pos.x + r, pos.y), ImVec2(pos.x + size.x - r, pos.y + 2.0f), styled(p.accent, 0.9f));
				return;
			}
			if ((flags & ImGuiWindowFlags_ChildWindow) && !(flags & ImGuiWindowFlags_Popup))
				return;

			// dialogs: the game's centred title between two mint diamonds, a line under the header
			if (!(flags & ImGuiWindowFlags_NoTitleBar) && !window->DockIsActive && titleBar.GetHeight() > 4.0f)
			{
				const char* end = ImGui::FindRenderedTextEnd(window->Name);
				const float textWidth = ImGui::CalcTextSize(window->Name, end, true).x;
				const ImVec2 c((titleBar.Min.x + titleBar.Max.x) * 0.5f, (titleBar.Min.y + titleBar.Max.y) * 0.5f);
				const float gap = textWidth * 0.5f + 16.0f;
				const float dr = titleBar.GetHeight() * 0.13f;
				diamond(dl, c - ImVec2(gap, 0), dr, styled(p.accent, titleHighlight ? 1.0f : 0.5f));
				diamond(dl, c + ImVec2(gap, 0), dr, styled(p.pink, titleHighlight ? 1.0f : 0.5f));
				dl->AddLine(ImVec2(titleBar.Min.x + r, titleBar.Max.y - 1.0f), ImVec2(titleBar.Max.x - r, titleBar.Max.y - 1.0f), styled(p.line), 1.0f);
			}

			// the main menu bar: a soft shadow under it
			if (flags & ImGuiWindowFlags_MenuBar)
			{
				const ImRect bar = window->MenuBarRect();
				dl->AddLine(ImVec2(bar.Min.x, bar.Max.y - 1.0f), ImVec2(bar.Max.x, bar.Max.y - 1.0f), styled(p.line), 1.0f);
			}
		}

		void setTimelineColors(bool sekai)
		{
			using namespace MikuMikuWorld;
			const Palette& p = current;
			if (sekai)
			{
				// the lanes sit in the game's dark 2D-mode window: keep the lines light, the cursor mint
				cursorColor = col(p.accent);
				measureColor = col(ImVec4(0.92f, 0.92f, 1.0f, 1), 0.78f);
				divColor1 = col(ImVec4(0.80f, 0.80f, 0.92f, 1), 0.62f);
				divColor2 = col(ImVec4(0.80f, 0.80f, 0.92f, 1), 0.26f);
				selectionColor2 = col(p.accent, 0.42f);
			}
			else
			{
				cursorColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.87f, 0.23f, 0.23f, 1.00f));
				measureColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.80f, 0.80f, 0.80f, 0.65f));
				divColor1 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.65f, 0.65f, 0.65f));
				divColor2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.65f, 0.65f, 0.30f));
				selectionColor2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.46f, 1.00f, 0.50f));
			}
		}

		float hash01(int n)
		{
			unsigned int x = static_cast<unsigned int>(n) * 2654435761u;
			x ^= x >> 13;
			x *= 0x5bd1e995u;
			x ^= x >> 15;
			return (x & 0xffffff) / static_cast<float>(0xffffff);
		}
	}

	// ---- public ----------------------------------------------------------------------------------------

	ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
	{
		return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
	}

	float luminance(const ImVec4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

	ImU32 col(const ImVec4& color, float alpha)
	{
		ImVec4 c = color;
		c.w *= alpha;
		return ImGui::ColorConvertFloat4ToU32(c);
	}

	const Palette& palette() { return current; }
	bool active() { return installed; }

	const Sprite* sprite(const std::string& name)
	{
		if (!atlas().built)
			buildAtlas();
		auto it = atlas().sprites.find(name);
		return it != atlas().sprites.end() ? &it->second : nullptr;
	}

	bool hasAssetPack()
	{
		return sprite("ui/btn_round_h56_wh") != nullptr;
	}

	void nineSlice(ImDrawList* dl, const Sprite& s, ImVec2 a, ImVec2 b, ImU32 tint, float cornerScale)
	{
		if (!s.valid() || b.x <= a.x || b.y <= a.y)
			return;
		ImVec4 bd = s.border;
		if (bd.x + bd.y + bd.z + bd.w <= 0.0f)
		{
			dl->AddImage(s.texture, a, b, s.uv0, s.uv1, tint);
			return;
		}
		// corners in screen pixels, shrunk when the rect is too small for them
		float l = bd.x * cornerScale, t = bd.y * cornerScale, r = bd.z * cornerScale, btm = bd.w * cornerScale;
		const float w = b.x - a.x, h = b.y - a.y;
		if (l + r > w) { const float k = w / (l + r); l *= k; r *= k; }
		if (t + btm > h) { const float k = h / (t + btm); t *= k; btm *= k; }
		const float xs[4] = { a.x, a.x + l, b.x - r, b.x };
		const float ys[4] = { a.y, a.y + t, b.y - btm, b.y };
		const float du = s.uv1.x - s.uv0.x, dv = s.uv1.y - s.uv0.y;
		const float us[4] = { s.uv0.x, s.uv0.x + du * bd.x / s.size.x, s.uv1.x - du * bd.z / s.size.x, s.uv1.x };
		const float vs[4] = { s.uv0.y, s.uv0.y + dv * bd.y / s.size.y, s.uv1.y - dv * bd.w / s.size.y, s.uv1.y };
		for (int iy = 0; iy < 3; ++iy)
			for (int ix = 0; ix < 3; ++ix)
				if (xs[ix + 1] > xs[ix] + 0.01f && ys[iy + 1] > ys[iy] + 0.01f)
					dl->AddImage(s.texture, ImVec2(xs[ix], ys[iy]), ImVec2(xs[ix + 1], ys[iy + 1]), ImVec2(us[ix], vs[iy]), ImVec2(us[ix + 1], vs[iy + 1]), tint);
	}

	void spriteFit(ImDrawList* dl, const Sprite& s, ImVec2 a, ImVec2 b, ImU32 tint)
	{
		if (!s.valid())
			return;
		const float w = b.x - a.x, h = b.y - a.y;
		const float k = std::min(w / s.size.x, h / s.size.y);
		const ImVec2 size = s.size * k;
		const ImVec2 o = a + (ImVec2(w, h) - size) * 0.5f;
		dl->AddImage(s.texture, o, o + size, s.uv0, s.uv1, tint);
	}

	namespace
	{
		// the selected tool: a faint grey disc right behind the icon
		void selectedDisc(ImVec2 a, ImVec2 b)
		{
			const ImVec2 c = (a + b) * 0.5f;
			const float r = std::min(b.x - a.x, b.y - a.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddCircleFilled(c, r, ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.6f, 0.18f)), 40);
		}
	}

	bool glyphButton(const char* id, const char* glyph, ImVec2 size, bool selected, bool enabled)
	{
		ImGui::BeginDisabled(!enabled);
		const bool pressed = ImGui::Button(id, size);
		const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		if (selected)
			selectedDisc(a, b);
		// draw the icon, then move its vertices so its ink sits in the middle of the button
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const int start = dl->VtxBuffer.Size;
		dl->AddText(a, ImGui::GetColorU32(ImGuiCol_Text), glyph);
		ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
		for (int i = start; i < dl->VtxBuffer.Size; ++i)
		{
			lo = ImMin(lo, dl->VtxBuffer[i].pos);
			hi = ImMax(hi, dl->VtxBuffer[i].pos);
		}
		if (dl->VtxBuffer.Size > start)
		{
			const ImVec2 shift = ImFloor((a + b) * 0.5f - (lo + hi) * 0.5f);
			for (int i = start; i < dl->VtxBuffer.Size; ++i)
				dl->VtxBuffer[i].pos += shift;
		}
		ImGui::EndDisabled();
		return pressed;
	}

	bool spriteButton(const char* id, const std::string& name, ImVec2 size, bool selected, bool enabled, bool flipX)
	{
		const Sprite* s = sprite(name);
		ImGui::BeginDisabled(!enabled);
		const bool pressed = ImGui::Button(id, size);
		if (selected)
			selectedDisc(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		if (s)
		{
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			const ImVec2 pad(size.x * 0.14f, size.y * 0.14f);
			// white icons are tinted with the text colour; coloured note icons keep theirs
			const bool white = name.rfind("ui/", 0) == 0;
			const ImU32 tint = white ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImVec4(1, 1, 1, 1));
			// icons fill the button: their transparent margins are trimmed
			Sprite shown = *s;
			shown.uv0 = s->contentUv0;
			shown.uv1 = s->contentUv1;
			shown.size = s->contentSize;
			if (flipX)
				std::swap(shown.uv0.x, shown.uv1.x);
			spriteFit(ImGui::GetWindowDrawList(), shown, a + pad, b - pad, tint);
		}
		ImGui::EndDisabled();
		return pressed;
	}

	void apply(ImGuiStyle& style, const ImVec4& accentIn, bool dark)
	{
		Palette& p = current;
		p.dark = dark;
		p.accent = withAlpha(accentIn, 1.0f);
		p.accentDark = mix(p.accent, ImVec4(0, 0, 0, 1), 0.25f);
		p.accentLight = mix(p.accent, ImVec4(1, 1, 1, 1), 0.45f);
		p.onAccent = luminance(p.accent) > 0.55f ? rgb(0x44, 0x44, 0x66) : rgb(255, 255, 255);
		p.pink = rgb(0xff, 0x66, 0x99);
		if (dark)
		{
			p.base = rgb(0x26, 0x26, 0x3a);
			p.panel = rgb(0x2e, 0x2e, 0x46);
			p.card = rgb(0x3a, 0x3a, 0x56);
			p.frame = rgb(0x22, 0x22, 0x36);
			p.border = rgb(0x55, 0x55, 0x75);
			p.line = rgb(0x4a, 0x4a, 0x68);
			p.text = rgb(0xeb, 0xeb, 0xf2);
			p.textDim = rgb(0xa7, 0xa7, 0xbc);
		}
		else
		{
			// the game's colours: #EBEBF2 bases, #BCBCD0 lines, #444466 text, #A7A7BC dimmed text
			p.base = rgb(0xeb, 0xeb, 0xf2);
			p.panel = rgb(0xf4, 0xf4, 0xf8);
			p.card = rgb(0xff, 0xff, 0xff);
			p.frame = rgb(0xff, 0xff, 0xff);
			p.border = rgb(0xbc, 0xbc, 0xd0);
			p.line = rgb(0xd8, 0xd8, 0xe4);
			p.text = rgb(0x44, 0x44, 0x66);
			p.textDim = rgb(0xa7, 0xa7, 0xbc);
		}

		if (dark)
			ImGui::StyleColorsDark(&style);
		else
			ImGui::StyleColorsLight(&style);
		style.WindowPadding = ImVec2(12, 10);
		style.FramePadding = ImVec2(10, 5);
		style.CellPadding = ImVec2(6, 4);
		style.ItemSpacing = ImVec2(8, 6);
		style.ItemInnerSpacing = ImVec2(6, 5);
		style.IndentSpacing = 18;
		style.ScrollbarSize = 12;
		style.GrabMinSize = 12;
		style.WindowRounding = 16;
		style.ChildRounding = 12;
		style.FrameRounding = 10;
		style.PopupRounding = 12;
		style.ScrollbarRounding = 8;
		style.GrabRounding = 10;
		style.TabRounding = 10;
		style.WindowBorderSize = 1;
		style.ChildBorderSize = 1;
		style.PopupBorderSize = 1;
		style.FrameBorderSize = 1;
		style.TabBorderSize = 0;
		style.TabBarBorderSize = 1;
		style.TabBarOverlineSize = 0;
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.SeparatorTextBorderSize = 2;
		style.SeparatorTextPadding = ImVec2(14, 4);
		style.DockingSeparatorSize = 3;
		style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

		ImVec4* c = style.Colors;
		const ImVec4 none(0, 0, 0, 0);
		c[ImGuiCol_Text] = p.text;
		c[ImGuiCol_TextDisabled] = p.textDim;
		c[ImGuiCol_WindowBg] = p.panel;
		c[ImGuiCol_ChildBg] = none;
		c[ImGuiCol_PopupBg] = p.card;
		c[ImGuiCol_Border] = p.border;
		c[ImGuiCol_BorderShadow] = none;
		c[ImGuiCol_FrameBg] = p.frame;
		c[ImGuiCol_FrameBgHovered] = mix(p.frame, p.accent, 0.06f);
		c[ImGuiCol_FrameBgActive] = mix(p.frame, p.accent, 0.12f);
		c[ImGuiCol_TitleBg] = p.card;
		c[ImGuiCol_TitleBgActive] = p.card;
		c[ImGuiCol_TitleBgCollapsed] = p.base;
		c[ImGuiCol_MenuBarBg] = p.card;
		c[ImGuiCol_ScrollbarBg] = none;
		c[ImGuiCol_ScrollbarGrab] = withAlpha(p.border, 0.9f);
		c[ImGuiCol_ScrollbarGrabHovered] = withAlpha(p.accent, 0.75f);
		c[ImGuiCol_ScrollbarGrabActive] = p.accent;
		c[ImGuiCol_CheckMark] = p.accent;
		c[ImGuiCol_SliderGrab] = p.accent;
		c[ImGuiCol_SliderGrabActive] = p.accentDark;
		c[ImGuiCol_Button] = p.card;
		c[ImGuiCol_ButtonHovered] = mix(p.card, p.accent, 0.10f);
		c[ImGuiCol_ButtonActive] = mix(p.card, p.accent, 0.28f);
		c[ImGuiCol_Header] = mix(p.card, p.base, 0.35f);
		c[ImGuiCol_HeaderHovered] = mix(p.card, p.accent, 0.14f);
		c[ImGuiCol_HeaderActive] = mix(p.card, p.accent, 0.26f);
		c[ImGuiCol_Separator] = p.line;
		c[ImGuiCol_SeparatorHovered] = withAlpha(p.accent, 0.7f);
		c[ImGuiCol_SeparatorActive] = p.accent;
		c[ImGuiCol_ResizeGrip] = withAlpha(p.accent, 0.15f);
		c[ImGuiCol_ResizeGripHovered] = withAlpha(p.accent, 0.5f);
		c[ImGuiCol_ResizeGripActive] = withAlpha(p.accent, 0.85f);
		c[ImGuiCol_InputTextCursor] = p.accentDark;
		c[ImGuiCol_Tab] = p.base;
		c[ImGuiCol_TabHovered] = mix(p.card, p.accent, 0.12f);
		c[ImGuiCol_TabSelected] = p.accent;
		c[ImGuiCol_TabSelectedOverline] = p.accent;
		c[ImGuiCol_TabDimmed] = p.base;
		c[ImGuiCol_TabDimmedSelected] = p.card;
		c[ImGuiCol_TabDimmedSelectedOverline] = none;
		c[ImGuiCol_DockingPreview] = withAlpha(p.accent, 0.45f);
		c[ImGuiCol_DockingEmptyBg] = p.base;
		c[ImGuiCol_PlotLines] = p.accent;
		c[ImGuiCol_PlotLinesHovered] = p.pink;
		c[ImGuiCol_PlotHistogram] = p.accent;
		c[ImGuiCol_PlotHistogramHovered] = p.pink;
		c[ImGuiCol_TableHeaderBg] = mix(p.base, p.card, 0.4f);
		c[ImGuiCol_TableBorderStrong] = p.border;
		c[ImGuiCol_TableBorderLight] = p.line;
		c[ImGuiCol_TableRowBg] = none;
		c[ImGuiCol_TableRowBgAlt] = dark ? ImVec4(1, 1, 1, 0.03f) : ImVec4(0.27f, 0.27f, 0.4f, 0.035f);
		c[ImGuiCol_TextLink] = p.accentDark;
		c[ImGuiCol_TextSelectedBg] = withAlpha(p.accent, 0.35f);
		c[ImGuiCol_TreeLines] = p.border;
		c[ImGuiCol_DragDropTarget] = p.pink;
		c[ImGuiCol_NavCursor] = p.accent;
		c[ImGuiCol_NavWindowingHighlight] = withAlpha(p.accent, 0.7f);
		c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0.133f, 0.35f);
		// the game's dialog backdrop: #000022 at 50%
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0.133f, 0.5f);

		for (int i = 0; i < ImGuiCol_COUNT; ++i)
			styleColors[i] = c[i];
		GImGuiSkin.Frame = frameHook;
		GImGuiSkin.TabBackground = tabHook;
		GImGuiSkin.TabLabelColor = tabLabelHook;
		GImGuiSkin.WindowDecorations = windowHook;
		GImGuiSkin.Slide = []() { Canvas::Sekai::noteSlide(); };
		installed = true;
		setTimelineColors(true);
	}

	void uninstall()
	{
		GImGuiSkin = ImGuiSkinHooks{};
		if (installed)
			setTimelineColors(false);
		installed = false;
	}

	namespace
	{
		std::unique_ptr<MikuMikuWorld::Texture>& skyTexture()
		{
			static std::unique_ptr<MikuMikuWorld::Texture> sky;
			return sky;
		}

		bool& skyLoadTried()
		{
			static bool tried = false;
			return tried;
		}
	}

	void releaseGlResources()
	{
		skyTexture().reset();
		skyLoadTried() = true;	// never reload: the context is going away
	}

	void drawBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, double time, float avoidX0, float avoidX1)
	{
		const Palette& p = current;
		const float w = b.x - a.x, h = b.y - a.y;
		if (w <= 1 || h <= 1)
			return;
		dl->PushClipRect(a, b, true);

		// the SEKAI sky: bg_sekai.png covering the area, else a pale blue-white gradient
		auto& sky = skyTexture();
		bool& skyTried = skyLoadTried();
		if (!skyTried)
		{
			skyTried = true;
			const std::string file = packDir() + "bg/bg_sekai.png";
			if (IO::File::exists(file))
				sky = std::make_unique<MikuMikuWorld::Texture>(file);
		}
		if (sky && sky->getID())
		{
			const float k = std::max(w / sky->getWidth(), h / sky->getHeight());
			const ImVec2 size(sky->getWidth() * k, sky->getHeight() * k);
			const ImVec2 o = a + (ImVec2(w, h) - size) * 0.5f;
			dl->AddImage(static_cast<ImTextureID>(sky->getID()), o, o + size, ImVec2(0, 0), ImVec2(1, 1), p.dark ? IM_COL32(120, 120, 165, 255) : IM_COL32_WHITE);
		}
		else
		{
			const ImU32 edge = p.dark ? col(rgb(0x2a, 0x2c, 0x55)) : col(rgb(0xbf, 0xd5, 0xff));
			const ImU32 centre = p.dark ? col(rgb(0x3d, 0x3f, 0x70)) : col(rgb(0xff, 0xff, 0xff));
			dl->AddRectFilledMultiColor(a, b, edge, edge, centre, centre);
		}

		// floating circles and triangles, drifting up and turning slowly
		static const char* shapes[] = { "bg/circle_white", "bg/circle_pink", "bg/triangle_pink", "bg/triangle_mint" };
		const float unit = std::max(w, h);
		for (int i = 0; i < 16; ++i)
		{
			const int kind = static_cast<int>(hash01(i * 5 + 1) * 4) % 4;
			const float size = unit * (0.025f + 0.045f * hash01(i * 5 + 2));
			const float speed = 0.006f + 0.012f * hash01(i * 5 + 3);
			const float x = a.x + w * hash01(i * 5 + 4) + std::sin(static_cast<float>(time) * 0.2f + i) * size * 0.4f;
			const float y = b.y + size - std::fmod(static_cast<float>(time) * speed * h + hash01(i * 5 + 5) * (h + size * 2), h + size * 2);
			const float angle = static_cast<float>(time) * (0.15f + 0.3f * hash01(i * 7)) * (i % 2 ? 1.0f : -1.0f);
			float alpha = 0.45f + 0.4f * hash01(i * 11);
			// fade out over the lanes (the lane window is translucent)
			if (avoidX1 > avoidX0)
			{
				const float d = std::max(avoidX0 - (x + size * 0.5f), (x - size * 0.5f) - avoidX1);
				alpha *= std::clamp(d / (size * 0.5f) + 0.0f, 0.0f, 1.0f);
				if (alpha <= 0.01f)
					continue;
			}
			const ImVec2 c(x, y);
			const Sprite* s = sprite(shapes[kind]);
			if (s && s->valid())
			{
				ImVec2 q[4];
				for (int k = 0; k < 4; ++k)
				{
					const float ang = angle + IM_PI * 0.25f + k * IM_PI * 0.5f;
					q[k] = c + ImVec2(std::cos(ang), std::sin(ang)) * (size * 0.7071f);
				}
				dl->AddImageQuad(s->texture, q[0], q[1], q[2], q[3], ImVec2(s->uv1.x, s->uv0.y), s->uv1, ImVec2(s->uv0.x, s->uv1.y), s->uv0, col(ImVec4(1, 1, 1, 1), alpha));
			}
			else
			{
				const ImVec4 tint = kind == 0 ? ImVec4(1, 1, 1, 1) : kind == 1 || kind == 2 ? rgb(0xff, 0xb8, 0xde) : rgb(0x00, 0xff, 0xdd);
				if (kind < 2)
					dl->AddCircle(c, size * 0.45f, col(tint, alpha), 40, size * 0.04f);
				else
				{
					ImVec2 t[3];
					for (int k = 0; k < 3; ++k)
					{
						const float ang = angle - IM_PI * 0.5f + k * IM_PI * 2.0f / 3.0f;
						t[k] = c + ImVec2(std::cos(ang), std::sin(ang)) * size * 0.5f;
					}
					dl->AddTriangleFilled(t[0], t[1], t[2], col(tint, alpha * 0.8f));
				}
			}
		}
		dl->PopClipRect();
	}

	void drawLogo(ImDrawList* dl, ImVec2 a, float size, ImTextureID customIcon)
	{
		const ImVec2 b = a + ImVec2(size, size);
		if (customIcon)
		{
			dl->AddImageRounded(customIcon, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.22f);
			return;
		}
		// without a loaded icon texture: a small prism
		const ImVec2 c = (a + b) * 0.5f;
		const float r = size * 0.45f;
		dl->AddQuadFilled(c + ImVec2(0, -r), c + ImVec2(r, 0), c + ImVec2(0, r), c + ImVec2(-r, 0), col(current.accent));
	}

	void ToolbarBar::begin()
	{
		drawList = ImGui::GetWindowDrawList();
		drawList->ChannelsSplit(2);
		drawList->ChannelsSetCurrent(1);
		inGroup = false;
	}

	void ToolbarBar::group()
	{
		if (inGroup)
			endGroup();
		groupStart = ImGui::GetCursorScreenPos();
		inGroup = true;
	}

	void ToolbarBar::endGroup()
	{
		if (!inGroup || !drawList)
			return;
		inGroup = false;
		const Palette& p = current;
		const ImVec2 last = ImGui::GetItemRectMax();
		const ImVec2 pad(6, 3);
		const ImVec2 a = groupStart - pad;
		const ImVec2 b = ImVec2(last.x, std::max(last.y, groupStart.y + ImGui::GetFrameHeight())) + pad;
		drawList->ChannelsSetCurrent(0);
		const float r = (b.y - a.y) * 0.5f;
		drawList->AddRectFilled(a + ImVec2(0, 2), b + ImVec2(0, 2), col(ImVec4(0.27f, 0.27f, 0.4f, 1), p.dark ? 0.35f : 0.12f), r);
		drawList->AddRectFilled(a, b, col(p.card), r);
		drawList->AddRect(a, b, col(p.border, 0.8f), r, 0, 1.0f);
		drawList->ChannelsSetCurrent(1);
		ImGui::SameLine(0, 18);
	}

	void ToolbarBar::end()
	{
		if (inGroup)
			endGroup();
		if (drawList)
			drawList->ChannelsMerge();
		drawList = nullptr;
	}

	void sectionTitle(const char* text)
	{
		const Palette& p = current;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (headerFont)
			ImGui::PushFont(headerFont, ImGui::GetFontSize() * 1.1f);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const float h = ImGui::GetTextLineHeight();
		diamond(dl, pos + ImVec2(6, h * 0.5f), h * 0.22f, col(p.accent));
		ImGui::SetCursorScreenPos(pos + ImVec2(18, 0));
		ImGui::TextUnformatted(text);
		if (headerFont)
			ImGui::PopFont();
		const ImVec2 end = ImGui::GetCursorScreenPos();
		dl->AddLine(ImVec2(end.x, end.y - 2), ImVec2(end.x + ImGui::GetContentRegionAvail().x, end.y - 2), col(p.line), 1.0f);
		ImGui::Dummy(ImVec2(0, 2));
	}

	void badge(const char* text, const ImVec4& color)
	{
		const ImVec2 size = ImGui::CalcTextSize(text) + ImVec2(14, 4);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(pos, pos + size, col(color), size.y * 0.5f);
		dl->AddText(pos + ImVec2(7, 2), col(luminance(color) > 0.6f ? current.text : ImVec4(1, 1, 1, 1)), text);
		ImGui::Dummy(size);
	}
}
