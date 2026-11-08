#pragma once
#include <map>
#include <string>

// English text for the Stages panel and its menu entries (the model's own keys live in CanvasStringsStages.h).
namespace MikuMikuWorld
{
	inline const std::map<std::string, std::string>& canvasStagesUiEnglishStrings()
	{
		static const std::map<std::string, std::string> strings
		{
			{"stages", "Stages"},
			{"stages_enable", "Enable Dynamic Stages"},
			{"stages_enable_help", "Lets notes live on their own moving stages. Off keeps a normal chart."},
			{"stages_name", "Name"},
			{"stages_show", "Show in the preview"},
			{"stages_solo", "Solo"},
			{"stages_select_notes", "Select this stage's notes"},
			{"stages_settings", "Stage settings"},
			{"stages_lanes", "Lanes"},
			{"stages_base", "Base values (while a track has no joints)"},
			{"stages_now", "Now"},
			{"stages_time", "Time"},
			{"stages_jump", "Jump the cursor here"},
			{"stages_no_joints", "No joints yet. Add one at the cursor."},
			{"stages_menu", "Dynamic Stages"},
			{"stages_menu_enable", "Enable Dynamic Stages"},
			{"stages_menu_open", "Open Stages panel"},
			{"stages_c_lane", "Lane"},
			{"stages_c_size", "Half width"},
			{"stages_c_clip", "Clip notes"},
			{"stages_c_divsize", "Divider every"},
			{"stages_c_parity", "Parity"},
			{"stages_c_yoff", "Line height"},
			{"stages_c_ybeat", "Line beats"},
			{"stages_c_color", "Line colour"},
			{"stages_c_line", "Line style"},
			{"stages_c_lborder", "Left border"},
			{"stages_c_rborder", "Right border"},
			{"stages_c_full", "Full width"},
			{"stages_c_notea", "Notes"},
			{"stages_c_lanea", "Lanes"},
			{"stages_c_linea", "Line"},
			{"stages_c_diva", "Dividers"},
			{"stages_c_rotate", "Turn"},
			{"stages_c_x", "X"},
			{"stages_c_y", "Y"},
			{"stages_c_elev", "Lift"},
			{"stages_c_anchor", "Anchor"},
			{"stages_c_zoom", "Zoom"},
			{"stages_c_zlane", "Zoom at"},
			{"stages_c_zy", "Zoom height"},
			{"stages_c_align", "Zoom towards"},
			{"stages_c_tilt", "Tilt"},
		};
		return strings;
	}
}
