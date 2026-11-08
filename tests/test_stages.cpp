#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/DynamicStages.h"
#include <json.hpp>
#include <cmath>
#include <map>
#include <set>

using namespace MikuMikuWorld;
using namespace CanvasTest;
namespace St = Canvas::Stages;
using nlohmann::json;

namespace
{
	bool near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) < eps; }

	// two stages: a still one and one that slides + turns, a camera that zooms
	St::StageSet sampleSet()
	{
		St::StageSet set;
		set.enabled = true;
		const int second = set.addStage("Side");

		St::TransformEvent a, b;
		a.tick = 0;
		b.tick = 960;
		b.rotate = 90;
		b.x = 4;
		a.ease = St::EaseIn;
		set.setTransform(second, b);
		set.setTransform(second, a);	// out of order on purpose

		St::MaskEvent m0, m1;
		m0.tick = 0;
		m0.lane = 3;
		m0.size = 3;
		m1.tick = 960;
		m1.lane = 3;
		m1.size = 1.5f;
		m1.maskNotes = true;
		m0.ease = St::EaseOut;
		set.setMask(second, m0);
		set.setMask(second, m1);

		St::StyleEvent s0, s1;
		s0.tick = 0;
		s1.tick = 960;
		s1.laneAlpha = 0.5f;
		s1.judgeLineColor = 3;
		set.setStyle(second, s0);
		set.setStyle(second, s1);

		St::CameraEvent c0, c1;
		c0.tick = 0;
		c1.tick = 1920;
		c1.zoom = 2.0f;
		c1.rotate = 10;
		c1.stageTilt = 0.5f;
		c1.zoomVerticalAlign = 1;
		c0.ease = St::EaseNone;
		set.setCamera(c1);
		set.setCamera(c0);
		return set;
	}

	json parse(const std::string& text) { return json::parse(text); }

	// every note of one slide onto a stage
	void assignHold(St::StageSet& set, const HoldNote& hold, int stage)
	{
		set.assign(hold.start.ID, stage);
		for (const auto& step : hold.steps)
			set.assign(step.ID, stage);
		set.assign(hold.end, stage);
	}

	std::map<std::string, json> byName(const json& level)
	{
		std::map<std::string, json> out;
		for (const json& e : level["entities"])
			if (e.contains("name"))
				out[e["name"].get<std::string>()] = e;
		return out;
	}

	const json* dataOf(const json& entity, const char* name)
	{
		for (const json& d : entity["data"])
			if (d["name"] == name)
				return &d;
		return nullptr;
	}

	double valueOf(const json& entity, const char* name)
	{
		const json* d = dataOf(entity, name);
		return d && d->contains("value") ? (*d)["value"].get<double>() : -12345.0;
	}

	std::string refOf(const json& entity, const char* name)
	{
		const json* d = dataOf(entity, name);
		return d && d->contains("ref") ? (*d)["ref"].get<std::string>() : std::string();
	}

	int count(const json& level, const char* archetype)
	{
		int n = 0;
		for (const json& e : level["entities"])
			if (e["archetype"] == archetype)
				++n;
		return n;
	}
}

TEST(stages_easing_shapes)
{
	CHECK(near(St::ease(St::EaseLinear, 0.3f), 0.3f));
	CHECK(near(St::ease(St::EaseIn, 0.5f), 0.25f));
	CHECK(near(St::ease(St::EaseOut, 0.5f), 0.75f));
	CHECK(near(St::ease(St::EaseInOut, 0.25f), 0.125f));
	CHECK(near(St::ease(St::EaseOutIn, 0.25f), 0.375f));
	// none holds the first value until the next joint
	CHECK(near(St::ease(St::EaseNone, 0.9f), 0.0f));
	// every easing starts at 0 and ends at 1 (steps aside)
	for (int id = 1; id < 38; ++id)
	{
		CHECK(near(St::ease(id, 1.0f), 1.0f, 0.01f));
		CHECK(near(St::ease(id, 0.0f), 0.0f, 0.01f));
	}
	// out back overshoots past 1 on its way
	CHECK(St::ease(31, 0.5f) > 1.0f);
}

