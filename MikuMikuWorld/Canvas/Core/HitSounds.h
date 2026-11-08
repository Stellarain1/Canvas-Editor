#pragma once
#include "../../Score.h"
#include <string>
#include <vector>

// When each hit sound plays, for rendering the audio of an exported video. Mirrors what the
// editor plays during playback (ScoreEditorTimeline::updateNoteSE): one-shot sounds for taps,
// flicks, traces, visible hold steps and hold ends, plus the hold loop from start to end.
namespace Canvas
{
	struct HitSound
	{
		double time{};			// seconds from the start of the chart
		std::string name;		// SE name, e.g. "se_live_perfect"
		float volume{ 1.0f };	// from the chart's SE volume changes
		double loopUntil{ -1 };	// > time for looping sounds (hold connect)
	};

	std::vector<HitSound> scheduleHitSounds(const MikuMikuWorld::Score& score);
}
