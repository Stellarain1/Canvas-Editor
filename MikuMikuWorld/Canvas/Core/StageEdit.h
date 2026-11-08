#pragma once
#include "DynamicStages.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

// The editing side of Dynamic Stages that has no UI in it (so the tests can poke it): which stage new
// notes go to, grabbing a hold's whole note family, and "add a joint right here without a jump".
namespace Canvas::Stages
{
	// The stage new notes are placed on (only matters while stages are enabled). UI state, never saved.
	inline int currentStage = 0;

	// a steady colour per stage (0xAABBGGRR, ImGui's packing)
	uint32_t stageColor(int stageId);

	// Every note id a stage assignment has to cover for these selected ids: a hold start brings its steps
	// and end, a step or an end brings the whole hold. Ids the score does not have are dropped.
	std::vector<int> withHoldFamilies(const MikuMikuWorld::Score& score, const std::unordered_set<int>& selected);

	// puts the selection (hold families included) on a stage
	void assignSelection(MikuMikuWorld::Score& score, const std::unordered_set<int>& selected, int stageId);

	// every note id on the stage (stage 0 = the ones not assigned anywhere)
	std::vector<int> notesOfStage(const MikuMikuWorld::Score& score, int stageId);

	// New notes just appeared in the chart (insert / paste): when stages are on and a non-main stage is
	// current, they join it. `likeNote` >= 0 copies that note's stage instead (a new hold step follows its hold).
	void adoptNewNotes(MikuMikuWorld::Score& score, const std::vector<int>& ids, int likeNote = -1);

	// A joint at `tick` starting from what the stage looks like there right now, so adding one never makes
	// anything jump. Returns false for an unknown stage.
	bool insertMaskAt(StageSet& set, int stageId, int tick);
	bool insertPivotAt(StageSet& set, int stageId, int tick);
	bool insertStyleAt(StageSet& set, int stageId, int tick);
	bool insertTransformAt(StageSet& set, int stageId, int tick);
	void insertCameraAt(StageSet& set, int tick);

	// two example stages ("Main", "Side") with a move / rotate and a mask joint, for screenshots and demos;
	// the notes alternate between them. Turns the set on.
	void fillDemo(MikuMikuWorld::Score& score);
}