TEST(stages_evaluate_between_joints)
{
	St::StageSet set = sampleSet();
	set.sort();
	const int side = 1;

	// before / after the joints the values hold
	CHECK(near(St::evaluate(set, side, -50).rotate, 0.0f));
	CHECK(near(St::evaluate(set, side, 99999).rotate, 90.0f));

	// halfway, ease-in (quad): a quarter of the way
	const St::StageState mid = St::evaluate(set, side, 480);
	CHECK(near(mid.rotate, 22.5f));
	CHECK(near(mid.x, 1.0f));
	// mask uses its own ease (out): three quarters of the way from size 3 to 1.5
	CHECK(near(mid.maskHalfWidth, 3.0f - 1.5f * 0.75f));
	CHECK(mid.maskNotes == false);	// the first joint's flag until the next one is reached
	CHECK(St::evaluate(set, side, 960).maskNotes);
	// style: lane alpha, and the colour switches with a progress to cross-fade
	CHECK(near(mid.laneAlpha, 0.75f));
	CHECK(mid.judgeLineColor == 0 && mid.judgeLineColorTo == 3 && near(mid.styleProgress, 0.5f));

	// a joint exactly on the tick already counts
	CHECK(near(St::evaluate(set, side, 960).x, 4.0f));

	// a stage without joints keeps its base values; the default stage is plain
	const St::StageState plain = St::evaluate(set, 0, 700);
	CHECK(!plain.hasTransform());
	CHECK(near(plain.maskHalfWidth, 6.0f) && plain.visible);
	// an id that is not there is hidden
	CHECK(!St::evaluate(set, 77, 0).visible);

	// ticks to seconds: the same fraction of the time, so a tempo change moves the curve
	const St::TickToSeconds slowFirstHalf = [](double tick) { return tick <= 480 ? tick * 2.0 : 960 + (tick - 480); };
	// at tick 480 time is 960 of 1440 -> 2/3 through; ease-in quad = 4/9 of 90 degrees
	CHECK(near(St::evaluate(set, side, 480, slowFirstHalf).rotate, 90.0f * 4.0f / 9.0f, 0.01f));
}

TEST(stages_visibility_from_mask_joints)
{
	St::StageSet set;
	const int id = set.addStage("Pop in");
	set.find(id)->fromStart = false;
	set.find(id)->untilEnd = false;
	St::MaskEvent a, b;
	a.tick = 960;
	b.tick = 1920;
	set.setMask(id, a);
	set.setMask(id, b);
	CHECK(!St::evaluate(set, id, 959).visible);
	CHECK(St::evaluate(set, id, 960).visible);
	CHECK(St::evaluate(set, id, 1920).visible);
	CHECK(!St::evaluate(set, id, 1921).visible);
}

TEST(stages_camera_blends)
{
	St::StageSet set = sampleSet();
	set.sort();
	// camera joint 0 -> 1 has ease none: it holds until the second joint
	CHECK(near(St::camera(set, 960).zoom, 1.0f));
	CHECK(near(St::camera(set, 1920).zoom, 2.0f));

	set.camera[0].ease = St::EaseLinear;
	const St::CameraState half = St::camera(set, 960);
	CHECK(near(half.zoom, 1.5f));
	CHECK(near(half.rotate, 5.0f));
	CHECK(near(half.stageTilt, 0.75f));
	// no camera joints: the plain 12 lane view
	const St::CameraState none = St::camera(St::StageSet(), 100);
	CHECK(near(none.size, 6.0f) && near(none.zoom, 1.0f) && near(none.stageTilt, 1.0f));
}

