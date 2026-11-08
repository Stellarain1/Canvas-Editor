#pragma once
#include "../../Score.h"
#include <functional>
#include <string>
#include <vector>

// Auto Fix Chart Issues: one pass over everything the Issue List can flag that has a sensible
// automatic answer. Runs on a copy of the chart (the editor does it on a worker thread) and
// reports what it changed; the editor applies the result as a single undo step.
//
// What each group does:
//   Overlaps    identical taps merge, taps that cover another note move aside (fixOverlaps)
//   Holds       broken / orphaned holds go, steps outside a hold or on its ends go, a hold that ends
//               before it starts is flipped (or given a beat), notes are pulled into the 12 lanes
//   Hi-speed    unusable changes (not a number, before the start, on a missing layer, duplicates)
//               are cleaned up; with `aggressiveHiSpeed` also: negative speeds are made positive,
//               an extreme speed left running for a measure is scaled back (a 100 typed for 1.0),
//               and crawls / stops that draw two different notes on the same spot are sped up
//   Skills      duplicate / outside / too-close skills are moved, missing ones come from the
//               official layout; FEVER chance / start are placed and put in order
//   Timing      invalid or duplicate BPMs, no BPM at tick 0, invalid time signatures
namespace Canvas::Fixes
{
	enum AutoFixGroup
	{
		FixOverlaps = 1 << 0,
		FixHolds = 1 << 1,
		FixHiSpeed = 1 << 2,
		FixSkillFever = 1 << 3,
		FixTiming = 1 << 4,
		FixAll = 0x1f
	};

	struct AutoFixOptions
	{
		int groups{ FixAll };
		bool aggressiveHiSpeed{ false };
		double musicDurationSeconds{ -1.0 };	// for the "notes after the music" check
	};

	struct AutoFixReport
	{
		// changes made per group
		int overlaps{}, holds{}, hiSpeed{}, skillFever{}, timing{};
		// issues (errors + warnings) the Issue List finds before and after
		int before{}, after{};
		std::vector<std::string> log;

		int total() const { return overlaps + holds + hiSpeed + skillFever + timing; }
		bool changed() const { return total() > 0; }
	};

	// Group (AutoFixGroup bit) an Issue List code belongs to; 0 for codes Auto Fix does not touch.
	int groupOfCode(const std::string& code);

	// How many issues each group could act on right now (index by bit position, 0..4).
	struct AutoFixPreview
	{
		int perGroup[5]{};
		int errors{}, warnings{}, infos{};
		int fixable() const { int n = 0; for (int v : perGroup) n += v; return n; }
	};
	AutoFixPreview previewAutoFix(const MikuMikuWorld::Score& score, double musicDurationSeconds = -1.0);

	// `stage` is called with a short label before each group runs (from the calling thread).
	AutoFixReport autoFix(MikuMikuWorld::Score& score, const AutoFixOptions& options, const std::function<void(const std::string& stage)>& stage = {});
}
