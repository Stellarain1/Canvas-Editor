#include "PreviewAnimation.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>

namespace Canvas::Animation
{
	namespace
	{
		constexpr float PI = 3.14159265358979f;

		float lerp(float a, float b, float t) { return a + (b - a) * t; }

		uint32_t withAlpha(uint32_t color, float alpha)
		{
			const float a = std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>(color >> 24);
			return (color & 0x00ffffffu) | (static_cast<uint32_t>(a + 0.5f) << 24);
		}

		float bounceOut(float t)
		{
			// the classic four-hop bounce
			if (t < 1 / 2.75f) return 7.5625f * t * t;
			if (t < 2 / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
			if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
			t -= 2.625f / 2.75f;
			return 7.5625f * t * t + 0.984375f;
		}
	}

	const char* easeName(Ease e)
	{
		static const char* names[] = { "Linear", "Ease in", "Ease out", "Ease in-out", "Overshoot", "Bounce", "Jump" };
		const int i = static_cast<int>(e);
		return i >= 0 && i < static_cast<int>(Ease::Count) ? names[i] : names[0];
	}

	float ease(Ease e, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		switch (e)
		{
		case Ease::In: return t * t * t;
		case Ease::Out: { const float u = 1 - t; return 1 - u * u * u; }
		case Ease::InOut: return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
		case Ease::Back: { const float c = 1.70158f, u = t - 1; return 1 + (c + 1) * u * u * u + c * u * u; }
		case Ease::Bounce: return bounceOut(t);
		case Ease::Step: return t < 1.0f ? 0.0f : 1.0f;
		default: return t;
		}
	}

	const std::vector<Target>& targets()
	{
		static const std::vector<Target> list{
			{ "screen", "anim_target_screen", false },
			{ "stage", "anim_target_stage", false },
			{ "score", "anim_target_score", true },
			{ "life", "anim_target_life", true },
			{ "combo", "anim_target_combo", true },
			{ "judge", "anim_target_judge", true },
			{ "skill", "anim_target_skill", true },
			{ "members", "anim_target_members", true },
			{ "fever", "anim_target_fever", true },
			{ "all_perfect", "anim_target_all_perfect", true },
		};
		return list;
	}

	const char* effectName(EffectKind kind)
	{
		switch (kind)
		{
		case EffectKind::Shake: return "anim_fx_shake";
		case EffectKind::Flash: return "anim_fx_flash";
		default: return "anim_fx_fade";
		}
	}

	bool PreviewAnimation::empty() const
	{
		return effects.empty() && std::all_of(tracks.begin(), tracks.end(), [](const Track& t) { return t.keys.empty(); });
	}

	const Track* PreviewAnimation::track(const std::string& target) const
	{
		for (const Track& t : tracks)
			if (t.target == target)
				return &t;
		return nullptr;
	}

	Track& PreviewAnimation::trackFor(const std::string& target)
	{
		for (Track& t : tracks)
			if (t.target == target)
				return t;
		tracks.push_back({ target, {} });
		return tracks.back();
	}

	void PreviewAnimation::setKey(const std::string& target, const Key& key)
	{
		Track& t = trackFor(target);
		auto it = std::lower_bound(t.keys.begin(), t.keys.end(), key.tick, [](const Key& k, int tick) { return k.tick < tick; });
		if (it != t.keys.end() && it->tick == key.tick)
			*it = key;
		else
			t.keys.insert(it, key);
	}

