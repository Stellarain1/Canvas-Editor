#pragma once
#include "../../Score.h"
#include <json.hpp>
#include <string>
#include <vector>

// A plain JSON view of a chart for tools (canvas-cli dump), and edit
// operations on it. Positions come as ticks (480 per beat) plus measure / beat / seconds so an
// agent can think in whichever it likes. Lanes are 0 (left) to 11 (right); a note covers
// lanes lane .. lane + width - 1.
//
//   {"ticksPerBeat":480, "offsetMs":..., "tempo":[{"tick":0,"bpm":148}], "timeSignatures":[...],
//    "notes":[{"id":5,"type":"tap","tick":1920,"measure":1,"beat":0,"time":2.83,"lane":3,"width":3,
//              "critical":false,"flick":"none"}],               type: tap | trace
//    "holds":[{"id":9,"kind":"hold","critical":false,"points":[
//              {"tick":..,"lane":..,"width":..,"role":"start","ease":"linear"},
//              {"role":"step","visibility":"visible|hidden|skip",...},
//              {"role":"end","flick":"none"}]}],                 kind: hold | guide
//    "skills":[ticks], "fever":{"chance":tick,"start":tick}}
namespace Canvas::ChartJson
{
	// The chart (or the part between fromTick and toTick, -1 = open) as JSON.
	nlohmann::ordered_json toJson(const MikuMikuWorld::Score& score, int fromTick = -1, int toTick = -1);

	// Tick of a position given as {"tick": n} or {"measure": m, "beat": b} (measure 0-based,
	// beat 0-based and may be fractional, e.g. 1.5 = the "and" of beat 2).
	int tickOf(const MikuMikuWorld::Score& score, const nlohmann::json& position);

	struct EditResult
	{
		int applied{};
		std::vector<std::string> errors;	// one per operation that failed (the rest still apply)
		std::vector<int> createdIds;
	};

	// Applies edit operations in order. Operations:
	//   {"op":"add_tap", position, "lane", "width", "critical"?, "flick"?: none|up|left|right, "trace"?}
	//   {"op":"add_hold", "points":[{position, "lane", "width", "ease"?, "visibility"?}...], "critical"?,
	//    "endFlick"?, "guide"?: false, "guideColor"?: green|...}
	//   {"op":"delete", "ids":[...]}                  (a hold's id removes the whole hold)
	//   {"op":"delete_range", "from": position, "to": position, "lanes"?: [first, last]}
	//   {"op":"move", "id", position?, "lane"?, "width"?}     (moving a hold's id moves the whole hold)
	//   {"op":"set", "id", "critical"?, "flick"?, "trace"?}
	//   {"op":"mirror", "from": position, "to": position}
	//   {"op":"shift", "from": position, "to": position, "byTicks"?, "byLanes"?}
	//   {"op":"set_bpm", position, "bpm"}  {"op":"set_offset", "ms"}
	EditResult applyEdits(MikuMikuWorld::Score& score, const nlohmann::json& operations);
}
