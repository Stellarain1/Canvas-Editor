#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/StageEdit.h"
#include <cmath>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace St = Canvas::Stages;

namespace
{
	bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

	St::StageSet movingStage(int& side)
	{
		St::StageSet set;
		set.enabled = true;
		side = set.addStage("Side");
		St::TransformEvent a, b;
		b.tick = 960;
		b.rotate = 90;
		b.x = 4;
		set.setTransform(side, a);
		set.setTransform(side, b);
		St::MaskEvent m0, m1;
		m1.tick = 960;
		m1.size = 2;
		set.setMask(side, m0);
		set.setMask(side, m1);
		return set;
	}
}

TEST(stages_ui_joint_at_cursor_does_not_jump)
{
	int side = 0;
	St::StageSet set = movingStage(side);
	const int tick = 480;
	const St::StageState before = St::evaluate(set, side, tick);

	CHECK(St::insertTransformAt(set, side, tick));
	CHECK(St::insertMaskAt(set, side, tick));
	CHECK(St::insertPivotAt(set, side, tick));
	CHECK(St::insertStyleAt(set, side, tick));
	CHECK(!St::insertMaskAt(set, 77, tick));

	const St::StageState after = St::evaluate(set, side, tick);
	CHECK(near(before.rotate, after.rotate));
	CHECK(near(before.x, after.x));
	CHECK(near(before.maskHalfWidth, after.maskHalfWidth));
	CHECK(near(before.laneAlpha, after.laneAlpha));
	// and a little before / after it the picture is still the same curve end to end
	CHECK(near(St::evaluate(set, side, 960).rotate, 90.0f));
	CHECK_EQ(set.find(side)->transform.size(), size_t(3));

	// pressing it twice on one tick replaces the joint instead of stacking
	St::insertTransformAt(set, side, tick);
	CHECK_EQ(set.find(side)->transform.size(), size_t(3));
}

TEST(stages_ui_camera_joint_at_cursor_does_not_jump)
{
	St::StageSet set;
	St::CameraEvent a, b;
	b.tick = 960;
	b.zoom = 2;
	b.rotate = 20;
	set.setCamera(a);
	set.setCamera(b);
	const St::CameraState before = St::camera(set, 240);
	St::insertCameraAt(set, 240);
	const St::CameraState after = St::camera(set, 240);
	CHECK(near(before.zoom, after.zoom));
	CHECK(near(before.rotate, after.rotate));
	CHECK_EQ(set.camera.size(), size_t(3));
}

TEST(stages_ui_assign_takes_the_whole_hold)
{
	resetNextID();
	Score score;
	const int tap = addTap(score, 0, 0, 2).ID;
	HoldNote& hold = addHold(score, { 480, 4, 2 }, { { 960, 5, 2 } }, { 1440, 6, 2 });
	const int start = hold.start.ID, mid = hold.steps[0].ID, end = hold.end;
	score.stages.enabled = true;
	const int side = score.stages.addStage("Side");

	// selecting only the middle step still moves the whole hold
	St::assignSelection(score, { mid }, side);
	CHECK_EQ(score.stages.stageOf(start), side);
	CHECK_EQ(score.stages.stageOf(mid), side);
	CHECK_EQ(score.stages.stageOf(end), side);
	CHECK_EQ(score.stages.stageOf(tap), 0);

	// and a tap on its own moves alone
	St::assignSelection(score, { tap }, side);
	CHECK_EQ(score.stages.stageOf(tap), side);
	CHECK_EQ(St::notesOfStage(score, side).size(), size_t(4));
	CHECK_EQ(St::notesOfStage(score, 0).size(), size_t(0));

	// back to the main stage by assigning 0
	St::assignSelection(score, { end }, 0);
	CHECK_EQ(St::notesOfStage(score, side).size(), size_t(1));

	// unknown ids are ignored
	CHECK_EQ(St::withHoldFamilies(score, { 9999 }).size(), size_t(0));
}

TEST(stages_ui_prune_after_delete)
{
	resetNextID();
	Score score;
	const int a = addTap(score, 0, 0, 2).ID;
	const int b = addTap(score, 480, 2, 2).ID;
	score.stages.enabled = true;
	const int side = score.stages.addStage("Side");
	score.stages.assign({ a, b }, side);

	score.notes.erase(a);
	score.stages.prune(score);
	CHECK_EQ(score.stages.noteStage.size(), size_t(1));
	CHECK_EQ(score.stages.stageOf(b), side);
	CHECK_EQ(score.stages.stageOf(a), 0);
}

TEST(stages_ui_new_notes_join_the_current_stage)
{
	resetNextID();
	Score score;
	const int a = addTap(score, 0, 0, 2).ID;
	const int side = score.stages.addStage("Side");

	St::currentStage = side;
	St::adoptNewNotes(score, { a });
	CHECK_EQ(score.stages.stageOf(a), 0);		// stages are off: nothing happens

	score.stages.enabled = true;
	St::adoptNewNotes(score, { a });
	CHECK_EQ(score.stages.stageOf(a), side);

	// a step follows its hold, whatever stage is current
	const int b = addTap(score, 480, 0, 2).ID;
	const int c = addTap(score, 960, 0, 2).ID;
	St::currentStage = 0;
	St::adoptNewNotes(score, { c }, a);
	CHECK_EQ(score.stages.stageOf(c), side);
	St::adoptNewNotes(score, { b });			// main stage current: stays unassigned
	CHECK_EQ(score.stages.stageOf(b), 0);

	St::currentStage = 55;						// a stage that does not exist
	St::adoptNewNotes(score, { b });
	CHECK_EQ(score.stages.stageOf(b), 0);
	St::currentStage = 0;
}

TEST(stages_ui_demo_fill)
{
	resetNextID();
	Score score = richChart();
	St::fillDemo(score);
	CHECK(score.stages.enabled);
	CHECK(score.stages.stages.size() >= 2);
	CHECK(!score.stages.stages.back().transform.empty());
	CHECK(!score.stages.stages.back().mask.empty());
	CHECK(!score.stages.noteStage.empty());
	St::currentStage = 0;
}
