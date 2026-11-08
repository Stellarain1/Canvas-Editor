#pragma once
#include "../../Score.h"
#include <string>
#include <vector>

// Whole-chart rewrites used when exporting to formats that cannot represent everything the
// editor can (Project SEKAI's own .sus / MusicScoreData). They work on a copy of the score.
namespace Canvas
{
	// Replace every ease-in-out / ease-out-in segment with a hidden midpoint step: ease-in then
	// ease-out (or the reverse). For quadratic curves that is the same shape, exactly when the
	// midpoint falls on whole lanes and ticks and within half a lane/tick otherwise.
	// Returns how many segments were rewritten.
	int lowerExtendedEases(MikuMikuWorld::Score& score);

	struct LayerMergeReport
	{
		int notesMoved{};		// notes that were on a layer other than the default one
		int hiSpeedsDropped{};	// hi-speed changes that belonged to those layers
	};

	// Fold every note onto layer 0 and drop the other layers' hi-speed changes. This is what the
	// game does anyway: it only reads #TIL00.
	LayerMergeReport mergeLayersToDefault(MikuMikuWorld::Score& score);

	// Guides whose colour or fade the game cannot show (it has green/yellow and fade-out only)
	int countExtendedGuides(const MikuMikuWorld::Score& score);
	int countExtendedEases(const MikuMikuWorld::Score& score);
	int countNotesWithSpeedRatio(const MikuMikuWorld::Score& score);
	bool usesLayers(const MikuMikuWorld::Score& score);

	// Sort tempo / hi-speed / SE volume lists and drop exact duplicates (same tick and layer).
	void normalizeEvents(MikuMikuWorld::Score& score);

	// First and last tick that carries a playable note (taps, hold starts/ends/visible steps).
	// Returns false for an empty chart.
	bool playableTickRange(const MikuMikuWorld::Score& score, int& firstTick, int& lastTick);
}
