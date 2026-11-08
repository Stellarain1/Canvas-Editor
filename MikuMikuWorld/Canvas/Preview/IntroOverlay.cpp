#include "IntroOverlay.h"
#include <algorithm>
#include <cctype>
#include <cmath>

// CANVAS_INTRO_PURE: the tests include this file on its own (ImGui only, no skin / score)
#ifndef CANVAS_INTRO_PURE
#include "LiveOverlay.h"
#include "../../ScoreContext.h"
#include "../../Rendering/Texture.h"
#include "../UI/SekaiStyle.h"
#endif

namespace Canvas::Preview
{
	namespace
	{
		constexpr float PI = 3.14159265358979f;
		float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
		// AviUtl's "減速" (decelerate) move, close enough: fast at first, settles
		float decel(float t) { t = clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

		std::string difficultyKey;
		int difficultyLevel = 0;

		std::string lower(const std::string& name)
		{
			std::string k;
			for (char c : name)
				k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return k;
		}

		ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, static_cast<int>(clamp01(a) * 255)); }

		// the colour of APPEND's {difficulty}_bg.png, for the text and the icon that wear it
		ImU32 difficultyColor(const std::string& name, float alpha)
		{
			const std::string k = lower(name);
			int r = 203, g = 50, b = 255;	// master, also what an unknown name gets
			if (k == "easy") { r = 136; g = 236; b = 58; }
			else if (k == "normal") { r = 96; g = 184; b = 232; }
			else if (k == "hard") { r = 243; g = 174; b = 60; }
			else if (k == "expert") { r = 235; g = 60; b = 89; }
			else if (k == "append") { r = 238; g = 130; b = 242; }
			else if (k == "eternal") { r = 156; g = 181; b = 222; }
			return rgba(r, g, b, alpha);
		}

		struct Frame
		{
			ImDrawList* dl;
			ImVec2 pos, size;
			float ui;
			ImVec2 origin;	// the middle of the picture: the exo's (0,0)
			ImVec2 at(float x, float y) const { return origin + ImVec2(x, y) * ui; }
		};

