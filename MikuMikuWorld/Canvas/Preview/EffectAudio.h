#pragma once
#include "LiveOverlay.h"
#include <string>
#include <vector>

struct ma_engine;

namespace MikuMikuWorld
{
	struct Score;
}

// The sounds of the live overlay's effects: a skill going off, FEVER CHANCE, FEVER / SUPER FEVER
// and ALL PERFECT. They come from the asset pack (pjsekai-overlay-APPEND's SE_LIVE_SKILL.mp3 and
// the audio of its FEVER / ALL PERFECT clips, pulled out once with ffmpeg into the user's
// cache); without those Canvas plays its own short chimes. The preview plays them live, video
// export mixes them into the soundtrack.
namespace Canvas::Preview
{
	// Voice1..Voice5: the line of the deck's card in that slot (LiveCards), with its cut-in
	enum class EffectSound { Skill, FeverChance, Fever, SuperFever, AllPerfect, Cutin, Voice1, Voice2, Voice3, Voice4, Voice5, Count };

	inline constexpr int EFFECT_SAMPLE_RATE = 44100;

	// 44.1 kHz stereo float samples of `sound` (never empty for the effects: built-in chimes as the
	// fallback; a card without a voice is silent)
	const std::vector<float>& effectPcm(EffectSound sound);

	struct EffectCue
	{
		double time{};
		EffectSound sound{};
	};
	// What plays when for the chart, for the overlay options (FEVER needs the fever markers, SUPER
	// FEVER replaces FEVER in Cheerful Carnival, ALL PERFECT after the last note).
	std::vector<EffectCue> effectCues(const MikuMikuWorld::Score& score, const OverlayOptions& options);

	// Any audio file miniaudio reads, as 44.1 kHz stereo float (UTF-8 paths work on Windows).
	bool decodeAudioFile(const std::string& path, std::vector<float>& out);

	// Plays the cues as the preview's clock passes them.
	class EffectPlayer
	{
	public:
		~EffectPlayer();
		void update(ma_engine* engine, bool playing, double time, const std::vector<EffectCue>& cues, float volume);
		void stop();

	private:
		struct Voice;
		std::vector<Voice*> voices;
		double lastTime{ -1e9 };
		bool wasPlaying{ false };
		void play(ma_engine* engine, EffectSound sound, float volume, double offsetSeconds);
		void reap();
	};
}
