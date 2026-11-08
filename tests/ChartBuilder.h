#pragma once
// Helpers for building small charts in tests.
#include "Score.h"
#include <initializer_list>

namespace CanvasTest
{
	using namespace MikuMikuWorld;

	inline Note& addTap(Score& score, int tick, int lane, int width, bool critical = false,
		FlickType flick = FlickType::None, bool friction = false, int layer = 0)
	{
		Note note(NoteType::Tap, tick, lane, width);
		note.ID = nextID++;
		note.critical = critical;
		note.flick = flick;
		note.friction = friction;
		note.layer = layer;
		score.notes[note.ID] = note;
		return score.notes[note.ID];
	}

	struct StepSpec
	{
		int tick, lane, width;
		HoldStepType type{ HoldStepType::Normal };
		EaseType ease{ EaseType::Linear };
	};

	// start/end are {tick, lane, width}; the start's ease is `startEase`.
	inline HoldNote& addHold(Score& score, StepSpec start, std::initializer_list<StepSpec> steps, StepSpec end,
		HoldNoteType kind = HoldNoteType::Normal, bool critical = false, int layer = 0)
	{
		HoldNote hold;
		Note head(NoteType::Hold, start.tick, start.lane, start.width);
		head.ID = nextID++;
		head.critical = critical;
		head.layer = layer;
		score.notes[head.ID] = head;
		hold.start = { head.ID, HoldStepType::Normal, start.ease };
		hold.startType = kind;
		hold.endType = kind;

		for (const auto& spec : steps)
		{
			Note mid(NoteType::HoldMid, spec.tick, spec.lane, spec.width);
			mid.ID = nextID++;
			mid.parentID = head.ID;
			mid.critical = critical;
			mid.layer = layer;
			score.notes[mid.ID] = mid;
			hold.steps.push_back({ mid.ID, spec.type, spec.ease });
		}

		Note tail(NoteType::HoldEnd, end.tick, end.lane, end.width);
		tail.ID = nextID++;
		tail.parentID = head.ID;
		tail.critical = critical;
		tail.layer = layer;
		score.notes[tail.ID] = tail;
		hold.end = tail.ID;

		score.holdNotes[head.ID] = hold;
		return score.holdNotes[head.ID];
	}

	inline HoldNote& addGuide(Score& score, StepSpec start, StepSpec end, GuideColor color, FadeType fade, int layer = 0)
	{
		HoldNote& guide = addHold(score, start, {}, end, HoldNoteType::Guide, isYellowFamilyGuide(color), layer);
		guide.guideColor = color;
		guide.fadeType = fade;
		return guide;
	}

	// A chart that uses most of what the editor can express.
	inline Score richChart()
	{
		resetNextID();
		Score score;
		score.metadata.title = "Canvas test chart";
		score.metadata.artist = "Canvas";
		score.metadata.author = "canvas_tests";
		score.metadata.musicOffset = 25.0f;
		score.tempoChanges = { Tempo(0, 120.0f), Tempo(1920 * 8, 150.0f) };

		score.layers.push_back({ "fast", false });
		score.hiSpeedChanges.push_back({ 960, 1.5f, 0 });
		score.hiSpeedChanges.push_back({ 0, 2.0f, 1 });

		addTap(score, 1920, 2, 3);
		addTap(score, 2400, 6, 2, true);
		addTap(score, 2880, 0, 4, false, FlickType::Left);
		addTap(score, 3360, 8, 3, false, FlickType::None, true);
		addTap(score, 3600, 4, 4, true, FlickType::Default);
		addTap(score, 4000, 0, 3, false, FlickType::None, false, 1);

		addHold(score, { 3840, 2, 3, HoldStepType::Normal, EaseType::EaseIn },
			{ { 4320, 4, 3, HoldStepType::Normal, EaseType::Linear },
			  { 4800, 5, 3, HoldStepType::Hidden, EaseType::EaseOut },
			  { 5040, 6, 3, HoldStepType::Skip, EaseType::Linear } },
			{ 5280, 7, 3 });

		addHold(score, { 5760, 0, 4, HoldStepType::Normal, EaseType::EaseInOut }, {}, { 6720, 8, 4 },
			HoldNoteType::Normal, true);

		addHold(score, { 7200, 8, 4, HoldStepType::Normal, EaseType::EaseOutIn }, {}, { 8160, 0, 4 });

		addGuide(score, { 8640, 2, 4, HoldStepType::Normal, EaseType::Linear }, { 9600, 6, 4 }, GuideColor::Blue, FadeType::In);
		addGuide(score, { 9600, 0, 2 }, { 10560, 10, 2 }, GuideColor::Yellow, FadeType::Out);

		// after the tempo change
		for (int i = 0; i < 16; ++i)
			addTap(score, 1920 * 8 + i * 240, (i * 3) % 10, 2);

		return score;
	}
}