TEST(stages_projection_matches_the_engine_layout)
{
	St::StageSet set;
	const St::StageState stage = St::evaluate(set, 0, 0);
	const St::CameraState cam = St::camera(set, 0);

	// judge line: middle of the picture, 79% down; lane 6 is 0.4435 of the width to the right
	const St::Point mid = St::projectLane(stage, cam, 0, 1);
	CHECK(near(mid.x, 0.5f) && near(mid.y, 0.7912f));
	const St::Point right = St::projectLane(stage, cam, 6, 1);
	CHECK(near(right.x, 0.8935f) && near(right.y, 0.7912f));
	// further up the highway: narrower and higher
	const St::Point far = St::projectLane(stage, cam, 6, 0.5f);
	CHECK(far.x < right.x && far.y < right.y);
	// the spawn depth is where notes start
	CHECK(near(St::approachDepth(0.0f), St::kSpawnDepth, 0.0005f));
	CHECK(near(St::approachDepth(1.0f), 1.0f));
	CHECK(near(St::approachDepth(1.0f, 0.4f), 1.0f));

	// a stage slid two lanes to the right
	St::StageState moved = stage;
	moved.x = 2;
	CHECK(near(St::projectLane(moved, cam, 0, 1).x, 0.5f + 2.0f * 0.23321f / 3.5556f));

	// a stage turned 90 degrees clockwise swings around its judge line centre: lane 0 stays, lane 3 goes down
	St::StageState turned = stage;
	turned.rotate = 90;
	const St::Point pivot = St::projectLane(turned, cam, 0, 1);
	CHECK(near(pivot.x, 0.5f) && near(pivot.y, 0.7912f));
	const St::Point swung = St::projectLane(turned, cam, 3, 1);
	CHECK(near(swung.x, 0.5f, 0.005f) && near(swung.y, 0.7912f + 3.0f * 0.23321f / 2.0f, 0.005f));

	// the camera zooming in makes the lanes wider
	St::CameraState zoomed = cam;
	zoomed.zoom = 2.0f;
	CHECK(St::projectLane(stage, zoomed, 3, 1).x > St::projectLane(stage, cam, 3, 1).x);

	// the prepared projection gives the same points
	const St::StageProjection prepared(turned, cam);
	CHECK(near(prepared.apply(3, 0.6f).x, St::projectLane(turned, cam, 3, 0.6f).x));
}

TEST(stages_json_round_trip)
{
	St::StageSet set = sampleSet();
	set.assign(11, 1);
	set.assign(12, 1);
	set.assign(13, 0);
	set.sort();
	const std::string text = St::toJson(set);

	St::StageSet back;
	CHECK(St::fromJson(text, back));
	CHECK_EQ(St::toJson(back), text);
	CHECK(back.enabled);
	CHECK_EQ(back.stages.size(), static_cast<size_t>(2));
	CHECK_EQ(back.stages[1].name, std::string("Side"));
	CHECK_EQ(back.stages[1].transform.size(), static_cast<size_t>(2));
	CHECK_EQ(back.camera.size(), static_cast<size_t>(2));
	CHECK_EQ(back.stageOf(11), 1);
	CHECK_EQ(back.stageOf(13), 0);
	CHECK_EQ(back.noteStage.size(), static_cast<size_t>(2));

	// junk and unknown keys are fine, a non object is not
	St::StageSet other;
	CHECK(!St::fromJson("[1,2", other));
	CHECK(St::fromJson("{\"stages\":[{\"id\":4,\"name\":\"x\",\"future\":1}],\"notes\":[[5,4],[6,9]]}", other));
	CHECK(other.find(0) != nullptr && other.find(4) != nullptr);
	CHECK_EQ(other.stageOf(5), 4);
	CHECK_EQ(other.stageOf(6), 0);	// stage 9 is not there
}

TEST(stages_set_bookkeeping)
{
	St::StageSet set;
	CHECK(set.empty());
	const int a = set.addStage();
	const int b = set.addStage();
	CHECK(a != b && a != 0);
	set.assign(7, a);
	set.assign(8, b);
	CHECK(!set.removeStage(0));
	CHECK(set.removeStage(a));
	CHECK_EQ(set.stageOf(7), 0);
	CHECK_EQ(set.stageOf(8), b);
	set.remapNotes({ { 8, 80 } });
	CHECK_EQ(set.stageOf(80), b);
	CHECK_EQ(set.stageOf(8), 0);

	Score score;
	set.prune(score);	// no such note
	CHECK(set.noteStage.empty());
}

