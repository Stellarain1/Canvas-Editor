#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/HitSounds.h"
#include <algorithm>
#include <cmath>

using namespace MikuMikuWorld;
using namespace CanvasTest;

namespace
{
	int count(const std::vector<Canvas::HitSound>& sounds, const std::string& name)
	{
		return static_cast<int>(std::count_if(sounds.begin(), sounds.end(), [&](const Canvas::HitSound& s) { return s.name == name; }));
	}

	const Canvas::HitSound* find(const std::vector<Canvas::HitSound>& sounds, const std::string& name)
	{
		auto it = std::find_if(sounds.begin(), sounds.end(), [&](const Canvas::HitSound& s) { return s.name == name; });
		return it == sounds.end() ? nullptr : &*it;
	}
}

TEST(hit_sounds_follow_note_types)
{
	resetNextID();
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f) };	// 480 ticks = 0.5 s
	addTap(score, 960, 0, 3);
	addTap(score, 1920, 4, 3, true);
	addTap(score, 2880, 8, 3, false, FlickType::Default);
	addTap(score, 3840, 0, 3, false, FlickType::None, true);

	const auto sounds = Canvas::scheduleHitSounds(score);
	const auto* tap = find(sounds, SE_PERFECT);
	CHECK(tap != nullptr);
	if (tap)
		CHECK(std::abs(tap->time - 1.0) < 1e-6);
	CHECK_EQ(count(sounds, SE_CRITICAL_TAP), 1);
	CHECK_EQ(count(sounds, SE_FLICK), 1);
	CHECK_EQ(count(sounds, SE_FRICTION), 1);
	CHECK_EQ(sounds.size(), size_t(4));
}

TEST(hit_sounds_for_holds_and_guides)
{
	resetNextID();
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f) };
	addHold(score, { 0, 2, 3 },
		{ { 480, 3, 3, HoldStepType::Normal }, { 960, 4, 3, HoldStepType::Hidden }, { 1200, 4, 3, HoldStepType::Skip } },
		{ 1920, 5, 3 });
	addGuide(score, { 0, 8, 3 }, { 1920, 8, 3 }, GuideColor::Green, FadeType::Out);

	const auto sounds = Canvas::scheduleHitSounds(score);
	// head + visible step + end; hidden step and guide silent; skip steps tick
	CHECK_EQ(count(sounds, SE_PERFECT), 2);
	CHECK_EQ(count(sounds, SE_TICK), 2);
	const auto* loop = find(sounds, SE_CONNECT);
	CHECK(loop != nullptr);
	if (loop)
	{
		CHECK(std::abs(loop->time - 0.0) < 1e-6);
		CHECK(std::abs(loop->loopUntil - 2.0) < 1e-6);
	}
}

TEST(hit_sounds_use_se_volume_changes)
{
	resetNextID();
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f) };
	score.seVolumeChanges = { { 960, 0.25f } };
	addTap(score, 480, 0, 3);
	addTap(score, 1440, 0, 3);
	const auto sounds = Canvas::scheduleHitSounds(score);
	CHECK_EQ(sounds.size(), size_t(2));
	CHECK(std::abs(sounds[0].volume - 1.0f) < 1e-6f);
	CHECK(std::abs(sounds[1].volume - 0.25f) < 1e-6f);
}
