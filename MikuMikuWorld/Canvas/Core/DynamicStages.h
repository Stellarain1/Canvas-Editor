#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	struct Score;
}

// Dynamic Stages: the Next SEKAI model (https://github.com/Next-SEKAI/sonolus-next-sekai-editor and
// its Sonolus engine). A chart has several stages (lane sets) that can move, rotate, tilt, fade and
// be masked, a camera that frames them, and every note belongs to one stage.
//
// The model follows the engine on purpose. Stage events are keyframes ("joints") in FOUR separate
// tracks per stage, each joint holding a group of values and the ease it uses to get to the NEXT joint:
//   mask      which lanes show (centre + half width), and whether notes get clipped to them
//   pivot     where the lane dividers sit, the judge line's vertical offset
//   style     judge line / border looks, and every alpha (lane, notes, judge line, dividers)
//   transform rotation, x / y translation, elevation (tilts the stage back), anchor
// Plus one global camera track. Ticks are Canvas ticks (480 per beat); the file format wants beats,
// that is just tick / 480.
//
// The set sits NEXT to the Score (Score::stages, saved in the .ce extension JSON). Notes are looked up
// by Note::ID in `noteStage`; a note that is not in the map is on stage 0, the default stage, which
// always exists.
namespace Canvas::Stages
{
	// ---------------------------------------------------------------- easing
	// The ids are the engine's EaseType, so they go into LevelData as they are. The Next SEKAI editor
	// itself only uses 0..5 (none / linear / in / out / in-out / out-in, all quadratic); the engine
	// takes all 42 so we do too.
	constexpr int EaseNone = 0;		// holds the first joint's values until the next joint (a step)
	constexpr int EaseLinear = 1;
	constexpr int EaseIn = 2;		// quad
	constexpr int EaseOut = 3;
	constexpr int EaseInOut = 4;
	constexpr int EaseOutIn = 5;
	constexpr int EaseCount = 42;	// 6.. = sine, cubic, quart, quint, expo, circ, back, elastic (4 modes each), then 4 steps

	// 0..1 -> 0..1 (back / elastic go outside it on the way). Out of range ids act like linear.
	float ease(int id, float t);
	const char* easeName(int id);

	// ---------------------------------------------------------------- events
	// Every event has `tick` and `ease` (how this joint reaches the next one in its track).

	struct MaskEvent
	{
		int tick{};
		float lane{};			// centre of the lit lanes, in lanes (0 = middle of a 12 lane stage)
		float size{ 6 };		// HALF the width, in lanes (6 = 12 lanes)
		bool maskNotes{};		// notes outside the mask get clipped too
		int ease{ EaseLinear };
		float left() const { return lane - size; }
		float width() const { return size * 2; }
	};

	struct PivotEvent
	{
		int tick{};
		float lane{};			// where the lane dividers are counted from
		int divisionSize{ 2 };	// a divider every N lanes (2 = the game's look)
		int divisionParity{};	// 0 even: dividers land on pivot + k*N, 1 odd: shifted half a division
		float yOffset{};		// judge line height, in the engine's depth units (0 = normal)
		float yBeatOffset{};	// same, but in beats of scroll; the engine converts it with the bpm and note speed
		int ease{ EaseLinear };
	};

	struct StyleEvent
	{
		int tick{};
		int judgeLineColor{};	// 0 neutral 1 red 2 green 3 blue 4 yellow 5 purple 6 cyan 7 black
		int judgeLineStyle{};	// 0 default, 1 single line
		int leftBorder{};		// 0 default, 1 light, 2 disabled, 3 medium
		int rightBorder{};
		bool fullWidth{};		// judge line runs across the whole screen
		float noteAlpha{ 1 };
		float laneAlpha{ 1 };
		float judgeLineAlpha{ 1 };
		float divisionLineAlpha{ 1 };
		int editorLane{};		// which row the joint sits in on the Next SEKAI editor's timeline (kept for round trips)
		int ease{ EaseLinear };
	};

	struct TransformEvent
	{
		int tick{};
		float rotate{};			// degrees, clockwise
		float x{};				// slide sideways, in lanes (at the camera's zoom)
		float y{};				// slide up (+) / down (-), in lanes
		float elevation{};		// lifts the stage back like a floor tilting away, in lanes
		int anchor{};			// 0: pivot on the stage's own judge line, 1: pivot / offset from the screen centre
		int ease{ EaseLinear };
	};