TEST(stages_export_has_the_engine_archetypes)
{
	resetNextID();
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f) };
	Note& a = addTap(score, 480, 1, 3);
	Note& b = addTap(score, 960, 8, 3, true);
	addHold(score, { 1920, 2, 3 }, { { 2400, 4, 3, HoldStepType::Normal, EaseType::Linear } }, { 2880, 5, 3 });

	St::StageSet set = sampleSet();
	set.assign(b.ID, 1);
	assignHold(set, score.holdNotes.begin()->second, 1);
	(void)a;

	std::string text, error;
	std::vector<std::string> warnings;
	CHECK(St::exportLevelData(score, set, text, error, &warnings));
	CHECK_EQ(error, std::string());
	const json level = parse(text);
	const auto names = byName(level);

	// two stages, a camera chain of two, the side stage's three tracks of two (+ default stage base joints)
	CHECK_EQ(count(level, "Stage"), 2);
	CHECK_EQ(count(level, "CameraChange"), 2);
	CHECK_EQ(count(level, "StageTransformChange"), 2);	// the default stage's transform is flat: none written
	CHECK_EQ(count(level, "StageMaskChange"), 3);		// 2 on the side stage + 1 base joint on the default one
	CHECK_EQ(count(level, "StagePivotChange"), 2);		// a base joint each
	CHECK_EQ(count(level, "StageStyleChange"), 3);

	// Initialization hooks the camera in; the camera chain is linked by next
	const json& init = level["entities"][0];
	CHECK_EQ(init["archetype"].get<std::string>(), std::string("Initialization"));
	const std::string cam0 = refOf(init, "firstCamera");
	CHECK(names.count(cam0) == 1);
	const json& camEntity = names.at(cam0);
	CHECK(near(static_cast<float>(valueOf(camEntity, "zoom")), 1.0f));
	CHECK(!refOf(camEntity, "next").empty());
	const json& cam1 = names.at(refOf(camEntity, "next"));
	CHECK(near(static_cast<float>(valueOf(cam1, "zoom")), 2.0f));
	CHECK(near(static_cast<float>(valueOf(cam1, "#BEAT")), 4.0f));	// tick 1920
	CHECK(near(static_cast<float>(valueOf(cam1, "stageTilt")), 0.5f));
	CHECK(near(static_cast<float>(valueOf(cam1, "zoomVerticalAlign")), 1.0f));
	CHECK(near(static_cast<float>(valueOf(camEntity, "ease")), 0.0f));
	CHECK(refOf(cam1, "next").empty());
	// the camera keys the engine reads, exactly
	for (const char* key : { "#BEAT", "lane", "size", "zoom", "zoomTargetLane", "zoomTargetY", "zoomVerticalAlign", "rotate", "stageTilt", "ease" })
		CHECK(dataOf(camEntity, key) != nullptr);

	// the side stage: ids by order, stage1 has the move
	const json& side = names.at("stage1");
	CHECK_EQ(side["archetype"].get<std::string>(), std::string("Stage"));
	CHECK_EQ(refOf(side, "editorName"), std::string("Side"));
	for (const char* key : { "fromStart", "untilEnd", "generateSimLines" })
		CHECK(dataOf(side, key) != nullptr);
	const json& move0 = names.at(refOf(side, "firstTransformChange"));
	CHECK_EQ(move0["archetype"].get<std::string>(), std::string("StageTransformChange"));
	for (const char* key : { "#BEAT", "rotate", "xLaneTranslate", "yLaneTranslate", "elevation", "anchor", "ease" })
		CHECK(dataOf(move0, key) != nullptr);
	const json& move1 = names.at(refOf(move0, "next"));
	CHECK(near(static_cast<float>(valueOf(move1, "rotate")), 90.0f));
	CHECK(near(static_cast<float>(valueOf(move1, "xLaneTranslate")), 4.0f));
	CHECK(near(static_cast<float>(valueOf(move0, "ease")), 2.0f));	// joint 0 -> 1 eases in
	CHECK(near(static_cast<float>(valueOf(move1, "#BEAT")), 2.0f));	// tick 960
	// the stage with no transform joints does not get a first ref
	CHECK(refOf(names.at("stage0"), "firstTransformChange").empty());
	CHECK(!refOf(names.at("stage0"), "firstMaskChange").empty());
	const json& mask1 = names.at(refOf(names.at(refOf(side, "firstMaskChange")), "next"));
	CHECK(near(static_cast<float>(valueOf(mask1, "maskNotes")), 1.0f));
	CHECK(near(static_cast<float>(valueOf(mask1, "size")), 1.5f));
	const json& style1 = names.at(refOf(names.at(refOf(side, "firstStyleChange")), "next"));
	CHECK(near(static_cast<float>(valueOf(style1, "laneAlpha")), 0.5f));
	CHECK(near(static_cast<float>(valueOf(style1, "judgeLineColor")), 3.0f));
	for (const char* key : { "judgeLineStyle", "leftBorderStyle", "rightBorderStyle", "fullWidth", "noteAlpha", "judgeLineAlpha", "divisionLineAlpha", "ease" })
		CHECK(dataOf(style1, key) != nullptr);

	// every note entity is on a stage, with the right one; every ref in the file resolves (but editorName)
	std::map<int, int> perStage;
	int noteEntities = 0;
	for (const json& e : level["entities"])
	{
		const std::string arch = e["archetype"].get<std::string>();
		const bool isNote = arch.size() > 4 && arch.compare(arch.size() - 4, 4, "Note") == 0 && arch != "TransientHiddenTickNote";
		if (isNote)
		{
			++noteEntities;
			const std::string stage = refOf(e, "stage");
			CHECK(stage == "stage0" || stage == "stage1");
			++perStage[stage == "stage1" ? 1 : 0];
		}
		for (const json& d : e["data"])
			if (d.contains("ref") && d["name"] != "editorName")
				CHECK(names.count(d["ref"].get<std::string>()) == 1);
	}
	CHECK_EQ(noteEntities, 5);		// 2 taps + a hold with 3 points
	CHECK_EQ(perStage[1], 4);		// the second tap and the whole hold
	CHECK_EQ(perStage[0], 1);

	// bad stage assignment is an error, not a broken file
	set.noteStage[a.ID] = 42;
	std::string bad, why;
	CHECK(!St::exportLevelData(score, set, bad, why));
	CHECK(!why.empty());
}

