#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/PreviewAnimation.h"
#include "NativeScoreSerializer.h"
#include "SusSerializer.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace fs = std::filesystem;
namespace Anim = Canvas::Animation;

namespace
{
	std::string temp(const std::string& name) { return (fs::temp_directory_path() / ("canvas_test_" + name)).u8string(); }
	bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

	Anim::PreviewAnimation sample()
	{
		Anim::PreviewAnimation anim;
		Anim::Key a, b;
		a.tick = 0;
		b.tick = 1920;
		b.x = 300;
		b.rotation = 90;
		b.opacity = 0.5f;
		b.ease = Anim::Ease::Linear;
		anim.setKey("score", b);	// out of order on purpose
		anim.setKey("score", a);
		Anim::Effect shake;
		shake.tick = 960;
		shake.duration = 1.0f;
		anim.effects.push_back(shake);
		Anim::Effect fade;
		fade.kind = Anim::EffectKind::Fade;
		fade.tick = 0;
		fade.duration = 1.0f;
		fade.strength = 1.0f;
		fade.color = 0xff000000u;
		anim.effects.push_back(fade);
		anim.sort();
		return anim;
	}
}

TEST(animation_keys_interpolate_and_hold)
{
	const auto anim = sample();
	CHECK(Anim::evaluate(anim, "score", -100).identity());
	const auto mid = Anim::evaluate(anim, "score", 960);
	CHECK(near(mid.x, 150.0f));
	CHECK(near(mid.rotation, 45.0f));
	CHECK(near(mid.opacity, 0.75f));
	// past the last key it stays put
	CHECK(near(Anim::evaluate(anim, "score", 99999).x, 300.0f));
	// a part without a track never moves
	CHECK(Anim::evaluate(anim, "life", 960).identity());
}

TEST(animation_set_key_replaces_same_tick)
{
	auto anim = sample();
	Anim::Key k;
	k.tick = 1920;
	k.x = -50;
	anim.setKey("score", k);
	CHECK_EQ(anim.track("score")->keys.size(), static_cast<size_t>(2));
	CHECK(near(Anim::evaluate(anim, "score", 1920).x, -50.0f));
}

TEST(animation_eases_hit_their_ends)
{
	for (int i = 0; i < static_cast<int>(Anim::Ease::Count); ++i)
	{
		const auto e = static_cast<Anim::Ease>(i);
		CHECK(near(Anim::ease(e, 1.0f), 1.0f));
		if (e != Anim::Ease::Step)
			CHECK(near(Anim::ease(e, 0.0f), 0.0f));
	}
}

TEST(animation_effects_fade_and_shake)
{
	const auto anim = sample();
	// one second per 1920 ticks keeps the numbers easy
	auto toSeconds = [](int tick) { return tick / 1920.0; };
	const auto start = Anim::effectsAt(anim, 0.0, toSeconds);
	CHECK_EQ(start.fade >> 24, 0u);
	const auto done = Anim::effectsAt(anim, 2.0, toSeconds);
	CHECK_EQ(done.fade, 0xff000000u);		// the fade holds once it is there
	const auto shaking = Anim::effectsAt(anim, 0.6, toSeconds);
	CHECK(shaking.shakeX != 0.0f || shaking.shakeY != 0.0f);
	const auto calm = Anim::effectsAt(anim, 1.6, toSeconds);
	CHECK(calm.shakeX == 0.0f && calm.shakeY == 0.0f);
}

TEST(animation_round_trips_through_json_and_ce)
{
	const auto anim = sample();
	Anim::PreviewAnimation back;
	CHECK(Anim::fromJson(Anim::toJson(anim), back));
	CHECK_EQ(Anim::toJson(back), Anim::toJson(anim));
	CHECK(!Anim::fromJson("not json", back));

	Score score = richChart();
	score.animation = anim;
	const std::string path = temp("anim.ce");
	NativeScoreSerializer().serialize(score, path);
	const Score loaded = NativeScoreSerializer().deserialize(path);
	CHECK_EQ(Anim::toJson(loaded.animation), Anim::toJson(anim));
	fs::remove(path);

	// CANVAS_ANIM_SAMPLE=<in.sus>|<out.ce> writes a demo chart for screenshots
	if (const char* spec = std::getenv("CANVAS_ANIM_SAMPLE"))
	{
		const std::string s = spec;
		const size_t bar = s.find('|');
		if (bar != std::string::npos)
		{
			Score demo = SusSerializer().deserialize(s.substr(0, bar));
			Anim::PreviewAnimation a;
			Anim::Key k0, k1;
			k0.tick = 0;
			k1.tick = 1920 * 2;
			k1.rotation = -12;
			k1.scale = 0.8f;
			k1.x = -200;
			a.setKey("stage", k0);
			a.setKey("stage", k1);
			Anim::Key s0, s1;
			s1.tick = 1920 * 2;
			s1.y = 120;
			s1.rotation = 20;
			s1.opacity = 0.4f;
			a.setKey("score", s0);
			a.setKey("score", s1);
			Anim::Effect flash;
			flash.kind = Anim::EffectKind::Flash;
			flash.tick = 1920 * 2;
			flash.duration = 2.0f;
			flash.color = 0xffd0a0ffu;
			a.effects.push_back(flash);
			a.sort();
			demo.animation = a;
			NativeScoreSerializer().serialize(demo, s.substr(bar + 1));
		}
	}
}