	void PreviewAnimation::sort()
	{
		for (Track& t : tracks)
		{
			std::stable_sort(t.keys.begin(), t.keys.end(), [](const Key& a, const Key& b) { return a.tick < b.tick; });
			// one key per tick: the later one wins
			std::vector<Key> unique;
			for (const Key& k : t.keys)
			{
				if (!unique.empty() && unique.back().tick == k.tick)
					unique.back() = k;
				else
					unique.push_back(k);
			}
			t.keys = std::move(unique);
		}
		tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.keys.empty(); }), tracks.end());
		std::stable_sort(effects.begin(), effects.end(), [](const Effect& a, const Effect& b) { return a.tick < b.tick; });
	}

	bool Transform::identity() const
	{
		return x == 0 && y == 0 && scale == 1 && rotation == 0 && opacity == 1;
	}

	Transform evaluate(const PreviewAnimation& anim, const std::string& target, int tick)
	{
		const Track* t = anim.track(target);
		if (!t || t->keys.empty())
			return {};
		const auto& keys = t->keys;
		auto pack = [](const Key& k) { return Transform{ k.x, k.y, k.scale, k.rotation, k.opacity }; };
		// before the first key and after the last the track holds still
		if (tick <= keys.front().tick)
			return pack(keys.front());
		if (tick >= keys.back().tick)
			return pack(keys.back());
		auto next = std::upper_bound(keys.begin(), keys.end(), tick, [](int tick, const Key& k) { return tick < k.tick; });
		const Key& b = *next;
		const Key& a = *(next - 1);
		const float u = ease(b.ease, static_cast<float>(tick - a.tick) / static_cast<float>(std::max(1, b.tick - a.tick)));
		return { lerp(a.x, b.x, u), lerp(a.y, b.y, u), lerp(a.scale, b.scale, u), lerp(a.rotation, b.rotation, u),
			std::clamp(lerp(a.opacity, b.opacity, u), 0.0f, 1.0f) };
	}

	EffectState effectsAt(const PreviewAnimation& anim, double seconds, const TickToSeconds& toSeconds)
	{
		EffectState state;
		float fadeLevel = 0.0f;
		uint32_t fadeColor = 0xff000000u;
		float flashLevel = 0.0f;
		uint32_t flashColor = 0xffffffffu;
		for (const Effect& e : anim.effects)
		{
			const double start = toSeconds(e.tick);
			if (seconds < start)
				break;		// sorted by tick
			const float duration = std::max(0.01f, e.duration);
			const float u = static_cast<float>((seconds - start) / duration);
			switch (e.kind)
			{
			case EffectKind::Shake:
				if (u < 1.0f)
				{
					// fades out as it goes; two speeds so it does not just slide on a diagonal
					const float amp = e.strength * (1.0f - u) * (1.0f - u);
					const float t = static_cast<float>(seconds - start);
					state.shakeX += amp * std::sin(2 * PI * e.speed * t + 0.7f);
					state.shakeY += amp * std::sin(2 * PI * e.speed * 1.37f * t + 2.1f);
				}
				break;
			case EffectKind::Flash:
				if (u < 1.0f)
				{
					// snaps on, eases off
					const float level = (1.0f - u) * (1.0f - u);
					if (level > flashLevel)
					{
						flashLevel = level;
						flashColor = e.color;
					}
				}
				break;
			default:
			{
				// a fade goes from wherever the last one left off to its own level, then stays there
				const float target = std::clamp(e.strength, 0.0f, 1.0f);
				const float from = fadeLevel;
				fadeLevel = lerp(from, target, ease(Ease::InOut, u));
				fadeColor = e.color;
				break;
			}
			}
		}
		if (fadeLevel > 0.001f)
			state.fade = withAlpha(fadeColor, fadeLevel);
		if (flashLevel > 0.001f)
			state.flash = withAlpha(flashColor, flashLevel);
		return state;
	}

	Transform screenAt(const PreviewAnimation& anim, int tick, double seconds, const TickToSeconds& toSeconds)
	{
		Transform t = evaluate(anim, "screen", tick);
		if (!anim.effects.empty())
		{
			const EffectState fx = effectsAt(anim, seconds, toSeconds);
			t.x += fx.shakeX;
			t.y += fx.shakeY;
		}
		return t;
	}

	std::string toJson(const PreviewAnimation& anim)
	{
		nlohmann::json tracks = nlohmann::json::array();
		for (const Track& t : anim.tracks)
		{
			if (t.keys.empty())
				continue;
			nlohmann::json keys = nlohmann::json::array();
			for (const Key& k : t.keys)
				keys.push_back({ { "tick", k.tick }, { "x", k.x }, { "y", k.y }, { "scale", k.scale }, { "rotation", k.rotation },
					{ "opacity", k.opacity }, { "ease", static_cast<int>(k.ease) } });
			tracks.push_back({ { "target", t.target }, { "keys", keys } });
		}
		nlohmann::json effects = nlohmann::json::array();
		for (const Effect& e : anim.effects)
			effects.push_back({ { "kind", static_cast<int>(e.kind) }, { "tick", e.tick }, { "duration", e.duration }, { "strength", e.strength },
				{ "speed", e.speed }, { "color", e.color } });
		return nlohmann::json{ { "version", 1 }, { "tracks", tracks }, { "effects", effects } }.dump();
	}

	bool fromJson(const std::string& text, PreviewAnimation& anim)
	{
		const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
		if (!root.is_object())
			return false;
		auto num = [](const nlohmann::json& o, const char* key, float fallback) {
			return o.contains(key) && o[key].is_number() ? o[key].get<float>() : fallback;
		};
		auto integer = [](const nlohmann::json& o, const char* key, int fallback) {
			return o.contains(key) && o[key].is_number_integer() ? o[key].get<int>() : fallback;
		};
		anim = {};
		if (root.contains("tracks") && root["tracks"].is_array())
			for (const auto& t : root["tracks"])
			{
				if (!t.is_object() || !t.contains("target") || !t["target"].is_string() || !t.contains("keys") || !t["keys"].is_array())
					continue;
				Track track{ t["target"].get<std::string>(), {} };
				for (const auto& k : t["keys"])
				{
					if (!k.is_object())
						continue;
					Key key;
					key.tick = integer(k, "tick", 0);
					key.x = num(k, "x", 0);
					key.y = num(k, "y", 0);
					key.scale = num(k, "scale", 1);
					key.rotation = num(k, "rotation", 0);
					key.opacity = std::clamp(num(k, "opacity", 1), 0.0f, 1.0f);
					key.ease = static_cast<Ease>(std::clamp(integer(k, "ease", static_cast<int>(Ease::InOut)), 0, static_cast<int>(Ease::Count) - 1));
					track.keys.push_back(key);
				}
				anim.tracks.push_back(std::move(track));
			}
		if (root.contains("effects") && root["effects"].is_array())
			for (const auto& e : root["effects"])
			{
				if (!e.is_object())
					continue;
				Effect fx;
				fx.kind = static_cast<EffectKind>(std::clamp(integer(e, "kind", 0), 0, static_cast<int>(EffectKind::Count) - 1));
				fx.tick = integer(e, "tick", 0);
				fx.duration = std::max(0.01f, num(e, "duration", 0.5f));
				fx.strength = num(e, "strength", 14.0f);
				fx.speed = num(e, "speed", 28.0f);
				fx.color = e.contains("color") && e["color"].is_number_unsigned() ? e["color"].get<uint32_t>() : 0xffffffffu;
				anim.effects.push_back(fx);
			}
		anim.sort();
		return true;
	}
}