	struct CameraEvent
	{
		int tick{};
		float lane{};			// centre of what the camera frames, in lanes
		float size{ 6 };		// HALF the framed width in lanes (6 = the usual 12 lane view; engine range 1.5..6 in the editor)
		float zoom{ 1 };
		float zoomTargetLane{};	// zoom pivot, lanes from `lane`
		float zoomTargetY{};	// zoom pivot height, -1..1
		int zoomVerticalAlign{};// 0 zoom towards the bottom, 1 towards the centre
		float rotate{};			// degrees, positive turns the picture clockwise
		float stageTilt{ 1 };	// 1 = the normal perspective, 0 = flat top-down
		int ease{ EaseLinear };
	};

	// ---------------------------------------------------------------- stages
	struct Stage
	{
		int id{};
		std::string name{ "Stage" };
		int laneCount{ 12 };			// lanes the default mask opens (the stage's "width")
		bool fromStart{ true };			// drawn from the beginning (otherwise from its first mask joint)
		bool untilEnd{ true };			// drawn to the end (otherwise until its last mask joint)
		bool isolatedSimLines{};		// simultaneous lines only join notes of this stage

		// What a track means while it has NO joints. Export writes them as a joint at tick 0 (the
		// engine draws nothing for a stage whose mask / style track is empty). Their tick is ignored.
		MaskEvent baseMask;
		PivotEvent basePivot;
		StyleEvent baseStyle;
		TransformEvent baseTransform;

		std::vector<MaskEvent> mask;
		std::vector<PivotEvent> pivot;
		std::vector<StyleEvent> style;
		std::vector<TransformEvent> transform;

		bool hasEvents() const;
	};

	struct StageSet
	{
		bool enabled{};					// the chart is a Dynamic Stages chart (mode picker); data may exist while off
		std::vector<Stage> stages;		// sorted by id; id 0 is the default stage and is never removed
		std::vector<CameraEvent> camera;
		std::map<int, int> noteStage;	// Note::ID -> stage id; missing = stage 0

		StageSet();

		bool empty() const;				// only the untouched default stage, no camera, no assignments
		Stage* find(int stageId);
		const Stage* find(int stageId) const;
		// new stage with a free id and a default 12 lane mask; returns its id
		int addStage(const std::string& name = {});
		// drops the stage; its notes go back to stage 0. False for stage 0 / unknown ids.
		bool removeStage(int stageId);
		int stageOf(int noteId) const;
		// stage 0 erases the entry
		void assign(int noteId, int stageId);
		void assign(const std::vector<int>& noteIds, int stageId);
		// forgets assignments of notes the score does not have (and of stages that are gone)
		void prune(const MikuMikuWorld::Score& score);
		// the editor gave notes new ids (paste, undo...): oldId -> newId; ids missing from the map are dropped
		void remapNotes(const std::map<int, int>& oldToNew);
		// keeps stage 0 present, tracks sorted by tick (a stable sort: two joints on one tick are a jump, and stay)
		void sort();

		// one joint per tick: adds, or replaces the one on the same tick. No-op for unknown stages.
		void setMask(int stageId, const MaskEvent& e);
		void setPivot(int stageId, const PivotEvent& e);
		void setStyle(int stageId, const StyleEvent& e);
		void setTransform(int stageId, const TransformEvent& e);
		void setCamera(const CameraEvent& e);
	};

	// ---------------------------------------------------------------- evaluation
	// Optional: ticks -> seconds. The engine eases by TIME, not by beat, so with tempo changes inside
	// a joint pair the curve differs a little from a tick based one. Leave it empty for plain ticks.
	using TickToSeconds = std::function<double(double tick)>;

	// Everything a stage looks like at one moment. Values between two joints are eased between them;
	// the on/off style choices (colours, borders, divisions) switch with `*To` + `progress` so a
	// renderer can cross-fade them the way the engine does.
	struct StageState
	{
		bool visible{ true };			// the stage is on screen at this tick (mask track + fromStart / untilEnd)

		float maskLane{}, maskHalfWidth{ 6 };
		bool maskNotes{};

		float pivotLane{};
		float yOffset{}, yBeatOffset{};	// total judge line offset = yOffset + yBeatOffset * 60 / bpm / preemptSeconds
		int divisionSize{ 2 }, divisionParity{};
		int divisionSizeTo{ 2 }, divisionParityTo{};
		float divisionProgress{};		// 0 = the first, 1 = the To pair

		int judgeLineColor{}, judgeLineColorTo{};
		int judgeLineStyle{}, judgeLineStyleTo{};
		int leftBorder{}, leftBorderTo{};
		int rightBorder{}, rightBorderTo{};
		float styleProgress{};			// cross-fade of the pairs above
		float fullWidth{};				// 0..1 (blends when one joint has it and the next does not)
		float noteAlpha{ 1 }, laneAlpha{ 1 }, judgeLineAlpha{ 1 }, divisionLineAlpha{ 1 };

