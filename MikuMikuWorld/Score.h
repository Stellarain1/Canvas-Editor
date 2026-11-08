#pragma once
#include "Note.h"
#include "Tempo.h"
#include "Canvas/Core/PreviewAnimation.h"
#include "Canvas/Core/DynamicStages.h"
#include <string>
#include <map>
#include <vector>

namespace MikuMikuWorld
{
	extern int nextSkillID;

	struct SkillTrigger
	{
		int ID;
		int tick;
	};

	// Encoded in .sus as #mmm1f:11 (fever chance) and #mmm1f:21 (fever start); -1 = not placed.
	struct Fever
	{
		int chanceTick{ -1 };
		int startTick{ -1 };
	};

	struct HiSpeedChange
	{
		int tick;
		float speed;
		int layer{ 0 };
	};

	// A hi-speed (soflan) layer, a.k.a. timescale group. Notes on a layer scroll
	// with that layer's hi-speed changes only.
	struct HiSpeedLayer
	{
		std::string name;
		bool hidden{ false };
	};

	struct SEVolumeChange
	{
		int tick;
		float volume;
	};

	struct ScoreMetadata
	{
		std::string title;
		std::string artist;
		std::string author;
		std::string musicFile;
		std::string jacketFile;
		float musicOffset;
		int musicId{};
		int endTick{ -1 };	// where the chart ends (playback stops, videos end); -1 = the song's end
	};

	struct Score
	{
		ScoreMetadata metadata;
		std::map<int, Note> notes;
		std::map<int, HoldNote> holdNotes;
		std::vector<Tempo> tempoChanges;
		std::map<int, TimeSignature> timeSignatures;
		std::vector<HiSpeedChange> hiSpeedChanges;
		std::vector<SEVolumeChange> seVolumeChanges;
		std::vector<SkillTrigger> skills;
		Fever fever;
		std::vector<HiSpeedLayer> layers{ HiSpeedLayer{ "default" } };
		Canvas::Animation::PreviewAnimation animation;	// Score Preview Animation (only .ce keeps it)
		Canvas::Stages::StageSet stages;				// Dynamic Stages (.ce and Next SEKAI LevelData keep it)

		Score();
	};

	// Hi-speed changes of one layer, sorted by tick.
	std::vector<HiSpeedChange> hiSpeedsOfLayer(const Score& score, int layer);

	// Hi-speed changes of every layer (index = layer), each sorted by tick. Always has
	// at least score.layers.size() entries.
	std::vector<std::vector<HiSpeedChange>> splitHiSpeedLayers(const Score& score);

	// Keep the guide's legacy critical flag in step with its colour (yellow family = critical),
	// which is what .sus / MusicScoreData / older MMWS readers understand.
	void syncGuideCritical(Score& score, HoldNote& hold);
}
