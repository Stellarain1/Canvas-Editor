#include "HitSounds.h"
#include "../../Tempo.h"
#include "../../Constants.h"
#include <algorithm>
#include <cmath>

using namespace MikuMikuWorld;

namespace Canvas
{
	namespace
	{
		float seVolumeAt(int tick, const std::vector<SEVolumeChange>& changes)
		{
			float volume = 1.0f;
			int best = -1;
			for (const auto& change : changes)
			{
				if (change.tick <= tick && change.tick >= best)
				{
					best = change.tick;
					volume = change.volume;
				}
			}
			return std::max(0.0f, volume);
		}
	}

	std::vector<HitSound> scheduleHitSounds(const Score& score)
	{
		std::vector<HitSound> sounds;
		auto timeOf = [&](int tick) { return static_cast<double>(accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges)); };

		for (const auto& [id, note] : score.notes)
		{
			bool play = true;
			const HoldNote* hold = nullptr;
			if (note.getType() != NoteType::Tap)
			{
				const int holdId = note.getType() == NoteType::Hold ? note.ID : note.parentID;
				auto it = score.holdNotes.find(holdId);
				if (it == score.holdNotes.end())
					continue;
				hold = &it->second;
				if (hold->isGuide())
					continue;
			}

			if (note.getType() == NoteType::Hold)
				play = hold->startType == HoldNoteType::Normal;
			else if (note.getType() == NoteType::HoldEnd)
				play = hold->endType == HoldNoteType::Normal;

			const float volume = seVolumeAt(note.tick, score.seVolumeChanges);
			if (play)
			{
				const std::string_view se = getNoteSE(note, score);
				if (!se.empty())
					sounds.push_back({ timeOf(note.tick), std::string(se), volume });
			}

			if (note.getType() == NoteType::Hold)
			{
				auto end = score.notes.find(hold->end);
				if (end != score.notes.end() && end->second.tick > note.tick)
					sounds.push_back({ timeOf(note.tick), note.critical ? SE_CRITICAL_CONNECT : SE_CONNECT, volume, timeOf(end->second.tick) });
			}
		}

		// the editor never plays the same sound twice on one tick
		std::sort(sounds.begin(), sounds.end(), [](const HitSound& a, const HitSound& b)
		{
			return a.time != b.time ? a.time < b.time : a.name < b.name;
		});
		sounds.erase(std::unique(sounds.begin(), sounds.end(), [](const HitSound& a, const HitSound& b)
		{
			return a.loopUntil < 0 && b.loopUntil < 0 && a.name == b.name && std::abs(a.time - b.time) < 1e-6;
		}), sounds.end());
		return sounds;
	}
}