		float rotate{};					// degrees, clockwise
		float x{}, y{};					// lanes
		float elevation{};
		float centerWeight{};			// 0..1, the anchor blended (1 = centre anchor)

		bool hasTransform() const;
	};

	// The stage `stageId` at `tick` (fractional ticks are fine). A joint exactly on `tick` already
	// counts (the "right limit": the value just after a jump). Unknown ids give a hidden default state.
	StageState evaluate(const StageSet& set, int stageId, double tick, const TickToSeconds& toSeconds = {});

	// The camera at `tick`. Fields marked (derived) are what the projection needs; they are blended
	// the way the engine blends them (zoom target and anchor as screen points).
	struct CameraState
	{
		float lane{}, size{ 6 };
		float zoom{ 1 };
		float zoomTargetLane{};
		float rotate{};					// degrees
		float stageTilt{ 1 };
		float targetX{}, targetY{};		// (derived) zoom target on screen, engine screen units
		float anchorY{};				// (derived) zoom anchor height on screen
	};
	CameraState camera(const StageSet& set, double tick, const TickToSeconds& toSeconds = {});

	// ---------------------------------------------------------------- projection
	// stage space -> the Score Preview's picture.
	//
	//   lane   where a note sits on the stage, in lanes: -6 is the left edge of a 12 lane stage, 0 the
	//          middle, +6 the right edge (Canvas lane L, width W -> centre L - 6 + W / 2)
	//   depth  how far down the highway: 1 = the judge line, ~0.07 = where notes spawn (kSpawnDepth),
	//          0 = the vanishing point. The same convention as Canvas::Preview::Projector. Use
	//          approachDepth() to get it from "how far through its fall a note is" (0..1)
	//
	// The result is in the 16:9 picture, 0..1 across (left to right) and 0..1 down (top to bottom), so
	// pixel = frameOrigin + point * frameSize. It is the engine's own maths (layout.py's camera layout,
	// compute_stage_transform, the tilt / perspective widths), so with the default camera and the
	// default stage it gives the same picture as the normal preview, and a stage that rotates, slides
	// or tilts moves with the same perspective as in the Next SEKAI engine.
	struct Point
	{
		float x{}, y{};
	};

	constexpr float kSpawnDepth = 0.07269f;	// 1.06^-45, the depth a note starts at

	// depth for a note that has fallen `progress` (0 spawn .. 1 judge line) of its way, at a camera tilt
	float approachDepth(float progress, float stageTilt = 1.0f);

	// Prepares the stage + camera once; apply() is cheap, so build one per stage per frame and call
	// it for every corner you draw.
	class StageProjection
	{
	public:
		StageProjection() = default;
		StageProjection(const StageState& stage, const CameraState& camera);

		Point apply(float lane, float depth) const;
		// the same point in the engine's screen units (x -1.78..1.78, y -1..1, y up)
		Point applyEngine(float lane, float depth) const;

	private:
		double tilt{ 1 };
		// camera layout (after zoom)
		double t{}, wScale{}, hScale{}, xTranslate{}, rotation{};
		// the stage's screen mapping
		double a00{ 1 }, a01{}, a02{}, a10{}, a11{ 1 }, a12{};
	};

	Point projectLane(const StageState& stage, const CameraState& camera, float lane, float depth);

	// ---------------------------------------------------------------- JSON (chart file, .ce extension block)
	// "version": 1. fromJson ignores what it does not know and returns false for text that is not an object.
	std::string toJson(const StageSet& set);
	bool fromJson(const std::string& text, StageSet& set);

	// ---------------------------------------------------------------- Next SEKAI engine LevelData
	// A Sonolus LevelData (JSON text) for the Next SEKAI engine: the normal chart exactly as
	// LevelDataSerializer writes it, plus a `Stage` entity per stage with its four joint chains
	// (StageMaskChange / StagePivotChange / StageStyleChange / StageTransformChange, linked by `next`
	// and hooked in as firstMaskChange ... ), a CameraChange chain hooked in as the Initialization's
	// firstCamera, and a `stage` ref on every note. `warnings` (optional) gets what the normal exporter
	// had to approximate plus the stage specific ones.
	bool exportLevelData(const MikuMikuWorld::Score& score, const StageSet& stages, std::string& json, std::string& error,
		std::vector<std::string>* warnings = nullptr);

	// Reads the Next SEKAI editor's save (a Sonolus LevelData; gzip is detected) into a Score and its
	// stages. A file without Stage entities is a normal chart: `stages.enabled` stays false. Notes the
	// 12 lane Score cannot hold (fractional lanes, outside -6..6) are snapped, as LevelDataSerializer does.
	bool importLevelData(const std::string& bytes, MikuMikuWorld::Score& score, StageSet& stages, std::string& error,
		std::vector<std::string>* warnings = nullptr);
}
