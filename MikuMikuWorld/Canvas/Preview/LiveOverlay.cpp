#include "LiveOverlay.h"
#include "IntroOverlay.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../ScoreContext.h"
#include "../../Rendering/Texture.h"
#include "../../File.h"
#include "../../Constants.h"
#include "../UI/SekaiStyle.h"
#include "../Video/MoviePlayer.h"
#include "../Core/Profiler.h"
#include "../../Tempo.h"
#include "LiveCards.h"
#include "../../ImGui/imgui_internal.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace MikuMikuWorld;

namespace Canvas::Preview
{
	namespace
	{
		constexpr float PI = 3.14159265358979f;

		float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
		float easeOut(float t) { t = clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t); }
		float easeIn(float t) { t = clamp01(t); return t * t; }
		float easeOutBack(float t)
		{
			t = clamp01(t);
			const float c1 = 1.70158f, c3 = c1 + 1.0f;
			return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
		}
		// 0 -> 1 over [a, b], 1 until c, back to 0 over [c, d]
		float window4(float t, float a, float b, float c, float d)
		{
			if (t <= a || t >= d)
				return 0.0f;
			if (t < b)
				return (t - a) / (b - a);
			if (t <= c)
				return 1.0f;
			return 1.0f - (t - c) / (d - c);
		}

