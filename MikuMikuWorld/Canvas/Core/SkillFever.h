#pragma once
#include "../../Score.h"
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

// Skill triggers and the FEVER window, per skill-fever-format/README.txt:
//
//   .sus:            #mmm10:41 skill (lane 0, type 4)
//                    #mmm1f:11 fever chance (lane f, type 1)
//                    #mmm1f:21 fever start  (lane f, type 2)
//   MusicScoreData:  top-level "CanvasSkillFever": { "skills": [tick x6], "feverChance": tick|null,
//                    "feverStart": tick|null }, tick = round(beat * 480)
//
// Official charts carry exactly six skills plus one fever chance and one fever start, at about
// the same fractions of the note span every time (medians over 43 expert charts).
namespace Canvas::SkillFever
{
	inline constexpr const char* JSON_KEY = "CanvasSkillFever";
	inline constexpr double DEFAULT_SKILLS[6] = { 0.112, 0.243, 0.370, 0.534, 0.703, 0.862 };
	inline constexpr double DEFAULT_FEVER_CHANCE = 0.556;
	inline constexpr double DEFAULT_FEVER_START = 0.676;
	inline constexpr int OFFICIAL_SKILL_COUNT = 6;

	struct Markers
	{
		std::vector<int> skills;	// ticks, sorted
		int feverChance{ -1 };		// -1 = none
		int feverStart{ -1 };

		bool empty() const { return skills.empty() && feverChance < 0 && feverStart < 0; }
		bool operator==(const Markers& other) const
		{
			return skills == other.skills && feverChance == other.feverChance && feverStart == other.feverStart;
		}
	};

	bool hasMarkers(const MikuMikuWorld::Score& score);
	Markers readMarkers(const MikuMikuWorld::Score& score);

	// Replace the score's skills and fever with `markers`
	void applyMarkers(MikuMikuWorld::Score& score, const Markers& markers);

	// Official-style positions from the chart's note span (first to last note of any kind),
	// each snapped to the nearest measure start. Time signature changes are honoured; in a
	// plain 4/4 chart this is exactly skill_fever_reference.generate(). Like the production
	// engine (sus_skill_fever.py), two skills that land on the same measure in a very short
	// chart are pushed a beat apart, and a fever start that would not come after the fever
	// chance is moved one measure past it. Empty result when the chart has no notes.
	Markers generateOfficialStyle(const MikuMikuWorld::Score& score);

	// The extension key, as written by score_to_musicscore_json / write_json_skill_fever.
	// Returns null when there are no markers (the key is omitted in that case).
	nlohmann::json toJson(const Markers& markers);
	Markers fromJson(const nlohmann::json& value);

	// Difficulty names from hardest to easiest, used to pick which chart donates its markers.
	inline const std::vector<std::string>& hardestFirst()
	{
		static const std::vector<std::string> order{ "append", "master", "expert", "hard", "normal", "easy" };
		return order;
	}

	// ensure_song(): difficulties without markers get the markers of the hardest difficulty that
	// has some, or official-style defaults computed once from the hardest chart. Difficulties
	// that already have markers are never touched. Returns the difficulties that were filled in.
	std::vector<std::string> ensureSong(std::map<std::string, MikuMikuWorld::Score*>& charts);
}