		// an image centred on `centre`, `px` 1080p pixels big, turned by `turnDeg`
		void image(const Frame& F, ImTextureID tex, ImVec2 centre, ImVec2 px, ImU32 tint, float turnDeg = 0.0f)
		{
			if (!tex || (tint >> IM_COL32_A_SHIFT) == 0)
				return;
			const ImVec2 h = px * 0.5f * F.ui;
			if (turnDeg == 0.0f)
			{
				F.dl->AddImage(tex, centre - h, centre + h, ImVec2(0, 0), ImVec2(1, 1), tint);
				return;
			}
			const float s = std::sin(turnDeg * PI / 180.0f), co = std::cos(turnDeg * PI / 180.0f);
			auto rot = [&](float x, float y) { return centre + ImVec2(x * co - y * s, x * s + y * co); };
			F.dl->AddImageQuad(tex, rot(-h.x, -h.y), rot(h.x, -h.y), rot(h.x, h.y), rot(-h.x, h.y),
				ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), tint);
		}

		// a flat square for what has no art (no jacket, no tile image)
		void square(const Frame& F, ImVec2 centre, float side, ImU32 color, float turnDeg)
		{
			const ImVec2 h = ImVec2(side, side) * 0.5f * F.ui;
			const float s = std::sin(turnDeg * PI / 180.0f), co = std::cos(turnDeg * PI / 180.0f);
			auto rot = [&](float x, float y) { return centre + ImVec2(x * co - y * s, x * s + y * co); };
			F.dl->AddQuadFilled(rot(-h.x, -h.y), rot(h.x, -h.y), rot(h.x, h.y), rot(-h.x, h.y), color);
		}

		// the picture-sized background, `zoom` times its cover size (the exo draws it at 110 / 120%)
		void background(const Frame& F, const IntroInfo& info, float zoom)
		{
			if (!info.background)
			{
				F.dl->AddRectFilled(F.pos, F.pos + F.size, IM_COL32(0x20, 0x20, 0x40, 255));
				return;
			}
			const float s = std::max(F.size.x / info.backgroundSize.x, F.size.y / info.backgroundSize.y) * zoom;
			const ImVec2 h = info.backgroundSize * s * 0.5f, mid = F.pos + F.size * 0.5f;
			F.dl->AddImage(info.background, mid - h, mid + h, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
		}

		// text at `px` 1080p pixels, anchored like AviUtl's text alignment: hAlign 0 = `x` is the left
		// edge, 1 = the middle; vAlign 0 = `y` is the top, 1 = the bottom. Shrinks to fit `maxWidth`.
		void text(const Frame& F, const IntroInfo& info, const std::string& s, float x, float y, float px, int hAlign, int vAlign, ImU32 color, float maxWidth = 0.0f)
		{
			if (s.empty() || (color >> IM_COL32_A_SHIFT) == 0)
				return;
			ImFont* font = info.font ? info.font : ImGui::GetFont();
			float size = px * F.ui;
			ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
			if (maxWidth > 0 && extent.x > maxWidth * F.ui)
			{
				size *= maxWidth * F.ui / extent.x;
				extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
			}
			ImVec2 p = F.at(x, y);
			if (hAlign == 1)
				p.x -= extent.x * 0.5f;
			if (vAlign == 1)
				p.y -= extent.y;
			F.dl->AddText(font, size, p, color, s.c_str());
		}

		std::string withLevel(const IntroInfo& info)
		{
			std::string name = info.difficulty;
			for (char& c : name)
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			return info.level > 0 && !name.empty() ? name + " " + std::to_string(info.level) : name;
		}

		// the light streak (start_grad.png, 150%, 10% opaque) rising from below
		void streak(const Frame& F, const IntroInfo& info, const IntroState& st, float toY, float fade)
		{
			if (st.sweep < 0.0f || !info.gradient)
				return;
			const float y = 1500.0f + (toY - 1500.0f) * decel(st.sweep);
			image(F, info.gradient, F.at(0, y), info.gradientSize * 1.5f, rgba(255, 255, 255, 0.1f * fade));
		}

		// modern HUD, main_jp.exo
		void drawModern(const Frame& F, const IntroInfo& info, const IntroState& st)
		{
			const float a = st.fade;
			const ImU32 white = rgba(255, 255, 255, a);
			// obj 0: background.png at 120%; obj 1: #68689c over it at 80%
			background(F, info, 1.2f);
			// the exo's own background.png is a blurred, darkened render of the stage; ours is the raw
			// picture, so the veil is heavier and the colour is the flat purple-grey of the game's intro
			F.dl->AddRectFilled(F.pos, F.pos + F.size, IM_COL32(0x49, 0x48, 0x68, 235));
			streak(F, info, st, 0.0f, 1.0f);

			// obj 5 moves the tile, its text and the icon together: (43,-43) -> (0,0) over frames 1..112
			const ImVec2 off = ImVec2(43.0f, -43.0f) * (1.0f - decel(st.drift));
			if (info.tile)
				image(F, info.tile, F.at(-661.5f + off.x, 288.0f + off.y), ImVec2(402.0f, 402.0f), white);	// obj 8: {difficulty}_bg.png at 39.26%
			else
				square(F, F.at(-661.5f + off.x, 288.0f + off.y), 402.0f, difficultyColor(info.difficulty, a), 0.0f);
			// obj 12: the difficulty, left + bottom aligned at (-811, 486), 84 pt at 38%
			text(F, info, withLevel(info), -811.0f + off.x, 486.0f + off.y, 32.0f, 0, 1, white, 340.0f);
			image(F, info.sheet, F.at(-834.8f + off.x, 468.2f + off.y), info.sheetSize * 0.6513f, white);	// obj 14

			// obj 35: cover.png at 78.75%
			if (info.jacket)
				image(F, info.jacket, F.at(-618.0f, 245.0f), ImVec2(403.0f, 403.0f), white);
			else
				square(F, F.at(-618.0f, 245.0f), 403.0f, rgba(30, 30, 40, a), 0.0f);

			// the text column: left + top aligned (objs 17, 19, 21, 23, 33)
			const float w = 1300.0f;
			text(F, info, info.extra, -380.0f, 91.0f, 27.0f, 0, 0, white, w);
			text(F, info, info.title, -377.5f, 165.0f, 38.4f, 0, 0, white, w);
			text(F, info, info.credits[0], -380.0f, 254.5f, 27.0f, 0, 0, white, w);
			text(F, info, info.credits[1], -380.0f, 304.5f, 27.0f, 0, 0, white, w);
			if (!info.charter.empty())
			{
				image(F, info.person, F.at(-354.7f, 470.0f), info.personSize * 1.2f, white);	// obj 29
				text(F, info, "Charted by: " + info.charter, -323.0f, 453.0f, 27.0f, 0, 0, white, w - 60.0f);
			}
		}

		// 2.8.0 HUD, v1-skin_jp.exo
		void drawV1(const Frame& F, const IntroInfo& info, const IntroState& st)
		{
			const float a = st.fade;
			const ImU32 white = rgba(255, 255, 255, a);
			// obj 0: background-v1.png at 110%
			background(F, info, 1.1f);
			// obj 3: start_bg.png tiled, 144%, 80% opaque (it fades out with the rest)
			if (info.startBg && a > 0.01f)
			{
				const float cell = 16.0f * 1.44f * F.ui;
				const ImU32 tint = rgba(255, 255, 255, 0.8f * a);
				const int nx = static_cast<int>(F.size.x / cell) / 2 + 2, ny = static_cast<int>(F.size.y / cell) / 2 + 2;
				const ImVec2 mid = F.origin;
				for (int j = -ny; j <= ny; ++j)
					for (int i = -nx; i <= nx; ++i)
					{
						const ImVec2 p0 = mid + ImVec2((i - 0.5f) * cell, (j - 0.5f) * cell);
						F.dl->AddImage(info.startBg, p0, p0 + ImVec2(cell, cell), ImVec2(0, 0), ImVec2(1, 1), tint);
					}
			}
			streak(F, info, st, -600.0f, a);

			// objs 7-9: the difficulty tile behind the jacket, 54.75% (560 px), turns to -4.6 deg
			const ImVec2 mid = F.at(0.0f, -124.5f);
			if (info.tile)
				image(F, info.tile, mid, ImVec2(560.0f, 560.0f), white, st.tilt);
			else
				square(F, mid, 560.0f, difficultyColor(info.difficulty, a), st.tilt);
			// obj 13: cover.png at 109.5%
			if (info.jacket)
				image(F, info.jacket, mid, ImVec2(560.0f, 560.0f), white);
			else
				square(F, mid, 560.0f, rgba(30, 30, 40, a), 0.0f);

			// objs 15 + 16: the music sheet icon at (-90, 211), the difficulty colour filled in
			image(F, info.sheet, F.at(-90.0f, 211.0f), info.sheetSize * 0.6513f, difficultyColor(info.difficulty, a));
			// obj 18: the difficulty, middle + top aligned at (25, 187), 84 pt at 43%
			text(F, info, withLevel(info), 25.0f, 187.0f, 36.0f, 1, 0, difficultyColor(info.difficulty, a), 160.0f);

			// objs 30, 32, 34: title (96 pt at 54%), the two credit lines (72 pt at 38.75%), centred, top aligned
			text(F, info, info.title, 0.0f, 253.0f, 52.0f, 1, 0, white, 1700.0f);
			text(F, info, info.credits[0], 0.0f, 342.5f, 28.0f, 1, 0, white, 1700.0f);
			const std::string charted = info.charter.empty() ? std::string() : "Charted by: " + info.charter;
			text(F, info, !charted.empty() ? charted : info.credits[1], 0.0f, 397.0f, 28.0f, 1, 0, white, 1700.0f);
		}
	}

	IntroState introState(float seconds)
	{
		IntroState s;
		const float f = std::max(0.0f, seconds) * 60.0f;
		s.frame = f;
		s.flash = 1.0f - clamp01(f / 60.0f);
		s.fade = 1.0f - clamp01((f - 270.0f) / 30.0f);
		s.drift = clamp01(f / 112.0f);
		s.tilt = -4.6f * decel((f - 45.0f) / 15.0f);
		if (f >= 181.0f)
			s.sweep = clamp01((f - 181.0f) / 120.0f);
		else if (f >= 61.0f)
			s.sweep = clamp01((f - 61.0f) / 120.0f);
		else
			s.sweep = -1.0f;
		return s;
	}

	bool drawIntro(ImDrawList* dl, ImVec2 pos, ImVec2 size, float seconds, const IntroInfo& info, bool ui280)
	{
		if (!dl || size.x <= 1.0f || size.y <= 1.0f || seconds < 0.0f || seconds >= INTRO_SECONDS)
			return false;

		Frame F;
		F.dl = dl;
		F.pos = pos;
		F.size = size;
		F.ui = std::min(size.x / 1920.0f, size.y / 1080.0f);
		F.origin = pos + size * 0.5f;	// the exo's coordinates are centre-based (0,0 = the middle)

		const IntroState st = introState(seconds);
		dl->PushClipRect(pos, pos + size, true);
		if (ui280)
			drawV1(F, info, st);
		else
			drawModern(F, info, st);
		// the white flash over everything
		if (st.flash > 0.002f)
			dl->AddRectFilled(pos, pos + size, rgba(255, 255, 255, st.flash));
		dl->PopClipRect();
		return true;
	}

	void setIntroDifficulty(const std::string& difficulty, int level)
	{
		difficultyKey = difficulty;
		difficultyLevel = level;
	}

	namespace { double introClock = -1.0; }

	void setIntroClock(double seconds)
	{
		introClock = seconds;
	}

