#pragma once
#include <map>
#include <string>

// English text for Dynamic Stages (stage list, events, export). Merge into "en" like the other CanvasStrings headers.
namespace MikuMikuWorld
{
	inline const std::map<std::string, std::string>& canvasStagesEnglishStrings()
	{
		static const std::map<std::string, std::string> strings
		{
			{"stages_title", "Stages"},
			{"stages_add", "Add stage"},
			{"stages_remove", "Remove stage"},
			{"stages_main", "Main stage"},
			{"stages_assign", "Put selected notes on this stage"},
			{"stages_help", "Each stage has its own lanes, motion and look. Notes follow the stage they are on."},
			{"stages_from_start", "From start"},
			{"stages_until_end", "Until end"},
			{"stages_isolated_sim", "Own simultaneous lines"},
			{"stages_track_mask", "Mask"},
			{"stages_track_pivot", "Pivot"},
			{"stages_track_style", "Style"},
			{"stages_track_transform", "Move"},
			{"stages_track_camera", "Camera"},
			{"stages_key_at_cursor", "Joint at cursor"},
			{"stages_ease", "Ease"},
			{"stages_export", "Export Next SEKAI LevelData"},
			{"stages_import", "Import Next SEKAI editor save"},
		};
		return strings;
	}
}