TEST(stages_import_round_trip)
{
	resetNextID();
	Score score;
	score.tempoChanges = { Tempo(0, 120.0f) };
	addTap(score, 480, 1, 3);
	Note& b = addTap(score, 480, 1, 3, true);	// same spot on another stage on purpose
	addTap(score, 1440, 6, 2);
	addHold(score, { 1920, 2, 3 }, { { 2400, 4, 3 } }, { 2880, 5, 3 });

	St::StageSet set = sampleSet();
	set.assign(b.ID, 1);
	assignHold(set, score.holdNotes.begin()->second, 1);
	set.sort();

	std::string text, error;
	CHECK(St::exportLevelData(score, set, text, error));

	Score backScore;
	St::StageSet back;
	std::vector<std::string> warnings;
	CHECK(St::importLevelData(text, backScore, back, error, &warnings));
	CHECK_EQ(error, std::string());
	CHECK(back.enabled);
	CHECK_EQ(backScore.notes.size(), score.notes.size());
	CHECK_EQ(backScore.holdNotes.size(), static_cast<size_t>(1));
	CHECK_EQ(back.stages.size(), static_cast<size_t>(2));
	CHECK_EQ(back.stages[1].name, std::string("Side"));
	CHECK_EQ(back.camera.size(), static_cast<size_t>(2));
	CHECK(near(back.camera[1].zoom, 2.0f));
	CHECK_EQ(back.camera[1].tick, 1920);
	CHECK_EQ(back.camera[0].ease, 0);
	CHECK_EQ(back.camera[0].ease, set.camera[0].ease);
	CHECK_EQ(back.stages[1].transform.size(), static_cast<size_t>(2));
	CHECK(near(back.stages[1].transform[1].rotate, 90.0f));
	CHECK(near(back.stages[1].transform[1].x, 4.0f));
	CHECK_EQ(back.stages[1].mask.size(), static_cast<size_t>(2));
	CHECK(back.stages[1].mask[1].maskNotes);
	CHECK(near(back.stages[1].style[1].laneAlpha, 0.5f));

	// the two notes on one spot kept their own stages: the plain one on 0, the critical one on 1
	int onSide = 0, onMain = 0;
	for (const auto& [id, note] : backScore.notes)
	{
		if (back.stageOf(id) == 1)
			++onSide;
		else
			++onMain;
		if (note.tick == 480 && note.critical)
			CHECK_EQ(back.stageOf(id), 1);
		if (note.tick == 480 && !note.critical)
			CHECK_EQ(back.stageOf(id), 0);
	}
	CHECK_EQ(onSide, 4);	// the critical tap + the three points of the hold
	CHECK_EQ(onMain, 2);

	// exporting what came back gives the same stage data again
	std::string again;
	CHECK(St::exportLevelData(backScore, back, again, error));
	CHECK_EQ(count(parse(again), "Stage"), 2);
	CHECK_EQ(count(parse(again), "StageTransformChange"), 2);
	CHECK_EQ(count(parse(again), "CameraChange"), 2);
}