#ifndef CANVAS_INTRO_PURE
	bool drawIntroFor(ImDrawList* dl, ImVec2 pos, ImVec2 size, const MikuMikuWorld::ScoreContext& context, const OverlayOptions& options)
	{
		if (!options.intro)
			return false;
		const double now = introClock >= 0.0 ? introClock : context.getTimeAtCurrentTick();
		if (now < 0.0 || now >= INTRO_SECONDS)
			return false;

		const ScoreSkin& skin = activeSkin();
		auto tex = [&](const std::string& name, ImVec2& imageSize) {
			const MikuMikuWorld::Texture* t = skin.texture(name);
			if (!t)
				return ImTextureID{};
			imageSize = ImVec2(static_cast<float>(t->getWidth()), static_cast<float>(t->getHeight()));
			return static_cast<ImTextureID>(t->getID());
		};
		ImVec2 unused;

		IntroInfo info;
		const auto& data = context.workingData;
		const auto& meta = context.score.metadata;
		info.title = !data.title.empty() ? data.title : meta.title;
		info.artist = !data.artist.empty() ? data.artist : meta.artist;
		info.charter = !data.designer.empty() ? data.designer : meta.author;
		info.difficulty = !options.introDifficulty.empty() ? options.introDifficulty : difficultyKey;
		info.level = options.introLevel > 0 ? options.introLevel : difficultyLevel;
		// lyricist / composer / arranger / vocals are not in a chart: the artist stands in
		if (!info.artist.empty())
			info.credits[0] = "Music: " + info.artist;
		if (data.jacket.getTexID() > 0)
			info.jacket = static_cast<ImTextureID>(data.jacket.getTexID());
		info.gradient = tex("assets/intro/start_grad.png", info.gradientSize);
		info.sheet = tex("assets/intro/music-sheet.png", info.sheetSize);
		info.person = tex("assets/intro/person.png", info.personSize);
		info.background = tex(options.ui280 ? "assets/intro/background-v1.png" : "assets/intro/background.png", info.backgroundSize);
		info.startBg = tex("assets/intro/start_bg.png", unused);
		const std::string key = lower(info.difficulty);
		info.tile = tex("assets/intro/" + (key.empty() ? std::string("master") : key) + "_bg.png", unused);
		if (!info.tile && !key.empty())
			info.tile = tex("assets/intro/master_bg.png", unused);
		info.font = Canvas::SekaiStyle::headerFont;
		return drawIntro(dl, pos, size, static_cast<float>(now), info, options.ui280);
	}
#endif
}