		ImU32 col(const ImVec4& c, float alpha = 1.0f)
		{
			return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * clamp01(alpha)));
		}
		ImVec4 hex(unsigned int rgb, float a = 1.0f)
		{
			return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a);
		}
		ImVec4 mixc(const ImVec4& a, const ImVec4& b, float t) { return Canvas::SekaiStyle::mix(a, b, clamp01(t)); }

		ImTextureID texId(const Texture& t) { return static_cast<ImTextureID>(t.getID()); }

		// ---- frame mapping: the game's 1920x1080 layout into the preview rect -------------------
		struct Layout
		{
			ImDrawList* dl{};
			ImVec2 pos, size;
			float ui{ 1 };			// pixels per layout unit
			float offsetX{}, offsetY{};
			ImVec2 lt, rt;			// the HUD's top-left / top-right anchors
			ImVec2 at(float x, float y) const { return ImVec2(pos.x + offsetX + x * ui, pos.y + offsetY + y * ui); }
			// a 1080p spot that sticks to the right / left edge when the picture is narrower than 16:9
			// (like the game on a 4:3 tablet: the combo hugs the right, the skill panel the left)
			ImVec2 atRight(float x, float y) const { return offsetX < 0 ? ImVec2(pos.x + size.x - (1920.0f - x) * ui, pos.y + offsetY + y * ui) : at(x, y); }
			ImVec2 atLeft(float x, float y) const { return offsetX < 0 ? ImVec2(pos.x + x * ui, pos.y + offsetY + y * ui) : at(x, y); }
			// HUD rows stick to the top edge (the game does not letterbox them)
			ImVec2 top(float x, float y) const { return ImVec2(pos.x + offsetX + x * ui, pos.y + y * ui); }
			float s(float v) const { return v * ui; }
		};

		void image(const Layout& L, const Texture* t, ImVec2 a, ImVec2 b, float alpha = 1.0f, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1))
		{
			if (!t || t->getID() == 0 || alpha <= 0.002f || b.x - a.x < 0.1f || b.y - a.y < 0.1f)
				return;
			L.dl->AddImage(texId(*t), a, b, uv0, uv1, IM_COL32(255, 255, 255, static_cast<int>(clamp01(alpha) * 255)));
		}

		// Text in the header font (or the UI font) at `px` pixels, anchored by `align` (0..1).
		ImVec2 textSize(const char* text, float px)
		{
			ImFont* font = Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont();
			return font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
		}

		// Draws text and recolours its vertices with a horizontal gradient (stops spread evenly).
		void gradientText(ImDrawList* dl, ImVec2 p, float px, const char* text, const std::vector<ImVec4>& stops, float alpha, float shift = 0.0f)
		{
			ImFont* font = Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont();
			const ImVec2 size = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
			const int start = dl->VtxBuffer.Size;
			dl->AddText(font, px, p, IM_COL32_WHITE, text);
			if (stops.empty())
				return;
			for (int i = start; i < dl->VtxBuffer.Size; ++i)
			{
				ImDrawVert& v = dl->VtxBuffer[i];
				float t = size.x > 0 ? (v.pos.x - p.x) / size.x : 0.0f;
				t = std::fmod(t + shift + 4.0f, 1.0f);
				const float f = t * (stops.size() - 1);
				const int k = std::min(static_cast<int>(f), static_cast<int>(stops.size()) - 2);
				const ImVec4 c = stops.size() == 1 ? stops[0] : mixc(stops[k], stops[k + 1], f - k);
				const float a = ((v.col >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;
				v.col = col(c, a * alpha);
			}
		}

		void glowText(ImDrawList* dl, ImVec2 p, float px, const char* text, const ImVec4& glow, float alpha, float spread)
		{
			ImFont* font = Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont();
			for (int i = 0; i < 8; ++i)
			{
				const float a = i * PI * 0.25f;
				dl->AddText(font, px, p + ImVec2(std::cos(a), std::sin(a)) * spread, col(glow, alpha * 0.22f), text);
			}
		}

		void outlineText(ImDrawList* dl, ImVec2 p, float px, const char* text, ImU32 color, float width)
		{
			ImFont* font = Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont();
			for (int i = 0; i < 8; ++i)
			{
				const float a = i * PI * 0.25f;
				dl->AddText(font, px, p + ImVec2(std::cos(a), std::sin(a)) * width, color, text);
			}
		}

		// deterministic sparkle positions
		float hash01(int n)
		{
			unsigned int x = static_cast<unsigned int>(n) * 2654435761u;
			x ^= x >> 13;
			x *= 0x5bd1e995u;
			x ^= x >> 15;
			return (x & 0xffffff) / static_cast<float>(0xffffff);
		}

		void sparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 color)
		{
			dl->AddQuadFilled(c + ImVec2(0, -r), c + ImVec2(r * 0.22f, 0), c + ImVec2(0, r), c + ImVec2(-r * 0.22f, 0), color);
			dl->AddQuadFilled(c + ImVec2(-r, 0), c + ImVec2(0, -r * 0.22f), c + ImVec2(r, 0), c + ImVec2(0, r * 0.22f), color);
		}

		// the overlay's movie clips are unmultiplied (MoviePlayer::setUnmultiply): premultiplied blending
		void premultipliedOn(const ImDrawList*, const ImDrawCmd*) { glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }

		bool waitMovies = false;
	}

	// ---- skins ---------------------------------------------------------------------------------

	const std::map<std::string, SkinElement>& ScoreSkin::defaultElements()
	{
		// x, y move an element from where the game puts it (1080p pixels); scale resizes it
		static const std::map<std::string, SkinElement> elements{
			{ "score", { 0.0f, 0.0f, 1.0f, true } },
			{ "life", { 0.0f, 0.0f, 1.0f, true } },
			{ "combo", { 0.0f, 0.0f, 1.0f, true } },
			{ "judge", { 0.0f, 0.0f, 1.0f, true } },
			{ "skill", { 0.0f, 0.0f, 1.075f, true } },
			{ "fever", { 0.0f, 0.0f, 1.0f, true } },
			{ "all_perfect", { 0.0f, 0.0f, 1.0f, true } },
			{ "score_delta", { 0.0f, 0.0f, 1.0f, true } },
		};
		return elements;
	}

	const std::map<std::string, ImVec4>& ScoreSkin::defaultColors()
	{
		static const std::map<std::string, ImVec4> colors{
			{ "skill_panel_left", hex(0x7a6f9a) }, { "skill_panel_right", hex(0x55b6c9) }, { "skill_text", hex(0x7af7ff) },
			{ "fever_chance", hex(0xffc94a) }, { "fever", hex(0xb46bff) }, { "super_fever", hex(0xff7ad9) },
			{ "ap_1", hex(0xfff2a0) }, { "ap_2", hex(0xffb0e4) }, { "ap_3", hex(0xd2a4ff) }, { "ap_4", hex(0x8ff3ff) },
		};
		return colors;
	}

	const std::map<std::string, std::string>& ScoreSkin::defaultTexts()
	{
		static const std::map<std::string, std::string> texts{
			{ "all_perfect", "ALL PERFECT!!" }, { "fever_chance", "FEVER CHANCE!" }, { "fever", "FEVER!" },
			{ "super_fever", "SUPER FEVER!!" }, { "skill", "SKILL" }, { "skill_effect", "SCORE UP" }, { "skill_value", "120%" },
			{ "skill_level", "Lv.4" },
		};
		return texts;
	}

	SkinElement ScoreSkin::element(const std::string& key) const
	{
		auto it = elements.find(key);
		if (it != elements.end())
			return it->second;
		auto def = defaultElements().find(key);
		return def != defaultElements().end() ? def->second : SkinElement{};
	}

	ImVec4 ScoreSkin::color(const std::string& key) const
	{
		auto it = colors.find(key);
		if (it != colors.end())
			return it->second;
		auto def = defaultColors().find(key);
		return def != defaultColors().end() ? def->second : ImVec4(1, 1, 1, 1);
	}

	std::string ScoreSkin::text(const std::string& key) const
	{
		auto it = texts.find(key);
		if (it != texts.end())
			return it->second;
		auto def = defaultTexts().find(key);
		return def != defaultTexts().end() ? def->second : key;
	}

	namespace
	{
		// pjsekai-overlay-APPEND keeps the same art under other names
		std::string overlayPathOf(const std::string& root, const std::string& relative)
		{
			auto join = [&](const std::string& a) { return (fs::u8path(root) / fs::u8path(a)).u8string(); };
			if (relative.rfind("score/", 0) == 0) return join("assets/" + relative);
			if (relative.rfind("combo/", 0) == 0) return join("assets/" + relative);
			if (relative.rfind("life/", 0) == 0) return join("assets/life/v3/" + relative.substr(5));
			if (relative == "judge/perfect.png") return join("assets/judge/v3/1.png");
			if (relative.rfind("skill/", 0) == 0) return join("assets/skill/v3/" + relative.substr(6));
			if (relative.rfind("extra/", 0) == 0) return join("extra-assets/" + relative.substr(6));
			if (relative.rfind("assets/", 0) == 0) return join(relative);
			return {};
		}
	}

	std::string ScoreSkin::file(const std::string& relative) const
	{
		auto cached = fileCache.find(relative);
		if (cached != fileCache.end())
			return cached->second;

		std::string found;
		if (!folder.empty())
		{
			const std::string mine = (fs::u8path(folder) / fs::u8path(relative)).u8string();
			if (IO::File::exists(mine))
				found = mine;
		}
		if (found.empty())
		{
			const std::string pack = Application::getAppDir() + "res/sekai/" + relative;
			if (IO::File::exists(pack))
				found = pack;
		}
		if (found.empty() && !overlayDir.empty())
		{
			const std::string theirs = overlayPathOf(overlayDir, relative);
			if (!theirs.empty() && IO::File::exists(theirs))
				found = theirs;
		}
		if (found.empty())
		{
			const std::string base = Application::getAppDir() + "res/editor/";
			const std::string builtIn = relative.rfind("combo/", 0) == 0 ? base + relative : base + "hud/" + relative;
			if (IO::File::exists(builtIn))
				found = builtIn;
		}
		fileCache[relative] = found;
		return found;
	}

	const Texture* ScoreSkin::texture(const std::string& relative) const
	{
		auto it = cache.find(relative);
		if (it == cache.end())
		{
			const std::string path = file(relative);
			std::shared_ptr<Texture> texture;
			if (!path.empty())
				texture = std::make_shared<Texture>(path, Texture::Smooth);
			it = cache.emplace(relative, std::move(texture)).first;
		}
		return it->second && it->second->getID() ? it->second.get() : nullptr;
	}

	void ScoreSkin::clearCache() const
	{
		for (auto& [key, texture] : cache)
			if (texture)
				texture->dispose();
		cache.clear();
		fileCache.clear();
	}

	bool ScoreSkin::load(const std::string& dir)
	{
		folder = dir;
		elements.clear();
		colors.clear();
		texts.clear();
		name = fs::u8path(dir).filename().u8string();
		std::ifstream in(fs::u8path(dir) / "skin.json");
		if (!in)
			return true;	// a folder of images alone is a skin too
		const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
		if (!json.is_object())
			return false;
		if (json.contains("name") && json["name"].is_string())
			name = json["name"].get<std::string>();
		// 1.2.1 skins stored absolute positions for different art: keep only their switches
		const bool offsets = json.contains("version") && json["version"].is_number() && json["version"].get<int>() >= 2;
		if (json.contains("elements") && json["elements"].is_object())
		{
			for (const auto& [key, value] : json["elements"].items())
			{
				SkinElement e = element(key);
				if (offsets && value.contains("x") && value["x"].is_number()) e.x = value["x"].get<float>();
				if (offsets && value.contains("y") && value["y"].is_number()) e.y = value["y"].get<float>();
				if (offsets && value.contains("scale") && value["scale"].is_number()) e.scale = value["scale"].get<float>();
				if (value.contains("visible") && value["visible"].is_boolean()) e.visible = value["visible"].get<bool>();
				elements[key] = e;
			}
		}
		if (json.contains("colors") && json["colors"].is_object())
		{
			for (const auto& [key, value] : json["colors"].items())
			{
				if (value.is_string())
				{
					std::string s = value.get<std::string>();
					if (!s.empty() && s[0] == '#')
						s.erase(0, 1);
					if (s.size() == 6 || s.size() == 8)
					{
						const unsigned long v = std::stoul(s, nullptr, 16);
						colors[key] = s.size() == 6 ? hex(static_cast<unsigned int>(v)) : hex(static_cast<unsigned int>(v >> 8), (v & 0xff) / 255.0f);
					}
				}
			}
		}
		if (json.contains("texts") && json["texts"].is_object())
			for (const auto& [key, value] : json["texts"].items())
				if (value.is_string())
					texts[key] = value.get<std::string>();
		return true;
	}

	bool ScoreSkin::save() const
	{
		if (folder.empty())
			return false;
		nlohmann::ordered_json json;
		json["format"] = "canvas-editor/score-skin";
		json["version"] = 2;
		json["name"] = name;
		for (const auto& [key, def] : defaultElements())
		{
			const SkinElement e = element(key);
			json["elements"][key] = { { "x", e.x }, { "y", e.y }, { "scale", e.scale }, { "visible", e.visible } };
		}
		for (const auto& [key, def] : defaultColors())
		{
			const ImVec4 c = color(key);
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", static_cast<int>(c.x * 255 + 0.5f), static_cast<int>(c.y * 255 + 0.5f), static_cast<int>(c.z * 255 + 0.5f));
			json["colors"][key] = buffer;
		}
		for (const auto& [key, def] : defaultTexts())
			json["texts"][key] = text(key);
		std::error_code error;
		fs::create_directories(fs::u8path(folder), error);
		std::ofstream out(fs::u8path(folder) / "skin.json");
		out << json.dump(2);
		return static_cast<bool>(out);
	}

	std::string skinsFolder()
	{
		return Application::getUserDataDir() + "score_skins";
	}

	std::vector<std::string> listSkins()
	{
		std::vector<std::string> names;
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(fs::u8path(skinsFolder()), error))
			if (entry.is_directory())
				names.push_back(entry.path().filename().u8string());
		std::sort(names.begin(), names.end());
		return names;
	}

	namespace
	{
		ScoreSkin skin;
		std::string skinKey;
	}

	ScoreSkin& activeSkin()
	{
		const std::string key = config.pvScoreSkin + "|" + config.overlayAssetsDir;
		if (key != skinKey)
		{
			skin.clearCache();
			skin.name.clear();
			skin.folder.clear();
			skin.overlayDir.clear();
			skin.elements.clear();
			skin.colors.clear();
			skin.texts.clear();
			if (!config.pvScoreSkin.empty())
			{
				const std::string dir = (fs::u8path(skinsFolder()) / fs::u8path(config.pvScoreSkin)).u8string();
				if (fs::is_directory(fs::u8path(dir)))
					skin.load(dir);
			}
			if (skin.name.empty())
				skin.name = "Built-in";
			if (!config.overlayAssetsDir.empty() && fs::is_directory(fs::u8path(config.overlayAssetsDir) / "assets"))
				skin.overlayDir = config.overlayAssetsDir;
			else if (fs::is_directory(fs::u8path(Application::getAppDir() + "res/sekai/overlay/assets")))
				skin.overlayDir = Application::getAppDir() + "res/sekai/overlay";
			skinKey = key;
		}
		return skin;
	}

	void reloadSkins()
	{
		skin.clearCache();
		skinKey.clear();
	}

	void clearClips();	// below, next to the cache it empties

	void releaseGlResources()
	{
		clearClips();
		clearCardTextures();
		reloadSkins();
	}

	bool createSkinFromCurrent(const std::string& rawName, std::string& error)
	{
		std::string name;
		for (char c : rawName)
			if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || static_cast<unsigned char>(c) >= 0x80)
				name.push_back(c);
		if (name.empty())
		{
			error = "Give the skin a name.";
			return false;
		}
		const fs::path dir = fs::u8path(skinsFolder()) / fs::u8path(name);
		std::error_code ec;
		if (fs::exists(dir, ec))
		{
			error = "A skin with that name exists.";
			return false;
		}
		fs::create_directories(dir, ec);

		// every image the overlay draws, from wherever the current skin gets it
		ScoreSkin& current = activeSkin();
		std::vector<std::string> files = { "hud/bg_scoreGauge.png", "hud/gauge_base_h16_dbl.png", "hud/gauge_bar_ingame_freeLive_s.png",
			"hud/meter_mark.png", "hud/plus.png", "hud/bg_lifeGauge.png", "hud/bar_lifeGauge_gn.png", "hud/icon_heart_gn.png",
			"hud/effect_scoreGauge.png", "hud/effect_lifeGauge.png", "hud/txt_combo_pr.png", "hud/txt_combo_ap_rbw.png", "hud/eff_combo.png",
			"judge/judge_perfect.png", "live/btn_stop.png" };
		for (char r : std::string("SABCD"))
		{
			files.push_back(std::string("hud/score_") + r + ".png");
			files.push_back(std::string("hud/score_") + r + "_glow.png");
			files.push_back(std::string("hud/rank_text_") + static_cast<char>(std::tolower(r)) + ".png");
			if (r != 'D')
				files.push_back(std::string("hud/") + r + ".png");
		}
		for (char d : std::string("0123456789"))
		{
			files.push_back(std::string("hud/number_48px_") + d + "_wh.png");
			files.push_back(std::string("hud/number_48px_shadow_") + d + ".png");
			files.push_back(std::string("hud/number_combo_154px_") + d + "_pr.png");
			files.push_back(std::string("hud/number_combo_ap_154px_") + d + "_rbw.png");
			files.push_back(std::string("hud/eff_") + d + "_150.png");
		}
		for (const char* f : { "skill/Score-UP.png", "skill/icon/ScoreUP.png", "skill/bar/ScoreBarUP.png", "skill/digit/Lv.png" })
			files.push_back(f);
		int copied = 0;
		for (const std::string& relative : files)
		{
			const std::string from = current.file(relative);
			if (from.empty())
				continue;
			const fs::path to = dir / fs::u8path(relative);
			fs::create_directories(to.parent_path(), ec);
			fs::copy_file(fs::u8path(from), to, fs::copy_options::overwrite_existing, ec);
			copied += !ec;
		}
		ScoreSkin made;
		made.elements = current.elements;
		made.colors = current.colors;
		made.texts = current.texts;
		made.folder = dir.u8string();
		made.name = name;
		if (!made.save())
		{
			error = "Could not write skin.json in " + dir.u8string();
			return false;
		}
		config.pvScoreSkin = name;
		reloadSkins();
		return copied > 0;
	}

	// ---- effect timing ---------------------------------------------------------------------------

	EffectTimes effectTimes(const Score& score)
	{
		EffectTimes times;
		if (score.tempoChanges.empty())
			return times;
		auto timeOf = [&](int tick) { return static_cast<double>(accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges)); };
		for (const SkillTrigger& skill : score.skills)
			times.skills.push_back(timeOf(skill.tick));
		std::sort(times.skills.begin(), times.skills.end());
		int lastTick = -1;
		for (const auto& [id, note] : score.notes)
			lastTick = std::max(lastTick, note.tick);
		if (lastTick >= 0)
		{
			times.lastNote = timeOf(lastTick);
			times.allPerfect = times.lastNote + ALL_PERFECT_DELAY;
		}
		if (score.fever.chanceTick >= 0)
			times.feverChance = timeOf(score.fever.chanceTick);
		if (score.fever.startTick >= 0)
		{
			times.feverStart = timeOf(score.fever.startTick);
			// FEVER lasts until the next skill (where the game's fever section ends), at most 15 s
			times.feverEnd = times.feverStart + 15.0;
			for (double s : times.skills)
				if (s > times.feverStart + 1.0)
				{
					times.feverEnd = std::min(times.feverEnd, s);
					break;
				}
			if (times.lastNote > 0)
				times.feverEnd = std::min(times.feverEnd, times.lastNote);
		}
		return times;
	}

	void setMovieFrameWaiting(bool wait) { waitMovies = wait; }

	// ---- drawing ---------------------------------------------------------------------------------

	namespace
	{
		struct Clip
		{
			Canvas::Video::MoviePlayer player;
			std::string file;
		};

		std::map<std::string, std::unique_ptr<Clip>>& clipCache()
		{
			static std::map<std::string, std::unique_ptr<Clip>> clips;
			return clips;
		}

		Clip& clip(const char* name)
		{
			auto& c = clipCache()[name];
			if (!c)
				c = std::make_unique<Clip>();
			return *c;
		}

		// draws a pjsekai-overlay-APPEND movie clip full-screen like AviUtl's "unmult" (black turns
		// see-through, colours stay as they are); false when it cannot
		// `underlay` fills the frame under the clip while it plays (ALL PERFECT's 50% black)
		// opens a clip and starts decoding it a moment before it is due, so it plays smoothly from
		// its first frame instead of catching up
		void primeClip(const ScoreSkin& s, const char* key, const std::string& relative, double untilStart)
		{
			if (s.overlayDir.empty() || untilStart <= 0.0 || untilStart > 1.5 || waitMovies)
				return;
			const std::string file = s.file(relative);
			if (file.empty())
				return;
			Clip& c = clip(key);
			c.player.setUnmultiply(true);
			c.player.setFrameRate(60.0);
			c.player.setMaxWidth(1920);
			if (c.player.open(file, config.ffmpegPath))
				c.player.prime(0.0);
		}

		// `clipTop` hides what is above that screen y (the FEVER gauge's fill)
		// `tint` colours the clip (0 = as it is); `fill` stretches it over the whole picture instead of
		// the 16:9 stage (the FEVER frame belongs on the edges)
		bool drawClip(const Layout& L, const ScoreSkin& s, const char* key, const std::string& relative, double t, bool loop = false, float alpha = 1.0f, ImU32 underlay = 0, float clipTop = -FLT_MAX, ImU32 tint = 0, bool fill = false)
		{
			if (s.overlayDir.empty() || t < 0)
				return false;
			const std::string file = s.file(relative);
			if (file.empty())
				return false;
			Clip& c = clip(key);
			c.player.setUnmultiply(true);
			c.player.setFrameRate(60.0);	// the effect clips are 60 fps
			c.player.setMaxWidth(1920);
			c.player.setOutputWidth(static_cast<int>(fill ? L.size.x : 1920.0f * L.ui));
			if (!c.player.open(file, config.ffmpegPath))
				return false;
			if (loop && c.player.getDuration() > 0.1)
				t = std::fmod(t, c.player.getDuration() - 0.05);
			else if (t > c.player.getDuration() + 0.05)
				return true;	// finished: nothing more to draw, but no built-in fallback either
			if (!c.player.update(t, waitMovies))
				return true;
			const ImVec2 a = fill ? L.pos : L.at(0, 0), b = fill ? L.pos + L.size : L.at(1920, 1080);
			const int k = static_cast<int>(clamp01(alpha) * 255.0f + 0.5f);
			// premultiplied: the tint scales each channel, alpha stays
			const ImU32 color = tint ? IM_COL32(((tint >> IM_COL32_R_SHIFT) & 0xff) * k / 255, ((tint >> IM_COL32_G_SHIFT) & 0xff) * k / 255,
				((tint >> IM_COL32_B_SHIFT) & 0xff) * k / 255, k) : IM_COL32(k, k, k, k);
			if (underlay)
				L.dl->AddRectFilled(L.pos, L.pos + L.size, underlay);
			if (clipTop > -FLT_MAX)
				L.dl->PushClipRect(ImVec2(L.pos.x, std::max(L.pos.y, clipTop)), L.pos + L.size, true);
			L.dl->AddCallback(premultipliedOn, nullptr);
			L.dl->AddImage(static_cast<ImTextureID>(c.player.getTexture()), a, b, ImVec2(0, 0), ImVec2(1, 1), color);
			L.dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			if (clipTop > -FLT_MAX)
				L.dl->PopClipRect();
			return true;
		}

		// ---- the game's HUD: OpenSekai's sprites where FrontUIView.prefab puts them ------------------
		// Score and life hang off the top corners (the game's SpriteAnchor: 98% of the way from the
		// centre to each edge); combo and judgement sit in the 1920x1080 frame. Coordinates are 1080p
		// pixels; a sprite draws at 108 / ppu pixels per texture pixel, like the game's HUD camera
		// (orthographic size 5: 10 units = 1080 pixels).

		struct SpriteMeta
		{
			float pivotX{ 0.5f }, pivotY{ 0.5f }, ppu{ 108.0f };
			float border[4]{};	// left, bottom, right, top, in texture pixels
		};

		const SpriteMeta& spriteMeta(const std::string& key)
		{
			static std::map<std::string, SpriteMeta> metas;
			static bool loaded = false;
			if (!loaded)
			{
				loaded = true;
				std::ifstream in(fs::u8path(Application::getAppDir() + "res/sekai/sprites.json"));
				const nlohmann::json json = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
				if (json.is_object())
				{
					for (const auto& [name, v] : json.items())
					{
						SpriteMeta m;
						if (v.contains("pivot") && v["pivot"].is_array() && v["pivot"].size() == 2)
						{
							m.pivotX = v["pivot"][0].get<float>();
							m.pivotY = v["pivot"][1].get<float>();
						}
						if (v.contains("ppu") && v["ppu"].is_number() && v["ppu"].get<float>() > 0)
							m.ppu = v["ppu"].get<float>();
						if (v.contains("border") && v["border"].is_array() && v["border"].size() == 4)
							for (int i = 0; i < 4; ++i)
								m.border[i] = v["border"][i].get<float>();
						metas[name] = m;
					}
				}
			}
			static const SpriteMeta none{};
			auto it = metas.find(key);
			return it != metas.end() ? it->second : none;
		}

		ImU32 white(float alpha) { return IM_COL32(255, 255, 255, static_cast<int>(clamp01(alpha) * 255.0f + 0.5f)); }

		// one HUD group: positions relative to its anchor, moved and scaled by the skin's element
		struct Hud
		{
			const Layout& L;
			const ScoreSkin& s;
			ImVec2 anchor;
			SkinElement e;

			ImVec2 at(float x, float y) const { return anchor + ImVec2(e.x + x * e.scale, e.y + y * e.scale) * L.ui; }
			float px(float v) const { return v * e.scale * L.ui; }

			// "hud/score_S" with its pivot at (x, y); `scale` is the transform's
			void sprite(const std::string& name, float x, float y, float scale = 1.0f, ImU32 tint = IM_COL32_WHITE) const
			{
				const Texture* t = s.texture(name + ".png");
				if (!t || ((tint >> IM_COL32_A_SHIFT) & 0xff) == 0 || scale <= 0.0001f)
					return;
				const SpriteMeta& m = spriteMeta(name);
				const float k = px(108.0f / m.ppu * scale);
				const ImVec2 size(t->getWidth() * k, t->getHeight() * k);
				const ImVec2 p = at(x, y);
				const ImVec2 tl(p.x - m.pivotX * size.x, p.y - (1.0f - m.pivotY) * size.y);
				L.dl->AddImage(texId(*t), tl, tl + size, ImVec2(0, 0), ImVec2(1, 1), tint);
			}

			// a sliced sprite stretched to w x h from its top-left; the left and right borders keep
			// their size and the middle stretches (the gauges and their frames)
			void slice(const std::string& name, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE) const
			{
				const Texture* t = s.texture(name + ".png");
				if (!t || w <= 0.5f || h <= 0.5f)
					return;
				const SpriteMeta& m = spriteMeta(name);
				const float tw = static_cast<float>(t->getWidth());
				float left = m.border[0] * 108.0f / m.ppu, right = m.border[2] * 108.0f / m.ppu;
				if (left + right > w)
				{
					const float f = w / (left + right);
					left *= f;
					right *= f;
				}
				const ImVec2 a = at(x, y), b = at(x + w, y + h);
				const float l = px(left), r = px(right);
				const float u0 = m.border[0] / tw, u1 = 1.0f - m.border[2] / tw;
				ImTextureID id = texId(*t);
				L.dl->AddImage(id, a, ImVec2(a.x + l, b.y), ImVec2(0, 0), ImVec2(u0, 1), tint);
				L.dl->AddImage(id, ImVec2(a.x + l, a.y), ImVec2(b.x - r, b.y), ImVec2(u0, 0), ImVec2(u1, 1), tint);
				L.dl->AddImage(id, ImVec2(b.x - r, a.y), b, ImVec2(u1, 0), ImVec2(1, 1), tint);
			}

			// right-aligned digits like the game's NumberView: digit 0 (the last one) at x, the rest to
			// its left every `spacing`; `slots` shows leading zeros in `zeroTint`
			void number(const std::string& text, int slots, float x, float y, float spacing, float scale, ImU32 tint, ImU32 zeroTint, bool shadow) const
			{
				const int count = std::max(slots, static_cast<int>(text.size()));
				for (int pass = shadow ? 0 : 1; pass < 2; ++pass)
				{
					for (int i = 0; i < count; ++i)
					{
						const int index = static_cast<int>(text.size()) - 1 - i;
						const bool lead = index < 0;
						if (lead && ((zeroTint >> IM_COL32_A_SHIFT) & 0xff) == 0)
							continue;
						const char ch = lead ? '0' : text[index];
						if (ch < '0' || ch > '9')
							continue;
						const float dx = x - i * spacing;
						if (pass == 0)
							sprite(std::string("hud/number_48px_shadow_") + ch, dx, y, scale, white(((lead ? zeroTint : tint) >> IM_COL32_A_SHIFT & 0xff) / 255.0f));
						else
							sprite(std::string("hud/number_48px_") + ch + "_wh", dx, y, scale, lead ? zeroTint : tint);
					}
				}
			}
		};

		// the C / B / A / S marks sit at fixed fractions of the gauge (ScoreGaugeCalculator)
		constexpr float RANK_RATES[4] = { 0.45f, 0.6f, 0.75f, 0.9f };
		constexpr float GAUGE_X = 143.9f, GAUGE_Y = 44.3f, GAUGE_W = 531.7f, GAUGE_H = 23.8f;

		// the "+800": slides 0.5 units in from the left while fading in (0.2 s, OutQuad), stays 0.5 s
		constexpr float SCORE_DELTA_DURATION = 0.7f;
		constexpr float SCORE_DELTA_EASE = 0.2f;

		// `delta` is what the latest note added, `deltaAge` how long ago it landed (< 0: no popup);
		// `skillGlow` > 0 while a score-up skill runs
		void drawScore(const Layout& L, const ScoreSkin& s, int score, char rank, float bar, float skillGlow, int delta = 0, float deltaAge = -1.0f)
		{
			const Hud h{ L, s, L.lt, s.element("score") };
			if (!h.e.visible)
				return;
			ImDrawList* dl = L.dl;

			if (skillGlow > 0.001f)
				h.sprite("hud/effect_scoreGauge", 406.9f, 86.7f, 1.0f, white(skillGlow));
			h.slice("hud/bg_scoreGauge", 125.6f, 4.9f, 566.8f, 81.0f);
			h.slice("hud/gauge_base_h16_dbl", 143.5f, 44.1f, 532.1f, 24.1f);
			const float fill = clamp01(bar);
			if (const Texture* t = s.texture("hud/gauge_bar_ingame_freeLive_s.png"); t && fill > 0.001f)
				dl->AddImage(texId(*t), h.at(GAUGE_X, GAUGE_Y), h.at(GAUGE_X + GAUGE_W * fill, GAUGE_Y + GAUGE_H), ImVec2(0, 0), ImVec2(fill, 1));
			static const char* marks[4] = { "hud/C", "hud/B", "hud/A", "hud/S" };
			for (int i = 0; i < 4; ++i)
			{
				const float x = GAUGE_X + RANK_RATES[i] * GAUGE_W;
				dl->AddRectFilled(h.at(x - 1.7f, 38.9f), h.at(x + 1.7f, 68.2f), IM_COL32_WHITE);
				h.sprite(marks[i], x, 11.3f);
				h.sprite("hud/meter_mark", x, 35.6f);
			}

			// the rank: a dark box down from the top edge, the glowing letter, SCORERANK in its colour
			dl->AddRectFilled(h.at(23.3f, -10.8f), h.at(127.6f, 131.9f), IM_COL32(0x44, 0x44, 0x66, 255));
			const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(rank)));
			const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(rank)));
			const std::string letter = std::string("hud/score_") + upper;
			if (const Texture* t = s.texture(letter + ".png"))
			{
				const SpriteMeta& m = spriteMeta(letter);
				const float k = h.px(108.0f / m.ppu);
				const ImVec2 size(t->getWidth() * k, t->getHeight() * k);
				const ImVec2 p = h.at(74.8f, 51.0f);
				const ImVec2 tl(p.x - m.pivotX * size.x, p.y - (1.0f - m.pivotY) * size.y);
				// just the letter: the game draws no glow round it (score_X_glow.png stays in skins for
				// whoever wants it back, but nothing draws it)
				dl->AddImage(texId(*t), tl, tl + size);
			}
			if (const Texture* t = s.texture(std::string("hud/rank_text_") + lower + ".png"))
			{
				const float w = h.px(90.0f), hh = w * t->getHeight() / std::max(1, t->getWidth());
				const ImVec2 c = h.at(75.1f, 113.2f);
				dl->AddImage(texId(*t), c - ImVec2(w, hh) * 0.5f, c + ImVec2(w, hh) * 0.5f);
			}
			else
				h.sprite("hud/txt_scoreRank_ingame_wh", 75.1f, 113.2f);

			// 8 digits, leading zeros pale (NumberView "#ffffff" / "#CCCCEE")
			h.number(std::to_string(std::max(0, score)), 8, 398.41f, 101.2f, 32.94f, 0.94f, IM_COL32_WHITE, IM_COL32(0xcc, 0xcc, 0xee, 255), true);

			const SkinElement popup = s.element("score_delta");
			if (popup.visible && delta > 0 && deltaAge >= 0.0f && deltaAge < SCORE_DELTA_DURATION)
			{
				const float t = clamp01(deltaAge / SCORE_DELTA_EASE);
				const float eased = 1.0f - (1.0f - t) * (1.0f - t);
				const float k = popup.scale;
				const float rootX = 390.44f + 54.0f * eased + popup.x, rootY = 110.07f + popup.y;
				const std::string digits = std::to_string(delta);
				// the "+" is Canvas's art (white with its outline); the digits are the score's, left-aligned
				if (s.texture("hud/plus.png"))
					h.sprite("hud/plus", rootX, rootY, 0.6f * k, white(eased));
				else
					h.sprite("hud/text_plus_36wh", rootX, rootY, 0.564f * k, white(eased));
				for (int pass = 0; pass < 2; ++pass)
					for (size_t i = 0; i < digits.size(); ++i)
					{
						const float x = rootX + static_cast<float>(i + 1) * 20.3f * k;
						if (pass == 0)
							h.sprite(std::string("hud/number_48px_shadow_") + digits[i], x, rootY, 0.564f * k, white(eased));
						else
							h.sprite(std::string("hud/number_48px_") + digits[i] + "_wh", x, rootY, 0.564f * k, white(eased));
					}
			}
		}

		void drawLife(const Layout& L, const ScoreSkin& s, float life, float skillGlow)
		{
			const Hud h{ L, s, L.rt, s.element("life") };
			if (!h.e.visible)
				return;
			if (skillGlow > 0.001f)
				h.sprite("hud/effect_lifeGauge", -289.9f, 53.8f, 1.0f, white(skillGlow));
			h.slice("hud/bg_lifeGauge", -443.6f, 6.3f, 379.1f, 81.0f);
			h.slice("hud/gauge_base_h16_dbl", -392.0f, 48.4f, 264.6f, 15.6f);
			const float ratio = clamp01(life);
			if (ratio > 0.0f)
				h.slice("hud/bar_lifeGauge_gn", -392.1f, 48.4f, std::max(22.0f, 264.6f * ratio), 15.6f);
			h.sprite("hud/icon_heart_gn", -410.3f, 57.2f);
			h.sprite("live/btn_stop", -65.9f, 56.4f);
			// the last digit a little left of the game's spot, clear of the pause button
			h.number(std::to_string(static_cast<int>(std::lround(1000.0f * ratio))), 0, -141.81f, 26.31f, 22.11f, 0.63f, IM_COL32_WHITE, 0, true);
		}

		// ComboView: digits 0.95 units apart around the root; a new combo pops from 0.6 to 1 in 0.1 s
		// and leaves a fading copy that grows to 1.4; all-perfect digits are rainbow with a pulsing glow
		void drawCombo(const Layout& L, const ScoreSkin& s, int combo, float elapsed, float time, bool allPerfect)
		{
			const SkinElement e = s.element("combo");
			if (!e.visible || combo <= 0)
				return;
			const Hud h{ L, s, L.atRight(1631.44f, 474.44f), e };
			const std::string text = std::to_string(combo);
			const int n = static_cast<int>(text.size());
			const float pulse = (std::sin(time * 4.1887903f) + 1.0f) * 0.5f;
			const float rootScale = 0.6f + 0.4f * clamp01(elapsed / 0.1f);
			const float burst = clamp01((elapsed - 0.1f) / 0.1f);
			const float effectScale = elapsed >= 0.1f ? 1.0f + burst * 0.4f : 0.0f;
			const float effectAlpha = elapsed >= 0.1f ? (1.0f - burst) * 0.5f : 0.0f;

			if (allPerfect)
			{
				h.sprite("hud/eff_combo", 2.59f, -97.84f, 1.0f, white(pulse));
				h.sprite("hud/txt_combo_ap_rbw", 2.59f, -97.84f);
			}
			else
				h.sprite("hud/txt_combo_pr", 2.59f, -97.84f);

			auto digitX = [&](int i, float scale) { return ((n - 1) * 51.3f - i * 102.6f) * scale; };
			const std::string digitPrefix = allPerfect ? "hud/number_combo_ap_154px_" : "hud/number_combo_154px_";
			const char* digitSuffix = allPerfect ? "_rbw" : "_pr";
			for (int i = 0; i < n; ++i)
			{
				const char ch = text[n - 1 - i];
				if (allPerfect)
					h.sprite(std::string("hud/eff_") + ch + "_150", digitX(i, rootScale), 0.0f, rootScale, white(pulse));
				if (effectAlpha > 0.001f)
					h.sprite(digitPrefix + ch + digitSuffix, digitX(i, effectScale), 0.0f, effectScale, white(effectAlpha));
				h.sprite(digitPrefix + ch + digitSuffix, digitX(i, rootScale), 0.0f, rootScale);
			}
		}

		// JudgmentView: grows to full size in 0.05 s (1 - (1 - 20t)^3), gone after 0.3 s
		void drawJudge(const Layout& L, const ScoreSkin& s, float elapsed)
		{
			const SkinElement e = s.element("judge");
			if (!e.visible || elapsed < 0.0f || elapsed > 0.3f)
				return;
			const float t = clamp01(elapsed * 20.0f);
			const float scale = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
			const Hud h{ L, s, L.at(960.0f, 666.36f), e };
			h.sprite("judge/judge_perfect", 0.0f, 0.0f, scale);
		}

		// ---- skills ------------------------------------------------------------------------------

		// The skill cut-in, as pjsekai-overlay-APPEND animates it (frames at 60 fps): the panel slides
		// 64 px in over 12 frames, "Lv.4" swaps to "120%" at 1.9 - 2.1 s, out over 3.8 - 4.0 s.
		constexpr float CUTIN_DURATION = 4.0f;

		void drawSkill(const Layout& L, const ScoreSkin& s, float t, int index)
		{
			SkinElement e = s.element("skill");
			e.x += 140.5f;	// skin positions are offsets from the game's spot
			e.y += 358.5f;
			if (!e.visible || t < 0.0f || t > CUTIN_DURATION)
				return;
			const float frames = t * 60.0f;
			float fax = 1.0f - std::pow(1.0f - std::min(1.0f, frames / 12.0f), 2.0f);
			if (frames / 12.0f >= 19.0f)
				fax = std::pow(1.0f - std::min(1.0f, frames / 12.0f - 19.0f), 2.0f);
			if (fax <= 0.001f)
				return;
			const float u1 = std::clamp(2.0f * (frames / 12.0f) - 19.0f, 0.0f, 1.0f);
			const float shiftText1 = 1.0f - std::pow(1.0f - u1, 2.0f), alpha1 = std::pow(1.0f - u1, 2.0f);
			const float u2 = std::clamp(2.0f * (frames / 12.0f) - 20.0f, 0.0f, 1.0f);
			const float shiftText2 = -std::pow(1.0f - u2, 2.0f), alpha2 = 1.0f - std::pow(1.0f - u2, 2.0f);

			// centre-relative placement in the layout, like AviUtl's obj.draw(x, y, z, zoom, alpha)
			auto put = [&](const Texture* tex, float x, float y, float zoom, float alpha) {
				if (!tex)
					return false;
				const float w = tex->getWidth() * zoom * e.scale, h = tex->getHeight() * zoom * e.scale;
				const ImVec2 c = L.atLeft(e.x + x * e.scale, e.y + y * e.scale);
				image(L, tex, c - ImVec2(L.s(w), L.s(h)) * 0.5f, c + ImVec2(L.s(w), L.s(h)) * 0.5f, alpha);
				return true;
			};
			const float slide = 64.0f * fax;
			ImDrawList* dl = L.dl;

			// the card whose skill this is (skill n goes to card n % 5): its kind picks the panel
			const LiveCard& card = deckCard(index);
			const char* panel = card.skill == SkillKind::Life ? "skill/Life-UP.png" : card.skill == SkillKind::Judgment ? "skill/Skill-UP.png" : "skill/Score-UP.png";
			const Texture* panelArt = s.texture(panel);
			if (!panelArt)
				panelArt = s.texture("skill/Score-UP.png");
			if (put(panelArt, slide, 0, 1.0f, fax))
			{
				if (const Texture* thumb = cardTexture(index, CardArt::Thumbnail))
					put(thumb, -97.0f + slide, 14.5f, 73.0f / std::max(1, std::max(thumb->getWidth(), thumb->getHeight())), fax);
				put(s.texture("skill/digit/Lv.png"), 120.0f + slide - 10.0f * shiftText1, 31.5f, 1.0f, fax * alpha1);
				const std::string level = s.text("skill_level");
				const std::string levelDigit = level.empty() ? "4" : std::string(1, level.back());
				put(s.texture("skill/digit/" + levelDigit + ".png"), 167.0f + slide - 10.0f * shiftText1, 31.0f, 0.82f, fax * alpha1);

				// a skin's own text wins over the card's
				const std::string value = s.texts.count("skill_value") || card.value.empty() ? s.text("skill_value") : card.value;
				float startX = 0, after1 = 0, after2 = 0, period = 0;
				int specials = 0;
				const int n = static_cast<int>(value.size());
				for (int i = n; i >= 1; --i)
				{
					const char digit = value[i - 1];
					const char next = i < n ? value[i] : '\0';
					float sizeK = 1.0f;
					if (digit == '%' || digit == 's')
					{
						startX -= 4.0f;
						sizeK = 1.0f / 0.78f;
					}
					if (next == '%' || next == 's')
					{
						after1 -= 29.0f;
						++specials;
					}
					if (digit == '.')
						period += 8.5f;
					if (next == '.')
					{
						after2 -= 17.5f;
						++specials;
					}
					const float x = 167.0f - 27.5f * (n - i - specials) + period + startX + after1 + after2 + slide - 10.0f * shiftText2;
					put(s.texture(std::string("skill/digit/") + digit + ".png"), x, 33.0f, 0.78f * sizeK, fax * alpha2);
				}
				return;
			}

			// built-in panel (no asset pack): the same layout, drawn
			const float W = 408.0f * e.scale, H = 136.0f * e.scale;
			const float x0 = e.x + (slide - 204.0f) * e.scale, y0 = e.y - H * 0.5f;
			auto P = [&](float x, float y) { return L.atLeft(x0 + x * e.scale, y0 + y * e.scale); };
			const ImVec4 left = s.color("skill_panel_left"), right = s.color("skill_panel_right");
			dl->AddRectFilled(P(60, 0), P(164, 34), col(left, fax * 0.95f), L.s(10 * e.scale), ImDrawFlags_RoundCornersTop);
			const ImVec2 a = P(20, 30), b = P(380, 126);
			dl->AddRectFilledMultiColor(a, b, col(left, fax * 0.92f), col(right, fax * 0.92f), col(right, fax * 0.92f), col(left, fax * 0.92f));
			for (int r = 0; r < 6; ++r)
				for (int c = 0; c < 3; ++c)
					dl->AddCircleFilled(P(4.0f + c * 7.0f + (r % 2) * 3.5f, 36.0f + r * 15.0f), L.s(2.2f * e.scale), col(left, fax * (0.9f - c * 0.25f)));
			const char* tab = "SKILL";
			const float tabPx = L.s(28 * e.scale);
			const ImVec2 tabSize = textSize(tab, tabPx);
			dl->AddText(Canvas::SekaiStyle::headerFont, tabPx, P(112, 17) - tabSize * 0.5f, col(ImVec4(1, 1, 1, 1), fax), tab);
			dl->AddRectFilled(P(60, 42), P(140, 116), col(hex(0x332a55), fax), L.s(8 * e.scale));
			sparkle(dl, P(100, 79), L.s(26 * e.scale), col(s.color("skill_text"), fax * 0.8f));
			const ImVec4 arrow = s.color("skill_text");
			dl->AddTriangleFilled(P(185, 48), P(150, 82), P(220, 82), col(arrow, fax));
			dl->AddRectFilled(P(172, 82), P(198, 108), col(arrow, fax));
			const std::string effect = s.text("skill_effect");
			const float effectPx = L.s(30 * e.scale);
			gradientText(dl, P(236, 50) - ImVec2(0, effectPx * 0.5f), effectPx, effect.c_str(), { hex(0xbffcff), s.color("skill_text") }, fax);
			const float valuePx = L.s(40 * e.scale);
			const std::string level = s.text("skill_level"), value = s.text("skill_value");
			if (alpha1 > 0.0f)
				gradientText(dl, P(240 - 10 * shiftText1, 98) - ImVec2(0, valuePx * 0.5f), valuePx, level.c_str(), { hex(0xffffff) }, fax * alpha1);
			if (alpha2 > 0.0f)
				gradientText(dl, P(240 - 10 * shiftText2, 98) - ImVec2(0, valuePx * 0.5f), valuePx, value.c_str(), { hex(0xffffff), hex(0xd8fbff) }, fax * alpha2);
		}

		// ---- the card cut-in -----------------------------------------------------------------------
		// When a skill goes off, its card's art sweeps in on the right behind the combo inside a slanted
		// band, glass shards flying off its edge, and leaves after about two seconds. Art with a
		// background (a card scene, no cut-out) is cut to the band.
		constexpr float CARD_CUTIN_DURATION = 2.2f;

		struct ClipVertex
		{
			ImVec2 pos, uv;
		};

		// keeps the part of a convex polygon on the left of a -> b (Sutherland-Hodgman, one edge)
		std::vector<ClipVertex> clipPolygon(const std::vector<ClipVertex>& in, ImVec2 a, ImVec2 b)
		{
			std::vector<ClipVertex> out;
			auto side = [&](ImVec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
			for (size_t i = 0; i < in.size(); ++i)
			{
				const ClipVertex& p = in[i];
				const ClipVertex& q = in[(i + 1) % in.size()];
				const float sp = side(p.pos), sq = side(q.pos);
				if (sp >= 0)
					out.push_back(p);
				if ((sp >= 0) != (sq >= 0))
				{
					const float t = sp / (sp - sq);
					out.push_back({ p.pos + (q.pos - p.pos) * t, p.uv + (q.uv - p.uv) * t });
				}
			}
			return out;
		}

		// a textured convex polygon (a picture cut to a shape)
		void imagePolygon(ImDrawList* dl, ImTextureID texture, const std::vector<ClipVertex>& poly, ImU32 color)
		{
			const int n = static_cast<int>(poly.size());
			if (n < 3)
				return;
			dl->PushTexture(texture);
			dl->PrimReserve((n - 2) * 3, n);
			const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
			for (const ClipVertex& v : poly)
				dl->PrimWriteVtx(v.pos, v.uv, color);
			for (int i = 2; i < n; ++i)
			{
				dl->PrimWriteIdx(base);
				dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i - 1));
				dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
			}
			dl->PopTexture();
		}

		// The game's 2D cut-in: the card's scene (member_cutout) in a panel that tilts down towards
		// the screen edge, its inner side fading out, glass shards off its corners. Skills take
		// turns: even ones on the right, odd ones on the left (mirrored).
		void drawCardCutin(const Layout& L, int index, float t)
		{
			if (t < 0.0f || t > CARD_CUTIN_DURATION)
				return;
			const Texture* art = cardTexture(index, CardArt::Cutin);
			if (!art || art->getWidth() <= 0 || art->getHeight() <= 0)
				return;
			ImDrawList* dl = L.dl;
			const bool left = index % 2 == 1;
			const float side = left ? -1.0f : 1.0f;
			const float enter = 1.0f - std::pow(1.0f - clamp01(t / 0.28f), 3.0f);
			const float leave = easeIn((t - (CARD_CUTIN_DURATION - 0.35f)) / 0.35f);
			const float alpha = enter * (1.0f - leave);
			if (alpha <= 0.002f)
				return;
			// in from the screen edge, out the same way
			const float shift = side * (240.0f * (1.0f - enter) + 200.0f * leave) - side * 14.0f * t;
			// layout for the right side; the left side mirrors x round the middle
			auto P = [&](float x, float y) { return L.at(left ? 1920.0f - x + shift : x + shift, y); };
			const ImVec2 inTop = P(1250.0f, 340.0f), outTop = P(1990.0f, 458.0f);
			const ImVec2 outBottom = P(1990.0f, 900.0f), inBottom = P(1380.0f, 700.0f);

			// the scene zoomed in on the face, like the game: the cut-outs are framed with the face
			// about a quarter of the way down, which lands in the middle of the panel
			const float artW = 1000.0f, artH = artW * art->getHeight() / art->getWidth();
			const ImVec2 focus = P(1600.0f, 600.0f);
			const ImVec2 a = focus - ImVec2(L.s(artW) * 0.5f, L.s(artH) * 0.28f);
			const ImVec2 b = a + ImVec2(L.s(artW), L.s(artH));
			std::vector<ClipVertex> poly{ { a, ImVec2(0, 0) }, { ImVec2(b.x, a.y), ImVec2(1, 0) }, { b, ImVec2(1, 1) }, { ImVec2(a.x, b.y), ImVec2(0, 1) } };
			// the panel's edges, walked so that "left of each edge" is inside (y grows downwards)
			const ImVec2 quad[4] = { inTop, outTop, outBottom, inBottom };
			for (int e = 0; e < 4; ++e)
			{
				const ImVec2 p0 = left ? quad[(4 - e) % 4] : quad[e];
				const ImVec2 p1 = left ? quad[(3 - e) % 4] : quad[(e + 1) % 4];
				poly = clipPolygon(poly, p0, p1);
			}
			// inner side fades out over 110 px from the panel's inner edge
			const ImVec2 edgeDir = inBottom - inTop;
			auto innerFade = [&](ImVec2 p) {
				const float k = edgeDir.y != 0.0f ? (p.y - inTop.y) / edgeDir.y : 0.0f;
				const float edgeX = inTop.x + edgeDir.x * k;
				return clamp01((p.x - edgeX) * side / L.s(110.0f));
			};
			// cut the panel at the end of the fade: only that strip gets per-vertex alpha, the rest is
			// solid (one big fan with zero-alpha corners smeared see-through over the whole card)
			const ImVec2 off(side * L.s(110.0f), 0.0f);
			std::vector<ClipVertex> halfA = clipPolygon(poly, inTop + off, inBottom + off);
			std::vector<ClipVertex> halfB = clipPolygon(poly, inBottom + off, inTop + off);
			auto centroidFade = [&](const std::vector<ClipVertex>& h) {
				ImVec2 c(0, 0);
				for (const ClipVertex& v : h)
					c += v.pos;
				return h.empty() ? 0.0f : innerFade(c / static_cast<float>(h.size()));
			};
			if (centroidFade(halfA) > centroidFade(halfB))
				std::swap(halfA, halfB);
			// halfA = fading strip, halfB = solid part
			auto fan = [&](const std::vector<ClipVertex>& h, bool fade) {
				const int n = static_cast<int>(h.size());
				if (n < 3)
					return;
				dl->PrimReserve((n - 2) * 3, n);
				const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
				for (const ClipVertex& v : h)
					dl->PrimWriteVtx(v.pos, v.uv, col(ImVec4(1, 1, 1, 1), alpha * (fade ? innerFade(v.pos) : 1.0f)));
				for (int i = 2; i < n; ++i)
				{
					dl->PrimWriteIdx(base);
					dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i - 1));
					dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
				}
			};
			dl->PushTexture(texId(*art));
			fan(halfA, true);
			fan(halfB, false);
			dl->PopTexture();

			// light streaks along the top and bottom edges
			const float flash = 1.0f - clamp01(t / 0.3f);
			dl->AddLine(inTop + (outTop - inTop) * 0.12f, outTop, col(ImVec4(1, 1, 1, 1), (0.55f + 0.45f * flash) * alpha), L.s(2.5f + 6.0f * flash));
			dl->AddLine(inBottom + (outBottom - inBottom) * 0.25f, outBottom, col(ImVec4(1, 1, 1, 1), (0.45f + 0.55f * flash) * alpha), L.s(2.0f + 6.0f * flash));

			// glass shards: a burst at the inner top corner and along the bottom edge
			for (int i = 0; i < 16; ++i)
			{
				const bool topCorner = i < 7;
				const float born = 0.02f + hash01(index * 31 + i) * 0.2f;
				const float life = clamp01((t - born) / (0.9f + hash01(i * 7 + 3) * 0.7f));
				if (life <= 0.0f || life >= 1.0f)
					continue;
				const ImVec2 start = topCorner ? inTop + (outTop - inTop) * (0.05f + 0.25f * hash01(i * 13 + 1))
					: inBottom + (outBottom - inBottom) * (0.3f + 0.65f * hash01(i * 13 + 1));
				const float angle = topCorner ? 3.14159f * (1.1f + 0.5f * hash01(i * 17 + 5)) : 3.14159f * (0.15f + 0.5f * hash01(i * 17 + 5));
				ImVec2 dir(std::cos(angle), std::sin(angle));
				if (left)
					dir.x = -dir.x;
				const ImVec2 cc = start + dir * L.s(60.0f + hash01(i * 19 + 2) * 170.0f) * easeOut(life);
				const float size = L.s(14.0f + hash01(i * 23 + 4) * 40.0f) * (1.0f - life * 0.3f);
				const float spin = hash01(i * 29 + 6) * 6.28f + life * (1.5f + hash01(i) * 3.0f);
				ImVec2 q[3];
				for (int k = 0; k < 3; ++k)
				{
					const float ak = spin + k * 2.094f + (k == 1 ? 0.6f : 0.0f);
					q[k] = cc + ImVec2(std::cos(ak), std::sin(ak)) * size * (k == 2 ? 0.35f : 1.0f);
				}
				const float shardAlpha = (1.0f - life * life) * alpha;
				dl->AddTriangleFilled(q[0], q[1], q[2], col(hex(0xdff0ff), 0.55f * shardAlpha));
				dl->AddTriangle(q[0], q[1], q[2], col(ImVec4(1, 1, 1, 1), 0.9f * shardAlpha), L.s(1.2f));
			}
		}

		// ---- the 2.8.0 HUD ---------------------------------------------------------------------------
		// The game's UI before the 3.0 redesign (what a 2.8.0 client shows), from pjsekai-overlay-APPEND's
		// v1 skin: sekai-v1.obj draws it, v1-skin_*.exo places it. Positions there are AviUtl's (object
		// centre, y down from the frame's centre); here they are 1080p frame pixels. Animations run on
		// AviUtl's 60 fps frames, as in the scripts.
		namespace V1
		{
			// one AviUtl object: images drawn round its centre at `scale` x the object's own zoom
			struct Object
			{
				const Layout& L;
				const ScoreSkin& s;
				ImVec2 centre;		// screen pixels
				float zoom;			// the object's 拡大率

				ImVec2 at(float x, float y) const { return centre + ImVec2(x, y) * (zoom * L.ui); }
				// "assets/score/f_0" at (x, y), centred, `scale` like obj.draw's
				void image(const std::string& name, float x, float y, float scale = 1.0f, float alpha = 1.0f) const
				{
					const Texture* t = s.texture(name + ".png");
					if (!t || alpha <= 0.002f || scale <= 0.0001f)
						return;
					const ImVec2 half = ImVec2(t->getWidth() * 0.5f, t->getHeight() * 0.5f) * (scale * zoom * L.ui);
					const ImVec2 c = at(x, y);
					L.dl->AddImage(texId(*t), c - half, c + half, ImVec2(0, 0), ImVec2(1, 1), white(alpha));
				}
				// the image cut at `right` (object pixels): what is left of the line stays; `slant` leans the cut
				// (pixels to the right per pixel up), like the life bar's turned triangle mask
				void imageLeftOf(const std::string& name, float right, float slant, float alpha = 1.0f) const
				{
					const Texture* t = s.texture(name + ".png");
					if (!t)
						return;
					const float w = static_cast<float>(t->getWidth()), h = static_cast<float>(t->getHeight());
					const ImVec2 a = at(-w * 0.5f, -h * 0.5f), b = at(w * 0.5f, h * 0.5f);
					std::vector<ClipVertex> poly{ { a, ImVec2(0, 0) }, { ImVec2(b.x, a.y), ImVec2(1, 0) }, { b, ImVec2(1, 1) }, { ImVec2(a.x, b.y), ImVec2(0, 1) } };
					// the cut line through (right, 0), leaning by `slant`; keep the part on its left
					const ImVec2 top = at(right + slant * h * 0.5f, -h * 0.5f), bottom = at(right - slant * h * 0.5f, h * 0.5f);
					poly = clipPolygon(poly, top, bottom);	// y grows downwards: top -> bottom keeps the left side
					imagePolygon(L.dl, texId(*t), poly, white(alpha));
				}
			};

			float frames(float seconds) { return seconds * 60.0f; }

			// スコア: score.png, the fill (inner.png, a sweep 610 px long), the rank letter, bars.png, the digits
			void drawScore(const Layout& L, const ScoreSkin& s, int score, char rank, float bar, int delta, float deltaAge)
			{
				const SkinElement e = s.element("score");
				if (!e.visible)
					return;
				const Object o{ L, s, L.atLeft(960.0f - 541.7f + e.x, 540.0f - 448.0f + e.y), e.scale };
				o.image("assets/score/score", 0, 0);
				o.imageLeftOf("assets/score/inner", -264.5f + 610.0f * clamp01(bar), 0.0f);
				const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(rank)));
				if (lower != 'd')
					o.image(std::string("assets/score/") + lower, -315.0f, 3.1f);
				o.image("assets/score/bars", 0, 0);

				// 8 digits, the unused ones as the pale "n" (APPEND's min_digit = 8)
				std::string text = std::to_string(std::max(0, score));
				if (text.size() < 8)
					text = std::string(8 - text.size(), 'n') + text;
				for (const char* layer : { "b_", "f_" })
					for (size_t c = 0; c < text.size(); ++c)
						o.image(std::string("assets/score/") + layer + text[c], -244.5f + 34.5f * c, 31.5f);

				// "+800": slides out of the score, 0.9^frames
				if (delta > 0 && deltaAge >= 0.0f && frames(deltaAge) <= 30.0f)
				{
					const float progress = std::min(1.0f, frames(deltaAge) / 12.0f);
					const float ease = 1.0f - std::pow(0.9f, progress * 12.0f);
					const std::string diff = "+" + std::to_string(delta);
					const float x = -9.5f + 67.5f * ease + 34.5f * (static_cast<float>(text.size()) - 8.0f);
					for (const char* layer : { "b_", "f_" })
						for (size_t c = 0; c < diff.size(); ++c)
							o.image(std::string("assets/score/") + layer + diff[c], 21.5f * c + x, 39.0f, 0.6f, std::min(1.0f, 1.3f * ease));
				}
			}

			// ライフ: the frame, the empty track, the bar (a slanted cut), the digits, the border
			void drawLife(const Layout& L, const ScoreSkin& s, int life)
			{
				const SkinElement e = s.element("life");
				if (!e.visible)
					return;
				const Object o{ L, s, L.atRight(960.0f + 699.0f + e.x, 540.0f - 452.5f + e.y), e.scale };
				o.image("assets/life/v1/placeholder", 0, 0);
				const bool danger = life <= 200;
				o.image(danger ? "assets/life/v1/danger_bg" : "assets/life/v1/bg", 0, 0);
				// the bar runs from x -166 to 101 round the object's centre; the cut leans like the game's
				o.imageLeftOf(danger ? "assets/life/v1/danger" : "assets/life/v1/normal", -166.0f + 267.0f * clamp01(life / 1000.0f), 0.78f);
				const std::string text = std::to_string(std::max(0, life));
				for (const char* layer : { "b_", "f_" })
					for (size_t i = 0; i < text.size(); ++i)
						o.image(std::string("assets/life/v1/digit/") + layer + text[i], 75.0f - (text.size() - 1 - i) * 25.0f, -33.45f, 0.73f);
				o.image("assets/life/v1/border", 0, 0);
			}

			// コンボ: the last 4 digits, popping from half size in 8 frames with a fading copy; ALL PERFECT
			// draws the rainbow digits over a pulsing glow (b_)
			void drawCombo(const Layout& L, const ScoreSkin& s, int combo, float sinceCombo, float time, bool allPerfect)
			{
				const SkinElement e = s.element("combo");
				if (!e.visible || combo <= 0)
					return;
				const Object o{ L, s, L.atRight(960.0f + 684.0f + e.x, 540.0f - 90.0f + e.y), 1.44f * e.scale };
				const float apAlpha = std::min(1.0f, (std::sin(time * PI * 4.0f / 3.0f) + 1.0f) * 0.5f);
				const char* face = allPerfect ? "assets/combo/p_" : "assets/combo/n_";
				if (allPerfect)
					o.image("assets/combo/b_c", 0, -70.0f, 0.67f, apAlpha);
				o.image(std::string(face) + "c", 0, -67.0f, 0.67f);

				std::string text = std::to_string(combo);
				if (text.size() > 4)
					text = text.substr(text.size() - 4);
				const float progress = frames(sinceCombo);
				const int n = static_cast<int>(text.size());
				auto row = [&](float fax, float alpha) {
					for (int i = 0; i < n; ++i)
					{
						const float shift = -(n / 2.0f) + (i + 1) - 0.5f;
						if (allPerfect)
							o.image(std::string("assets/combo/b_") + text[i], shift * 72.0f * fax, 0, 0.70f * fax, apAlpha * alpha);
						o.image(std::string(face) + text[i], shift * 72.0f * fax, 0, 0.70f * fax, alpha);
					}
				};
				row(std::min(1.0f, (progress / 8.0f) * 0.5f + 0.5f), 1.0f);
				if (progress < 14.0f)
					row((progress / 8.0f) * 0.5f + 0.5f, 1.0f - progress / 14.0f);
			}

			// 判定: PERFECT, hidden 2 frames, grows in by frame 5, gone at frame 20
			void drawJudge(const Layout& L, const ScoreSkin& s, float since)
			{
				const SkinElement e = s.element("judge");
				const float progress = frames(since);
				if (!e.visible || since < 0.0f || progress < 2.0f || progress >= 20.0f)
					return;
				const Object o{ L, s, L.at(960.0f + e.x, 540.0f + 127.5f + e.y), 1.5f * e.scale };
				const float scale = progress < 5.0f ? 0.686f - std::pow(-1.45f + progress / 4.0f, 4.0f) * 0.686f : 0.686f;
				o.image("assets/judge/v1/1", 0, 0, scale);
			}
		}

		// ---- co-op members ----------------------------------------------------------------------------
		// Multi Live / Cheerful Carnival: five members' cards under the score with their combos ("YOU"
		// under yours). Through FEVER CHANCE they turn to "OK!" one by one, all of them once the gauge
		// reaches FEVER, and stay that way until FEVER ends.
		void drawMembers(const Layout& L, const ScoreSkin& s, const std::vector<float>& noteTimes, int latest, double now, const EffectTimes& times)
		{
			const Hud h{ L, s, L.lt, s.element("score") };
			if (!h.e.visible)
				return;
			ImDrawList* dl = L.dl;
			ImFont* font = Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont();
			const double feverEnd = times.feverEnd > 0 ? times.feverEnd : times.feverStart + 10.0;
			const bool hasFever = times.feverChance >= 0 && times.feverStart > times.feverChance;
			for (int m = 0; m < DECK_SIZE; ++m)
			{
				const float x = 192.0f + m * 88.5f, y = 150.0f, size = 80.0f;
				const ImVec2 a = h.at(x, y), b = h.at(x + size, y + size);

				// when this member's "OK!" shows: you when the fill passes 60%, the others in between
				bool ok = false;
				if (hasFever && now >= times.feverChance && now <= feverEnd)
				{
					const float when = m == 0 ? 0.6f : 0.25f + 0.7f * hash01(m * 41 + 7);
					ok = now >= times.feverStart || now >= times.feverChance + when * (times.feverStart - times.feverChance);
				}

				// members other than you drop their combo now and then (a fixed pattern per member)
				int combo = latest + 1;
				if (m != 0)
				{
					int last = -1;
					for (int n = 0; n <= latest; ++n)
						if (hash01(n * 7919 + m * 104729) < 0.004f + 0.002f * m)
							last = n;
					combo = latest - last;
				}

				if (ok)
				{
					dl->AddRectFilled(a, b, IM_COL32(0xff, 0xc4, 0x3a, 255), h.px(10.0f));
					dl->AddRect(a, b, IM_COL32(0xff, 0xea, 0xa8, 255), h.px(10.0f), 0, h.px(2.5f));
					const float px = h.px(34.0f);
					const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, "OK!");
					const ImVec2 tp = (a + b - ts) * 0.5f;
					outlineText(dl, tp, px, "OK!", IM_COL32(0xd0, 0x7a, 0x10, 200), h.px(1.6f));
					dl->AddText(font, px, tp, IM_COL32_WHITE, "OK!");
				}
				else
				{
					dl->AddRectFilled(a, b, IM_COL32(0x44, 0x44, 0x66, 230), h.px(8.0f));
					const float inset = h.px(3.0f);
					if (const Texture* thumb = cardTexture(m, CardArt::Thumbnail))
						dl->AddImageRounded(texId(*thumb), a + ImVec2(inset, inset), b - ImVec2(inset, inset), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, h.px(6.0f));
					const int rarity = std::clamp(deckCard(m).rarity, 1, 5);
					const std::string frame = rarity == 5 ? "hud/cardFrame_S_bd" : "hud/cardFrame_S_" + std::to_string(rarity);
					if (const Texture* f = s.texture(frame + ".png"))
						dl->AddImage(texId(*f), a, b);
				}

				// under the card, like the game: YOU in bold teal, or the member's combo in bold white
				// over a tiny COMBO, all with a dark outline
				const float cx = x + size * 0.5f;
				const ImU32 stroke = IM_COL32(0x3a, 0x38, 0x5c, 255);
				auto stroked = [&](const char* text, float px, float top, ImU32 fill, float width) {
					const float size = h.px(px);
					const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
					const ImVec2 p = h.at(cx, top) - ImVec2(ts.x * 0.5f, 0);
					const float w = h.px(width);
					for (int k = 0; k < 16; ++k)
					{
						const float a = k * PI / 8.0f;
						const float r = k % 2 ? w : w * 0.6f;
						dl->AddText(font, size, p + ImVec2(std::cos(a), std::sin(a)) * r, stroke, text);
					}
					dl->AddText(font, size, p, fill, text);
				};
				if (m == 0)
					stroked("YOU", 30.0f, y + size + 6.0f, IM_COL32(0x5c, 0xe6, 0xd2, 255), 2.4f);
				else if (combo > 0)
				{
					// the game's combo digits and COMBO, small (the same art as the big combo)
					const std::string text = std::to_string(combo);
					const float scale = 0.17f, spacing = 102.6f * scale;
					const float right = cx + (static_cast<float>(text.size()) - 1.0f) * spacing * 0.5f;
					for (size_t i = 0; i < text.size(); ++i)
						h.sprite(std::string("hud/number_combo_154px_") + text[text.size() - 1 - i] + "_pr", right - static_cast<float>(i) * spacing, y + size + 16.0f, scale);
					h.sprite("hud/txt_combo_pr", cx, y + size + 32.0f, 0.27f);
				}
			}
		}

		// ---- FEVER ---------------------------------------------------------------------------------

		struct LaneEdges
		{
			ImVec2 leftBottom, leftTop, rightBottom, rightTop;
		};

		LaneEdges laneEdges(const Layout& L, const Projector& projector)
		{
			if (projector)
			{
				const float topDepth = 47.0f / 850.0f + 0.02f;
				// projector x is already multiplied by the depth; 6.35 = the lanes plus their rails
				constexpr float W = 6.35f, D = 1.08f;
				return { projector(-W * D, D), projector(-W * topDepth, topDepth), projector(W * D, D), projector(W * topDepth, topDepth) };
			}
			// the stage of a 16:9 frame
			return { L.at(70, 1080), L.at(906, 82), L.at(1850, 1080), L.at(1014, 82) };
		}

		void lightBeam(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImVec4& color, float alpha, float width)
		{
			for (int i = 4; i >= 0; --i)
			{
				const float w = width * (1.0f + i * 1.6f);
				dl->AddLine(a, b, col(i == 0 ? Canvas::SekaiStyle::mix(color, ImVec4(1, 1, 1, 1), 0.65f) : color, alpha * (i == 0 ? 1.0f : 0.22f / i)), w);
			}
		}

		// "FEVER CHANCE!" / "FEVER!" / "SUPER FEVER!!": beams along the lane edges and the title
		void drawFeverBanner(const Layout& L, const ScoreSkin& s, const LaneEdges& edges, float t, const std::string& text, const ImVec4& color, bool rainbow)
		{
			const float duration = 2.35f;
			if (t < 0 || t > duration)
				return;
			const float beams = window4(t, 0.0f, 0.12f, 1.0f, 1.6f);
			const float title = window4(t, 0.15f, 0.35f, 1.8f, duration);
			ImDrawList* dl = L.dl;
			const float grow = easeOut(t / 0.5f);
			auto along = [&](ImVec2 a, ImVec2 b) { return a + (b - a) * grow; };
			const ImVec4 beamColor = rainbow ? Canvas::SekaiStyle::mix(color, ImVec4(0.6f, 0.9f, 1, 1), 0.5f + 0.5f * std::sin(t * 6.0f)) : color;
			lightBeam(dl, edges.leftBottom, along(edges.leftBottom, edges.leftTop), beamColor, beams, L.s(5.0f));
			lightBeam(dl, edges.rightBottom, along(edges.rightBottom, edges.rightTop), beamColor, beams, L.s(5.0f));
			// glow where the beams meet
			const ImVec2 apex = (edges.leftTop + edges.rightTop) * 0.5f;
			for (int i = 0; i < 6; ++i)
				dl->AddCircleFilled(apex, L.s(60.0f - i * 9.0f), col(beamColor, beams * grow * 0.05f), 32);

			SkinElement e = s.element("fever");
			e.x += 960.0f;
			e.y += 640.0f;
			const float px = L.s(58.0f * e.scale);
			const ImVec2 size = textSize(text.c_str(), px);
			const float drift = (1.0f - easeOut((t - 0.15f) / 0.4f)) * 40.0f;
			const ImVec2 p = L.at(e.x, e.y) - size * 0.5f + ImVec2(L.s(drift), 0);
			glowText(dl, p, px, text.c_str(), color, title, L.s(4.0f));
			outlineText(dl, p, px, text.c_str(), col(ImVec4(0.08f, 0.05f, 0.12f, 1), title * 0.75f), L.s(2.0f));
			std::vector<ImVec4> stops = rainbow ? std::vector<ImVec4>{ hex(0xfff2a0), hex(0xffb0e4), hex(0xd2a4ff), hex(0x8ff3ff) }
				: std::vector<ImVec4>{ Canvas::SekaiStyle::mix(color, ImVec4(1, 1, 1, 1), 0.7f), color, Canvas::SekaiStyle::mix(color, ImVec4(1, 1, 1, 1), 0.5f) };
			gradientText(dl, p, px, text.c_str(), stops, title, rainbow ? t * 0.5f : 0.0f);
		}

		// FEVER gauge drawn along the lane edges when the overlay art is missing
		void drawBuiltinFeverGauge(const Layout& L, const ScoreSkin& s, const LaneEdges& edges, const EffectTimes& times, double now, bool carnival)
		{
			if (times.feverChance < 0 || times.feverStart <= times.feverChance)
				return;
			const double end = times.feverEnd > 0 ? times.feverEnd : times.feverStart + 10.0;
			if (now < times.feverChance || now > end + 0.6)
				return;
			ImDrawList* dl = L.dl;
			const ImVec4 chance = s.color("fever_chance");
			const ImVec4 fever = carnival ? s.color("super_fever") : s.color("fever");
			const bool active = now >= times.feverStart;
			const float fade = now > end ? 1.0f - static_cast<float>((now - end) / 0.6) : 1.0f;
			const float target = carnival ? 1.0f : 0.84f;	// reaching the FEVER! mark starts FEVER
			const float fill = active ? 1.0f : static_cast<float>((now - times.feverChance) / (times.feverStart - times.feverChance)) * target;
			auto gaugeLine = [&](ImVec2 bottom, ImVec2 top) {
				dl->AddLine(bottom, top, col(ImVec4(1, 1, 1, 1), 0.10f * fade), L.s(6.0f));
				const ImVec2 to = bottom + (top - bottom) * fill;
				const ImVec4 c = active ? fever : Canvas::SekaiStyle::mix(chance, fever, fill * 0.4f);
				lightBeam(dl, bottom, to, c, (active ? 0.75f + 0.25f * std::sin(static_cast<float>(now) * 10.0f) : 0.85f) * fade, L.s(3.5f));
				// threshold marks
				for (float mark : { 0.84f, 1.0f })
				{
					const ImVec2 m = bottom + (top - bottom) * mark;
					dl->AddLine(m - ImVec2(L.s(12), 0), m + ImVec2(L.s(12), 0), col(ImVec4(1, 1, 1, 1), 0.6f * fade), L.s(2));
				}
			};
			gaugeLine(edges.leftBottom, edges.leftTop);
			gaugeLine(edges.rightBottom, edges.rightTop);
			// labels by the right edge, like the game
			const float px = L.s(16.0f);
			const ImVec2 feverMark = edges.rightBottom + (edges.rightTop - edges.rightBottom) * 0.84f;
			const ImVec2 superMark = edges.rightTop;
			dl->AddText(Canvas::SekaiStyle::headerFont, px, feverMark + ImVec2(L.s(18), -px * 0.5f), col(fever, fade), ("< " + s.text("fever")).c_str());
			if (carnival)
				dl->AddText(Canvas::SekaiStyle::headerFont, px, superMark + ImVec2(L.s(18), -px * 0.5f), col(s.color("super_fever"), fade), ("< " + s.text("super_fever")).c_str());
		}

		// ---- ALL PERFECT -----------------------------------------------------------------------------

		void drawAllPerfect(const Layout& L, const ScoreSkin& s, float t)
		{
			SkinElement e = s.element("all_perfect");
			e.x += 960.0f;
			e.y += 470.0f;
			if (!e.visible || t < 0.0f || t > ALL_PERFECT_DURATION)
				return;
			ImDrawList* dl = L.dl;
			const ImVec2 c = L.at(e.x, e.y);
			const float fadeOut = 1.0f - clamp01((t - (static_cast<float>(ALL_PERFECT_DURATION) - 0.7f)) / 0.7f);
			const float appear = easeOut(t / 0.35f);

			// darken behind it a little
			dl->AddRectFilled(L.at(0, 0), L.at(1920, 1080), IM_COL32(0, 0, 10, static_cast<int>(90 * appear * fadeOut)));

			// lens flare streak through the middle
			const float streak = window4(t, 0.0f, 0.12f, 0.5f, 1.4f);
			const float sw = L.s(1920.0f) * (0.4f + 0.6f * easeOut(t / 0.4f));
			dl->AddRectFilledMultiColor(c - ImVec2(sw * 0.5f, L.s(3)), c + ImVec2(0, L.s(3)), col(hex(0x9fd8ff), 0), col(hex(0xe8f6ff), streak), col(hex(0xe8f6ff), streak), col(hex(0x9fd8ff), 0));
			dl->AddRectFilledMultiColor(c - ImVec2(0, L.s(3)), c + ImVec2(sw * 0.5f, L.s(3)), col(hex(0xe8f6ff), streak), col(hex(0x9fd8ff), 0), col(hex(0x9fd8ff), 0), col(hex(0xe8f6ff), streak));
			for (int i = 0; i < 5; ++i)
				dl->AddCircleFilled(c, L.s(220.0f - i * 40.0f), col(hex(0xbfe4ff), streak * 0.05f), 48);

			// a ring expanding from the centre
			const float ringT = clamp01((t - 0.1f) / 1.6f);
			if (ringT > 0 && ringT < 1)
				dl->AddCircle(c, L.s(80.0f + 620.0f * easeOut(ringT)), col(ImVec4(1, 1, 1, 1), (1.0f - ringT) * 0.55f * fadeOut), 96, L.s(2.0f));

			// twinkling sparkles
			for (int i = 0; i < 46; ++i)
			{
				const float x = hash01(i * 3 + 1) * 1920.0f, y = hash01(i * 3 + 2) * 1080.0f;
				const float phase = hash01(i * 3 + 3) * 6.28f;
				const float tw = 0.5f + 0.5f * std::sin(t * 5.0f + phase);
				const float a = appear * fadeOut * tw * clamp01((t - hash01(i) * 0.8f) / 0.3f);
				sparkle(dl, L.at(x, y), L.s(2.0f + 5.0f * hash01(i * 7)), col(ImVec4(1, 1, 1, 1), a * 0.85f));
			}

			// the title: pastel rainbow, ghost outlines above and below, a soft glow
			const std::string text = s.text("all_perfect");
			const float zoom = 1.0f + 0.35f * (1.0f - easeOutBack(t / 0.45f));
			const float px = L.s(118.0f * e.scale) * zoom;
			const ImVec2 size = textSize(text.c_str(), px);
			const ImVec2 p = c - size * 0.5f;
			const float alpha = appear * fadeOut;
			const float ghost = window4(t, 0.3f, 0.6f, 2.5f, 3.5f) * 0.35f;
			ImFont* font = Canvas::SekaiStyle::headerFont ? Canvas::SekaiStyle::headerFont : ImGui::GetFont();
			for (float dy : { -0.62f, 0.62f })
			{
				const ImVec2 gp = p + ImVec2(0, size.y * dy);
				for (int i = 0; i < 8; ++i)
				{
					const float a = i * PI * 0.25f;
					dl->AddText(font, px, gp + ImVec2(std::cos(a), std::sin(a)) * L.s(1.5f), col(ImVec4(1, 1, 1, 1), ghost * alpha), text.c_str());
				}
				dl->AddText(font, px, gp, col(ImVec4(0, 0, 0, 1), ghost * alpha), text.c_str());
			}
			glowText(dl, p, px, text.c_str(), hex(0xd8c8ff), alpha, L.s(7.0f));
			outlineText(dl, p, px, text.c_str(), col(ImVec4(1, 1, 1, 1), alpha * 0.9f), L.s(2.2f));
			gradientText(dl, p, px, text.c_str(), { s.color("ap_1"), s.color("ap_2"), s.color("ap_3"), s.color("ap_4") }, alpha, 0.0f);
			// a light sweep over the letters
			const float sweep = std::fmod(t * 0.45f, 1.6f) - 0.3f;
			if (sweep > -0.2f && sweep < 1.2f)
			{
				const int start = dl->VtxBuffer.Size;
				dl->AddText(font, px, p, IM_COL32_WHITE, text.c_str());
				for (int i = start; i < dl->VtxBuffer.Size; ++i)
				{
					ImDrawVert& v = dl->VtxBuffer[i];
					const float u = (v.pos.x - p.x) / std::max(1.0f, size.x);
					const float d = std::abs(u - sweep);
					const float a = clamp01(1.0f - d / 0.08f) * 0.6f * alpha;
					v.col = (v.col & 0x00ffffff) | (static_cast<ImU32>(a * ((v.col >> IM_COL32_A_SHIFT) & 0xff)) << IM_COL32_A_SHIFT);
				}
			}
		}

		// ---- the FEVER gauge ----------------------------------------------------------------------
		// FEVER CHANCE: the track (t.png, with its FEVER! / SUPER FEVER!! marks) fades in along the lane
		// edges and its rainbow fill (fx_fever_gauge, looping) climbs one step per note until it
		// reaches the FEVER! mark (SUPER FEVER!! in Cheerful Carnival) as FEVER starts. Then the gauge
		// flashes, the track fades and the FEVER frame glows with its particles until FEVER ends.
		constexpr float FEVER_MARK_Y = 221.7f, SUPER_FEVER_MARK_Y = 96.1f;	// in t.png, 1080p

		// FEVER's colour at a spot u (0..1 round the frame): purple and pink for FEVER, a pastel
		// rainbow running round for SUPER FEVER
		ImVec4 feverColor(float u, float t, bool super)
		{
			if (!super)
				return mixc(hex(0xb46bff), hex(0xff7ad9), 0.5f + 0.5f * std::sin(t * 2.5f + u * 12.0f));
			static const ImVec4 rainbow[] = { hex(0xff8fb8), hex(0xffd36b), hex(0x8cf5a8), hex(0x7ad8ff), hex(0xb38cff), hex(0xff8fb8) };
			const float f = std::fmod(u * 2.0f + t * 0.35f, 1.0f) * 5.0f;
			const int k = std::min(4, static_cast<int>(f));
			return mixc(rainbow[k], rainbow[k + 1], f - k);
		}

		// The FEVER frame round a rectangle: a steady white line (the game's) with a coloured glow and
		// sparkles drifting along it. `scale` = pixels per 1080p pixel.
		void drawFeverFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float t, float alpha, bool super, float scale)
		{
			if (alpha <= 0.002f || b.x - a.x < 8.0f || b.y - a.y < 8.0f)
				return;
			const float inset = 7.0f * scale;
			a += ImVec2(inset, inset);
			b -= ImVec2(inset, inset);
			const ImVec2 corners[4] = { a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y) };
			const float perimeter = 2.0f * ((b.x - a.x) + (b.y - a.y));
			auto pointAt = [&](float u) {
				float d = std::fmod(u, 1.0f) * perimeter;
				for (int e = 0; e < 4; ++e)
				{
					const ImVec2 p = corners[e], q = corners[(e + 1) % 4];
					const float len = std::sqrt(ImLengthSqr(q - p));
					if (d <= len)
						return p + (q - p) * (d / std::max(1.0f, len));
					d -= len;
				}
				return corners[0];
			};
			// glow: short segments round the frame, each in its own colour, wide and faint down to thin
			constexpr int SEGMENTS = 96;
			const float pulse = 0.85f + 0.15f * std::sin(t * 6.0f);
			for (int layer = 5; layer >= 1; --layer)
			{
				const float width = layer * 5.0f * scale;
				const float layerAlpha = alpha * pulse * (0.07f + 0.05f * (6 - layer));
				for (int i = 0; i < SEGMENTS; ++i)
				{
					const float u0 = i / static_cast<float>(SEGMENTS), u1 = (i + 1) / static_cast<float>(SEGMENTS);
					dl->AddLine(pointAt(u0), pointAt(u1), col(feverColor(u0, t, super), layerAlpha), width);
				}
			}
			dl->AddRect(a, b, col(ImVec4(1, 1, 1, 1), alpha * 0.95f), 0.0f, 0, 2.5f * scale);
			// brighter ticks at the corners and the middles of the sides, like the clip's
			for (int e = 0; e < 4; ++e)
			{
				const ImVec2 p = corners[e], q = corners[(e + 1) % 4];
				const ImVec2 dir = (q - p) * (1.0f / std::max(1.0f, std::sqrt(ImLengthSqr(q - p))));
				for (float f : { 0.0f, 0.5f })
				{
					const ImVec2 c = p + (q - p) * f;
					dl->AddLine(c - dir * 40.0f * scale, c + dir * 40.0f * scale, col(ImVec4(1, 1, 1, 1), alpha), 5.0f * scale);
				}
			}
			// sparkles drifting along the frame
			const ImVec2 middle = (a + b) * 0.5f;
			for (int i = 0; i < 48; ++i)
			{
				const float u = std::fmod(hash01(i * 3 + 1) + t * (0.02f + 0.03f * hash01(i * 3 + 2)), 1.0f);
				const ImVec2 p = pointAt(u);
				const ImVec2 inward = middle - p;
				const ImVec2 off = inward * (1.0f / std::max(1.0f, std::sqrt(ImLengthSqr(inward)))) * (hash01(i * 7 + 5) * 26.0f * scale);
				const float tw = 0.5f + 0.5f * std::sin(t * 7.0f + hash01(i) * 6.28f);
				sparkle(dl, p + off, (2.5f + 4.0f * hash01(i * 11)) * scale, col(feverColor(u, t, super), alpha * tw));
			}
		}

		// how strong FEVER's frame is now (0 = none); `sinceStart` = how long FEVER has run
		float feverStrength(const EffectTimes& times, double now, double& sinceStart)
		{
			sinceStart = now - times.feverStart;
			if (times.feverStart < 0 || now < times.feverStart)
				return 0.0f;
			const double end = times.feverEnd > 0 ? times.feverEnd : times.feverStart + 10.0;
			const float in = clamp01(static_cast<float>(sinceStart / 0.35));
			const float out = 1.0f - clamp01(static_cast<float>((now - end) / 0.5));
			return in * out;
		}

		// pjsekai-overlay-MULTI's FEVER, layer for layer (its main_*.exo, layers 24-29):
		//   FEVER CHANCE  fx_fever_chance_v2 once; the track (t.png) and the looping fill
		//                 (fx_fever_gauge) wiped in from the bottom in 0.55 s; the fill climbs one step
		//                 per note and fx_fever_gauge_flash_v2 goes off when it is full
		//   FEVER starts  SUPER FEVER!!: fx_super_fever_v2 and the fx_super_fever_frame burst (once)
		//                 FEVER!:        fx_fever_v2
		//   during FEVER  fx_fever_frame looping with rainbow.png spinning inside it (720 to -720
		//                 degrees in 10 s at 850 %), and the fx_fever_frame_pt particles
		constexpr float GAUGE_REVEAL = 0.55f;	// the wipe (33 frames)
		constexpr float GAUGE_FULL_AT = 0.85f;	// share of FEVER CHANCE's notes that fills the gauge
		constexpr float RAINBOW_SIZE = 268.0f * 8.5f, RAINBOW_DEGREES_PER_SECOND = 144.0f;

		// FEVER's end (when the chart marks none: ten seconds in)
		double feverEndTime(const EffectTimes& times) { return times.feverEnd > 0 ? times.feverEnd : times.feverStart + 10.0; }

		// SUPER FEVER!! is reached as FEVER runs out: its banner plays so it finishes with FEVER, but
		// never on top of the FEVER! banner that opened it
		constexpr double SUPER_LEAD = 2.0;
		double superFeverTime(const EffectTimes& times) { return std::max(times.feverStart + 2.4, feverEndTime(times) - SUPER_LEAD); }

		// when the gauge fills up: after GAUGE_FULL_AT of the notes between FEVER CHANCE and FEVER
		double gaugeFullTime(const EffectTimes& times, const std::vector<float>& noteTimes)
		{
			const auto first = std::lower_bound(noteTimes.begin(), noteTimes.end(), static_cast<float>(times.feverChance) - 0.0001f);
			const auto last = std::lower_bound(noteTimes.begin(), noteTimes.end(), static_cast<float>(times.feverStart) + 0.0001f);
			const long total = static_cast<long>(last - first);
			if (total <= 0)
				return times.feverChance + GAUGE_FULL_AT * (times.feverStart - times.feverChance);
			const long needed = std::max(1L, static_cast<long>(std::ceil(total * GAUGE_FULL_AT)));
			return std::min(static_cast<double>(*(first + (needed - 1))), times.feverStart);
		}

		// Half black outside the lanes during FEVER CHANCE. The lane edges are only used between their
		// top and the bottom of the picture: past the top they cross (the vanishing point), which
		// is what drew a black diagonal through small previews.
		void dimOutsideLanes(const Layout& L, const LaneEdges& edges, float alpha)
		{
			if (alpha <= 0.0f)
				return;
			const ImU32 dim = IM_COL32(0, 0, 0, static_cast<int>(128 * alpha));
			const float left = L.pos.x, right = L.pos.x + L.size.x, top = L.pos.y, bottom = L.pos.y + L.size.y;
			auto atY = [](ImVec2 p, ImVec2 q, float y) {
				const float dy = q.y - p.y;
				return ImVec2(p.x + (q.x - p.x) * (y - p.y) / (std::abs(dy) < 0.001f ? 0.001f : dy), y);
			};
			const float laneTop = std::clamp(std::min(edges.leftTop.y, edges.rightTop.y), top, bottom);
			// everything above the lanes
			if (laneTop > top)
				L.dl->AddRectFilled(ImVec2(left, top), ImVec2(right, laneTop), dim);
			// the sides, from the lane top down
			const ImVec2 lt = atY(edges.leftBottom, edges.leftTop, laneTop), lb = atY(edges.leftBottom, edges.leftTop, bottom);
			const ImVec2 rt = atY(edges.rightBottom, edges.rightTop, laneTop), rb = atY(edges.rightBottom, edges.rightTop, bottom);
			const ImVec2 leftSide[4] = { ImVec2(left, laneTop), ImVec2(std::max(left, lt.x), laneTop), ImVec2(std::max(left, lb.x), bottom), ImVec2(left, bottom) };
			const ImVec2 rightSide[4] = { ImVec2(std::min(right, rt.x), laneTop), ImVec2(right, laneTop), ImVec2(right, bottom), ImVec2(std::min(right, rb.x), bottom) };
			L.dl->AddConvexPolyFilled(leftSide, 4, dim);
			L.dl->AddConvexPolyFilled(rightSide, 4, dim);
		}

		void drawFeverGauge(const Layout& L, const ScoreSkin& s, const LaneEdges& edges, const EffectTimes& times, double now,
			const std::vector<float>& noteTimes, bool super)
		{
			if (times.feverChance < 0 || times.feverStart <= times.feverChance)
				return;
			const double end = feverEndTime(times);
			if (now < times.feverChance || now > end + 0.5)
				return;

			const float dimIn = clamp01(static_cast<float>((now - times.feverChance) / 0.3));
			const float dimOut = 1.0f - clamp01(static_cast<float>((now - times.feverStart) / 0.3));
			dimOutsideLanes(L, edges, dimIn * dimOut);

			const Texture* track = s.texture("extra/fever/t.png");
			if (!track)
			{
				drawBuiltinFeverGauge(L, s, edges, times, now, true);
				return;
			}

			// the fill: one step per note up to the FEVER mark (gold), then, with SUPER FEVER on, it keeps
			// climbing through FEVER (rainbow) and tops out at the SUPER FEVER!! mark as FEVER ends
			const double fullAt = gaugeFullTime(times, noteTimes);
			float fill = 1.0f;
			if (now < fullAt)
			{
				auto count = [&](double from, double to) {
					return static_cast<float>(std::lower_bound(noteTimes.begin(), noteTimes.end(), static_cast<float>(to) + 0.0001f) -
						std::lower_bound(noteTimes.begin(), noteTimes.end(), static_cast<float>(from) - 0.0001f));
				};
				const float needed = count(times.feverChance, fullAt);
				fill = needed > 0 ? count(times.feverChance, now) / needed : static_cast<float>((now - times.feverChance) / (fullAt - times.feverChance));
			}
			float fillY = 1080.0f - clamp01(fill) * (1080.0f - FEVER_MARK_Y);
			if (super && now >= times.feverStart)
				fillY = FEVER_MARK_Y + (SUPER_FEVER_MARK_Y - FEVER_MARK_Y) * clamp01(static_cast<float>((now - times.feverStart) / std::max(0.1, end - times.feverStart)));
			// gold up to the FEVER mark; once FEVER! has played, what climbs past the mark is rainbow
			// (so the two never overlap: gold below the mark, rainbow above it)
			const float markYpx = L.at(0, FEVER_MARK_Y).y;
			const float goldTopY = std::max(fillY, FEVER_MARK_Y);
			const float rainbowGrow = clamp01(static_cast<float>((now - times.feverStart - 0.35) / 0.4));
			// the gauge ends on the judgement line, not below it
			float judgeY = L.pos.y + L.size.y;
			if (edges.leftBottom.y > L.pos.y + 1.0f && edges.rightBottom.y > L.pos.y + 1.0f)
				judgeY = std::min(judgeY, std::max(edges.leftBottom.y, edges.rightBottom.y));

			// shown from FEVER CHANCE to the end of FEVER (gone in 0.5 s after), wiped in from the bottom
			const float shown = 1.0f - clamp01(static_cast<float>((now - end) / 0.5));
			const float reveal = easeOut(static_cast<float>((now - times.feverChance) / GAUGE_REVEAL));
			if (shown > 0.0f && reveal > 0.0f)
			{
				// the wipe's soft edge (AviUtl's mask blur, 100 px): four bands fading in under it
				const float revealY = 1080.0f - reveal * 1180.0f;
				const double clipT = now - times.feverChance;
				for (int band = 0; band < 5; ++band)
				{
					const float bandTop = band == 4 ? revealY + 100.0f : revealY + band * 25.0f;
					const float bandBottom = band == 4 ? 1080.0f : revealY + (band + 1) * 25.0f;
					if (bandBottom <= bandTop || bandBottom <= 0.0f)
						continue;
					const float alpha = shown * (band == 4 ? 1.0f : (band + 0.5f) / 4.0f);
					L.dl->PushClipRect(ImVec2(L.pos.x, std::max(L.pos.y, L.at(0, bandTop).y)), ImVec2(L.pos.x + L.size.x, std::min(judgeY, L.at(0, bandBottom).y)), true);
					image(L, track, L.at(0, 0), L.at(1920, 1080), alpha);
					{
						// the fill clip's shape with its colours swapped: gold from the bottom up to the mark
						Clip& gold = clip("fever_gauge_gold");
						const Texture* goldTexture = s.texture("extra/fever/gold.png");
						gold.player.setTint(goldTexture ? goldTexture->getID() : 0, 0.0f, 64.0f);
						drawClip(L, s, "fever_gauge_gold", "extra/fever/fx_fever_gauge.mp4", clipT, true, alpha, 0, L.at(0, goldTopY).y);
					}
					if (rainbowGrow > 0.0f && fillY < FEVER_MARK_Y)
					{
						// ...and the rainbow between the mark and where the fill has got to
						Clip& rainbow = clip("fever_gauge");
						const Texture* gradient = s.texture("extra/fever/rainbow_gauge.png");
						rainbow.player.setTint(gradient ? gradient->getID() : 0, 0.0f, 1080.0f);
						L.dl->PushClipRect(ImVec2(L.pos.x, L.pos.y), ImVec2(L.pos.x + L.size.x, markYpx), true);
						drawClip(L, s, "fever_gauge", "extra/fever/fx_fever_gauge.mp4", clipT, true, alpha * rainbowGrow, 0, L.at(0, fillY).y);
						L.dl->PopClipRect();
					}
					L.dl->PopClipRect();
				}
			}
			// the gauge flashes when it is full
			if (now >= fullAt)
				drawClip(L, s, "fever_flash", "extra/fever/fx_fever_gauge_flash_v2.mp4", now - fullAt);
			if (now < times.feverStart)
				return;

			const double t = now - times.feverStart;
			// SUPER FEVER!! reached: the burst goes off with its banner
			if (super)
				drawClip(L, s, "fever_burst", "extra/fever/fx_super_fever_frame.mp4", now - superFeverTime(times), false, 1.0f, 0, -FLT_MAX, 0, true);
			// the frame and its particles, on the edges of the whole picture, until FEVER ends
			if (now <= end)
			{
				const float fade = 1.0f - clamp01(static_cast<float>((now - (end - 0.3)) / 0.3));
				Clip& frame = clip("fever_frame");
				const Texture* rainbow = s.texture("extra/fever/rainbow.png");
				const float degrees = 720.0f - RAINBOW_DEGREES_PER_SECOND * static_cast<float>(t);
				frame.player.setTint(rainbow ? rainbow->getID() : 0, degrees * PI / 180.0f, RAINBOW_SIZE);
				drawClip(L, s, "fever_frame", "extra/fever/fx_fever_frame.mp4", t, true, fade, 0, -FLT_MAX, 0, true);
				drawClip(L, s, "fever_particles", "extra/fever/fx_fever_frame_pt.mp4", t, true, fade, 0, -FLT_MAX, 0, true);
			}
		}
	}

	void drawEditorFeverFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, const ScoreContext& context, const OverlayOptions& options)
	{
		if (!dl || !options.fever || context.score.tempoChanges.empty())
			return;
		const EffectTimes times = effectTimes(context.score);
		double since = 0.0;
		const float strength = feverStrength(times, context.getTimeAtCurrentTick(), since);
		if (strength <= 0.0f)
			return;
		// the frame clips the preview just drew (same pictures, round the whole window)
		const int k = static_cast<int>(strength * 255.0f);
		for (const char* key : { "fever_frame", "fever_particles" })
		{
			auto it = clipCache().find(key);
			if (it == clipCache().end() || !it->second || !it->second->player.isOpen() || !it->second->player.getTexture())
				continue;
			dl->AddCallback(premultipliedOn, nullptr);
			dl->AddImage(static_cast<ImTextureID>(it->second->player.getTexture()), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(k, k, k, k));
			dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}
	}

	void clearClips()
	{
		clipCache().clear();
	}

	ImVec2 animatePoint(ImVec2 p, const Animation::Transform& t, ImVec2 pivot, float unit)
	{
		const float r = t.rotation * 3.14159265f / 180.0f;
		const float c = std::cos(r) * t.scale, sn = std::sin(r) * t.scale;
		const ImVec2 d = p - pivot;
		// y points down here, so a positive angle turns clockwise like on paper
		return ImVec2(pivot.x + d.x * c - d.y * sn + t.x * unit, pivot.y + d.x * sn + d.y * c + t.y * unit);
	}

	void animateVertices(ImDrawList* dl, int from, const Animation::Transform& t, float unit, const ImVec2* pivot)
	{
		if (!dl || from >= dl->VtxBuffer.Size || t.identity())
			return;
		ImDrawVert* first = dl->VtxBuffer.Data + from;
		ImDrawVert* last = dl->VtxBuffer.Data + dl->VtxBuffer.Size;
		ImVec2 middle;
		if (pivot)
			middle = *pivot;
		else
		{
			// the middle of whatever was drawn: good enough for a panel or a number
			ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
			for (ImDrawVert* v = first; v != last; ++v)
			{
				lo = ImMin(lo, v->pos);
				hi = ImMax(hi, v->pos);
			}
			middle = (lo + hi) * 0.5f;
		}
		const bool moves = t.x != 0 || t.y != 0 || t.scale != 1 || t.rotation != 0;
		for (ImDrawVert* v = first; v != last; ++v)
		{
			if (moves)
				v->pos = animatePoint(v->pos, t, middle, unit);
			if (t.opacity < 1.0f)
			{
				const ImU32 a = static_cast<ImU32>(((v->col >> IM_COL32_A_SHIFT) & 0xff) * std::max(0.0f, t.opacity) + 0.5f);
				v->col = (v->col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
			}
		}
	}

	void drawAnimationEffects(ImDrawList* dl, ImVec2 pos, ImVec2 size, const ScoreContext& context)
	{
		const auto& anim = context.score.animation;
		if (!dl || context.score.tempoChanges.empty())
			return;
		if (!anim.effects.empty())
		{
			const auto fx = Animation::effectsAt(anim, context.getTimeAtCurrentTick(),
				[&context](int tick) { return static_cast<double>(accumulateDuration(tick, TICKS_PER_BEAT, context.score.tempoChanges)); });
			if (fx.fade >> IM_COL32_A_SHIFT)
				dl->AddRectFilled(pos, pos + size, fx.fade);
			if (fx.flash >> IM_COL32_A_SHIFT)
				dl->AddRectFilled(pos, pos + size, fx.flash);
		}

		// after the ALL PERFECT!! has played, the whole picture (video, stage and HUD) goes slowly to black
		if (config.pvDrawAllPerfect)
		{
			const EffectTimes times = effectTimes(context.score);
			if (times.allPerfect >= 0)
			{
				const double t = context.getTimeAtCurrentTick() - times.allPerfect;
				const float black = clamp01(static_cast<float>((t - AP_FADE_START) / AP_FADE_LENGTH));
				if (black > 0.0f)
					dl->AddRectFilled(pos, pos + size, IM_COL32(0, 0, 0, static_cast<int>(black * 255.0f + 0.5f)));
			}
		}
	}

	void drawLiveOverlay(ImDrawList* dl, ImVec2 pos, ImVec2 size, const ScoreContext& context, const OverlayOptions& options, const Projector& projector)
	{
		CANVAS_PROFILE("overlay.draw");
		if (!dl || size.x <= 1.0f || size.y <= 1.0f || context.score.tempoChanges.empty())
			return;

		// start animation: covers the frame for the first 5 s of the chart
		if (drawIntroFor(dl, pos, size, context, options))
			return;

		Layout L;
		L.dl = dl;
		L.pos = pos;
		L.size = size;
		L.ui = hudScale(size.x, size.y);
		L.offsetX = (size.x - 1920.0f * L.ui) * 0.5f;
		L.offsetY = (size.y - 1080.0f * L.ui) * 0.5f;
		L.lt = ImVec2(pos.x + size.x * 0.01f, pos.y + size.y * 0.01f);
		L.rt = ImVec2(pos.x + size.x * 0.99f, pos.y + size.y * 0.01f);
		dl->PushClipRect(pos, pos + size, true);
		const int firstVertex = dl->VtxBuffer.Size;
		// combo + PERFECT keep the game's own 16:9 scale: hudScale's narrow-window boost is for the
		// score / life bars, and it made those two look huge
		Layout C = L;
		C.ui = std::min(size.x / 1920.0f, size.y / 1080.0f);
		C.offsetX = (size.x - 1920.0f * C.ui) * 0.5f;
		C.offsetY = (size.y - 1080.0f * C.ui) * 0.5f;

		const ScoreSkin& s = activeSkin();
		const auto& drawData = context.scorePreviewDrawData;
		const auto& comboTimes = drawData.comboTimes;
		const double now = context.getTimeAtCurrentTick();
		const float nowF = static_cast<float>(now);
		int latest = -1;
		if (!comboTimes.empty())
			latest = static_cast<int>(std::distance(comboTimes.begin(), std::upper_bound(comboTimes.begin(), comboTimes.end(), nowF + 0.0001f))) - 1;
		const int combo = latest + 1;
		const int score = latest >= 0 && latest < static_cast<int>(drawData.hudScores.size()) ? drawData.hudScores[latest] : 0;
		const char rank = latest >= 0 && latest < static_cast<int>(drawData.hudRanks.size()) ? drawData.hudRanks[latest] : 'd';
		const float bar = latest >= 0 && latest < static_cast<int>(drawData.hudScoreBars.size()) ? drawData.hudScoreBars[latest] : 0.0f;

		const EffectTimes times = effectTimes(context.score);
		int activeSkill = -1;
		float skillT = -1.0f;
		for (size_t i = 0; i < times.skills.size(); ++i)
			if (now >= times.skills[i] && now <= times.skills[i] + SKILL_DURATION + 0.3)
			{
				activeSkill = static_cast<int>(i);
				skillT = static_cast<float>(now - times.skills[i]);
			}

		// Score Preview Animation: every piece gets its own track, then everything the screen's
		const auto& anim = context.score.animation;
		const bool animated = !anim.empty();
		const ImVec2 middle = pos + size * 0.5f;
		auto piece = [&](const char* target, auto&& draw) {
			const int from = dl->VtxBuffer.Size;
			draw();
			if (animated)
				animateVertices(dl, from, Animation::evaluate(anim, target, context.currentTick), L.ui);
		};
		// the FEVER gauge hugs the lanes, so it follows the stage's track
		Projector lanes = projector;
		if (animated && projector)
		{
			const Animation::Transform stage = Animation::evaluate(anim, "stage", context.currentTick);
			if (!stage.identity())
				lanes = [projector, stage, middle, unit = size.y / 1080.0f](float laneX, float depth) {
					return animatePoint(projector(laneX, depth), stage, middle, unit);
				};
		}

		const LaneEdges edges = laneEdges(L, lanes);
		const bool coop = options.mode != LiveMode::Solo;

		// FEVER sits behind the HUD
		if (options.fever)
			piece("fever", [&] {
				drawFeverGauge(L, s, edges, times, now, comboTimes, options.superFever);
				if (times.feverChance >= 0)
				{
					const float t = static_cast<float>(now - times.feverChance);
					if (!drawClip(L, s, "fever_chance", "extra/fever/fx_fever_chance_v2.mp4", t))
						drawFeverBanner(L, s, edges, t, s.text("fever_chance"), s.color("fever_chance"), false);
				}
				if (times.feverStart >= 0)
				{
					const float t = static_cast<float>(now - times.feverStart);
					if (!drawClip(L, s, "fever_plain", "extra/fever/fx_fever_v2.mp4", t))
						drawFeverBanner(L, s, edges, t, s.text("fever"), s.color("fever"), false);
					if (options.superFever)
					{
						const float ts = static_cast<float>(now - superFeverTime(times));
						if (!drawClip(L, s, "fever", "extra/fever/fx_super_fever_v2.mp4", ts))
							drawFeverBanner(L, s, edges, ts, s.text("super_fever"), s.color("super_fever"), true);
					}
				}
			});

		// the card cut-in sweeps in behind the HUD
		if (options.cutins && activeSkill >= 0)
			piece("skill", [&] { drawCardCutin(L, activeSkill, skillT); });

		// what the latest note added to the score (notes on the same tick count together)
		int scoreDelta = 0;
		float scoreDeltaAge = -1.0f;
		if (options.scoreDelta && latest >= 0 && latest < static_cast<int>(drawData.hudScores.size()))
		{
			const float landed = comboTimes[latest];
			const int first = static_cast<int>(std::distance(comboTimes.begin(), std::lower_bound(comboTimes.begin(), comboTimes.end(), landed - 0.0001f)));
			scoreDelta = score - (first > 0 && first <= static_cast<int>(drawData.hudScores.size()) ? drawData.hudScores[first - 1] : 0);
			scoreDeltaAge = nowF - landed;
		}

		if (options.score)
			piece("score", [&] {
				if (options.ui280)
					V1::drawScore(L, s, score, rank, bar, scoreDelta, scoreDeltaAge);
				else
					drawScore(L, s, score, rank, bar, 0.0f, scoreDelta, scoreDeltaAge);
			});
		if (options.score && coop)
			piece("members", [&] { drawMembers(L, s, comboTimes, latest, now, times); });
		if (options.life)
			piece("life", [&] {
				if (options.ui280)
					V1::drawLife(L, s, 1000);
				else
					drawLife(L, s, 1.0f, 0.0f);
			});
		// the game's normal pink combo, or its rainbow ALL PERFECT one
		if (options.combo && combo > 0)
			piece("combo", [&] {
				if (options.ui280)
					V1::drawCombo(C, s, combo, std::max(0.0f, nowF - comboTimes[latest]), nowF, options.apCombo);
				else
					drawCombo(C, s, combo, std::max(0.0f, nowF - comboTimes[latest]), nowF, options.apCombo);
			});
		if (options.judge && !drawData.hudJudgeTimes.empty())
		{
			const auto& judges = drawData.hudJudgeTimes;
			const int j = static_cast<int>(std::distance(judges.begin(), std::upper_bound(judges.begin(), judges.end(), nowF + 0.0001f))) - 1;
			if (j >= 0)
				piece("judge", [&] {
					if (options.ui280)
						V1::drawJudge(C, s, nowF - judges[j]);
					else
						drawJudge(C, s, nowF - judges[j]);
				});
		}
		if (options.skills && activeSkill >= 0)
			piece("skill", [&] { drawSkill(L, s, skillT, activeSkill); });

		// get the big clips decoding before they are needed
		if (options.allPerfect && times.allPerfect >= 0)
			primeClip(s, options.ui280 ? "all_perfect_v1" : "all_perfect", options.ui280 ? "assets/ap-v1.mp4" : "assets/ap.mp4", times.allPerfect - now);
		if (options.fever && times.feverChance >= 0)
			primeClip(s, "fever_chance", "extra/fever/fx_fever_chance_v2.mp4", times.feverChance - now);
		if (options.fever && times.feverStart >= 0)
		{
			primeClip(s, "fever_plain", "extra/fever/fx_fever_v2.mp4", times.feverStart - now);
			if (options.superFever)
			{
				primeClip(s, "fever", "extra/fever/fx_super_fever_v2.mp4", superFeverTime(times) - now);
				primeClip(s, "fever_burst", "extra/fever/fx_super_fever_frame.mp4", superFeverTime(times) - now);
			}
		}

		// ALL PERFECT over everything
		if (options.allPerfect && times.allPerfect >= 0)
		{
			const float t = static_cast<float>(now - times.allPerfect);
			if (t >= 0.0f)
				piece("all_perfect", [&] {
					// at the game's own 16:9 scale (C), so it never runs past the edges of a narrow preview
					if (!drawClip(C, s, options.ui280 ? "all_perfect_v1" : "all_perfect", options.ui280 ? "assets/ap-v1.mp4" : "assets/ap.mp4", t, false, 1.0f, IM_COL32(0, 0, 0, 128)))
						drawAllPerfect(C, s, t);
				});
		}

		// the screen track (and any shake) moves the HUD with the picture under it
		if (animated)
		{
			const Animation::Transform screen = Animation::screenAt(anim, context.currentTick, now,
				[&context](int tick) { return static_cast<double>(accumulateDuration(tick, TICKS_PER_BEAT, context.score.tempoChanges)); });
			animateVertices(dl, firstVertex, screen, size.y / 1080.0f, &middle);
		}

		dl->PopClipRect();
	}
}