// What the Next SEKAI editor saves, written the way its serializer does (levelData/entities/serialize/*.ts)
TEST(stages_import_next_sekai_editor_sample)
{
	const std::string sample = R"JSON({
	"bgmOffset": -0.25,
	"entities": [
		{ "archetype": "Initialization", "data": [ { "name": "initialLife", "value": 1000 }, { "name": "firstCamera", "ref": "c0" } ] },
		{ "archetype": "#BPM_CHANGE", "data": [ { "name": "#BEAT", "value": 0 }, { "name": "#BPM", "value": 150 } ] },
		{ "name": "g", "archetype": "#TIMESCALE_GROUP", "data": [ { "name": "editorName", "ref": "Default" }, { "name": "first", "ref": "ts" } ] },
		{ "name": "s1", "archetype": "Stage", "data": [ { "name": "editorName", "ref": "#1" }, { "name": "fromStart", "value": 1 }, { "name": "untilEnd", "value": 1 },
			{ "name": "generateSimLines", "value": 0 }, { "name": "firstMaskChange", "ref": "m0" } ] },
		{ "name": "s2", "archetype": "Stage", "data": [ { "name": "editorName", "ref": "#2" }, { "name": "fromStart", "value": 0 }, { "name": "untilEnd", "value": 1 },
			{ "name": "generateSimLines", "value": 1 }, { "name": "firstTransformChange", "ref": "t0" }, { "name": "firstStyleChange", "ref": "y0" } ] },
		{ "name": "c0", "archetype": "CameraChange", "data": [ { "name": "#BEAT", "value": 0 }, { "name": "lane", "value": 0 }, { "name": "size", "value": 6 },
			{ "name": "zoom", "value": 1 }, { "name": "zoomTargetLane", "value": 0 }, { "name": "zoomTargetY", "value": 0 }, { "name": "zoomVerticalAlign", "value": 0 },
			{ "name": "rotate", "value": 0 }, { "name": "stageTilt", "value": 1 }, { "name": "ease", "value": 2 }, { "name": "next", "ref": "c1" } ] },
		{ "name": "c1", "archetype": "CameraChange", "data": [ { "name": "#BEAT", "value": 8 }, { "name": "lane", "value": 2 }, { "name": "size", "value": 4.5 },
			{ "name": "zoom", "value": 1.5 }, { "name": "zoomTargetLane", "value": 0 }, { "name": "zoomTargetY", "value": 0 }, { "name": "zoomVerticalAlign", "value": 1 },
			{ "name": "rotate", "value": 15 }, { "name": "stageTilt", "value": 0.5 }, { "name": "ease", "value": 0 } ] },
		{ "name": "m0", "archetype": "StageMaskChange", "data": [ { "name": "#BEAT", "value": 0 }, { "name": "lane", "value": 0 }, { "name": "size", "value": 6 },
			{ "name": "maskNotes", "value": 0 }, { "name": "ease", "value": 1 } ] },
		{ "name": "t0", "archetype": "StageTransformChange", "data": [ { "name": "#BEAT", "value": 0 }, { "name": "rotate", "value": 0 }, { "name": "xLaneTranslate", "value": 0 },
			{ "name": "yLaneTranslate", "value": 0 }, { "name": "elevation", "value": 0 }, { "name": "anchor", "value": 0 }, { "name": "ease", "value": 3 }, { "name": "next", "ref": "t1" } ] },
		{ "name": "t1", "archetype": "StageTransformChange", "data": [ { "name": "#BEAT", "value": 4 }, { "name": "rotate", "value": -30 }, { "name": "xLaneTranslate", "value": 2.5 },
			{ "name": "yLaneTranslate", "value": 1 }, { "name": "elevation", "value": 0.5 }, { "name": "anchor", "value": 1 }, { "name": "ease", "value": 1 } ] },
		{ "name": "y0", "archetype": "StageStyleChange", "data": [ { "name": "#BEAT", "value": 0 }, { "name": "editorLane", "value": 0 }, { "name": "judgeLineColor", "value": 5 },
			{ "name": "judgeLineStyle", "value": 1 }, { "name": "leftBorderStyle", "value": 3 }, { "name": "rightBorderStyle", "value": 2 }, { "name": "fullWidth", "value": 1 },
			{ "name": "noteAlpha", "value": 0.8 }, { "name": "laneAlpha", "value": 0.6 }, { "name": "judgeLineAlpha", "value": 0.4 }, { "name": "divisionLineAlpha", "value": 0.2 },
			{ "name": "ease", "value": 0 } ] },
		{ "name": "ts", "archetype": "#TIMESCALE_CHANGE", "data": [ { "name": "#TIMESCALE_GROUP", "ref": "g" }, { "name": "#BEAT", "value": 0 }, { "name": "editorLane", "value": -6 },
			{ "name": "#TIMESCALE", "value": 1 }, { "name": "#TIMESCALE_SKIP", "value": 0 }, { "name": "#TIMESCALE_EASE", "value": 0 }, { "name": "transitionStyle", "value": 0 },
			{ "name": "hideNotes", "value": 0 } ] },
		{ "archetype": "NormalTapNote", "data": [ { "name": "#TIMESCALE_GROUP", "ref": "g" }, { "name": "stage", "ref": "s1" }, { "name": "#BEAT", "value": 4 },
			{ "name": "lane", "value": -3.5 }, { "name": "size", "value": 1.5 }, { "name": "direction", "value": 0 }, { "name": "isAttached", "value": 0 },
			{ "name": "isSeparator", "value": 0 }, { "name": "connectorEase", "value": 1 }, { "name": "segmentKind", "value": 1 }, { "name": "segmentAlpha", "value": 1 },
			{ "name": "segmentLayer", "value": 0 }, { "name": "effectKind", "value": 0 }, { "name": "segmentThroughJudgeLine", "value": 0 },
			{ "name": "segmentPresentation", "value": 0 }, { "name": "style", "value": 0 } ] },
		{ "archetype": "NormalTapNote", "data": [ { "name": "#TIMESCALE_GROUP", "ref": "g" }, { "name": "stage", "ref": "s2" }, { "name": "#BEAT", "value": 4 },
			{ "name": "lane", "value": 3.5 }, { "name": "size", "value": 1.5 }, { "name": "direction", "value": 0 }, { "name": "isAttached", "value": 0 },
			{ "name": "isSeparator", "value": 0 }, { "name": "connectorEase", "value": 1 }, { "name": "segmentKind", "value": 1 }, { "name": "segmentAlpha", "value": 1 },
			{ "name": "segmentLayer", "value": 0 }, { "name": "effectKind", "value": 0 }, { "name": "segmentThroughJudgeLine", "value": 0 },
			{ "name": "segmentPresentation", "value": 0 }, { "name": "style", "value": 0 } ] },
		{ "archetype": "CriticalFlickNote", "data": [ { "name": "#TIMESCALE_GROUP", "ref": "g" }, { "name": "stage", "ref": "s2" }, { "name": "#BEAT", "value": 5 },
			{ "name": "lane", "value": 0 }, { "name": "size", "value": 1 }, { "name": "direction", "value": 2 }, { "name": "isAttached", "value": 0 },
			{ "name": "isSeparator", "value": 0 }, { "name": "connectorEase", "value": 1 }, { "name": "segmentKind", "value": 2 }, { "name": "segmentAlpha", "value": 1 },
			{ "name": "segmentLayer", "value": 0 }, { "name": "effectKind", "value": 0 }, { "name": "segmentThroughJudgeLine", "value": 0 },
			{ "name": "segmentPresentation", "value": 0 }, { "name": "style", "value": 0 } ] }
	]
})JSON";

	Score score;
	St::StageSet set;
	std::string error;
	std::vector<std::string> warnings;
	CHECK(St::importLevelData(sample, score, set, error, &warnings));
	CHECK_EQ(error, std::string());
	CHECK(set.enabled);

	// stages in file order, with their flags and joints
	CHECK_EQ(set.stages.size(), static_cast<size_t>(2));
	CHECK_EQ(set.stages[0].name, std::string("#1"));
	CHECK(set.stages[0].fromStart && set.stages[0].untilEnd && !set.stages[0].isolatedSimLines);
	CHECK_EQ(set.stages[0].mask.size(), static_cast<size_t>(1));
	CHECK_EQ(set.stages[1].name, std::string("#2"));
	CHECK(!set.stages[1].fromStart && set.stages[1].untilEnd && set.stages[1].isolatedSimLines);
	CHECK_EQ(set.stages[1].transform.size(), static_cast<size_t>(2));
	CHECK_EQ(set.stages[1].transform[0].ease, 3);
	CHECK_EQ(set.stages[1].transform[1].tick, 4 * 480);
	CHECK(near(set.stages[1].transform[1].rotate, -30.0f));
	CHECK(near(set.stages[1].transform[1].x, 2.5f));
	CHECK(near(set.stages[1].transform[1].y, 1.0f));
	CHECK(near(set.stages[1].transform[1].elevation, 0.5f));
	CHECK_EQ(set.stages[1].transform[1].anchor, 1);
	CHECK_EQ(set.stages[1].style.size(), static_cast<size_t>(1));
	const St::StyleEvent& style = set.stages[1].style[0];
	CHECK(style.judgeLineColor == 5 && style.judgeLineStyle == 1 && style.leftBorder == 3 && style.rightBorder == 2 && style.fullWidth);
	CHECK(near(style.noteAlpha, 0.8f) && near(style.laneAlpha, 0.6f) && near(style.judgeLineAlpha, 0.4f) && near(style.divisionLineAlpha, 0.2f));

	// camera: lane/size are centre + half width, same as the file
	CHECK_EQ(set.camera.size(), static_cast<size_t>(2));
	CHECK(near(set.camera[1].lane, 2.0f) && near(set.camera[1].size, 4.5f) && near(set.camera[1].zoom, 1.5f));
	CHECK(near(set.camera[1].rotate, 15.0f) && near(set.camera[1].stageTilt, 0.5f));
	CHECK_EQ(set.camera[1].zoomVerticalAlign, 1);
	CHECK_EQ(set.camera[1].tick, 8 * 480);
	CHECK_EQ(set.camera[0].ease, 2);
	CHECK_EQ(set.camera[1].ease, 0);

	// notes: the three taps, not the joints taken for slides; each with the stage the file gave it
	CHECK_EQ(score.notes.size(), static_cast<size_t>(3));
	CHECK(score.holdNotes.empty());
	CHECK(near(score.tempoChanges.at(0).bpm, 150.0f));
	for (const auto& [id, note] : score.notes)
	{
		if (note.tick == 4 * 480 && note.lane == 1)
			CHECK_EQ(set.stageOf(id), 0);
		if (note.tick == 4 * 480 && note.lane == 8)
			CHECK_EQ(set.stageOf(id), 1);
		if (note.tick == 5 * 480)
		{
			CHECK_EQ(set.stageOf(id), 1);
			CHECK(note.critical && note.isFlick() && note.flick == FlickType::Right);
		}
	}
	CHECK_EQ(set.noteStage.size(), static_cast<size_t>(2));

	// the camera + stage values evaluate: halfway to the second camera joint the first one's ease (in) applies
	set.sort();
	CHECK(near(St::camera(set, 4 * 480).zoom, 1.125f, 0.01f));
	CHECK(near(St::evaluate(set, 1, 2 * 480).x, 2.5f * St::ease(3, 0.5f), 0.01f));

	// a normal chart (no stages) stays a normal chart
	json plain = json::parse(sample);
	json kept = json::array();
	for (const json& e : plain["entities"])
	{
		const std::string a = e["archetype"].get<std::string>();
		if (a == "Stage" || (a.find("Change") != std::string::npos && a != "#BPM_CHANGE" && a != "#TIMESCALE_CHANGE"))
			continue;
		json copy = e;
		if (copy.contains("data"))
		{
			json data = json::array();
			for (const json& d : copy["data"])
				if (d["name"] != "stage" && d["name"] != "firstCamera")
					data.push_back(d);
			copy["data"] = data;
		}
		kept.push_back(copy);
	}
	plain["entities"] = kept;
	Score plainScore;
	St::StageSet plainSet;
	CHECK(St::importLevelData(plain.dump(), plainScore, plainSet, error));
	CHECK(!plainSet.enabled);
	CHECK(plainSet.empty());
	CHECK_EQ(plainScore.notes.size(), static_cast<size_t>(3));

	// not LevelData at all
	CHECK(!St::importLevelData("hello", plainScore, plainSet, error));
	CHECK(!error.empty());
}
