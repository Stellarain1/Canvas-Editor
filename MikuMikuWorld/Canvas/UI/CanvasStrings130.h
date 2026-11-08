#pragma once
#include <map>
#include <string>

// English text for what Canvas Editor 1.3.0b added. Merged into "en" after CanvasStrings122.h.
// Keep it short: one line per setting.
namespace MikuMikuWorld
{
	inline const std::map<std::string, std::string>& canvas130EnglishStrings()
	{
		static const std::map<std::string, std::string> strings
		{
			// FEVER
			{"preview_super_fever", "SUPER FEVER!! (off: FEVER!)"},
			{"preview_ui_280", "2.8.0 UI"},
			{"preview_intro", "Start animation (video export)"},

			// Score Preview Animation
			{"preview_animation", "Preview Animation"},
			{"anim_help", "Move, scale, turn or fade parts of the Score Preview. Keys go at the cursor; Ease is how a key is reached."},
			{"anim_add_key", "Key at cursor"},
			{"anim_no_keys", "No keys yet."},
			{"anim_jump", "Move the cursor here"},
			{"anim_time", "Time"},
			{"anim_scale", "Scale"},
			{"anim_turn", "Turn"},
			{"anim_opacity", "Opacity"},
			{"anim_ease", "Ease"},
			{"anim_effects", "Effects"},
			{"anim_kind", "Effect"},
			{"anim_length", "Length"},
			{"anim_strength", "Strength"},
			{"anim_colour", "Colour"},
			{"anim_fx_shake", "Shake"},
			{"anim_fx_flash", "Flash"},
			{"anim_fx_fade", "Fade"},
			{"anim_target_screen", "Whole screen"},
			{"anim_target_stage", "Stage + notes"},
			{"anim_target_score", "Score"},
			{"anim_target_life", "Life"},
			{"anim_target_combo", "Combo"},
			{"anim_target_judge", "Judgement"},
			{"anim_target_skill", "Skill"},
			{"anim_target_members", "Co-op members"},
			{"anim_target_fever", "FEVER"},
			{"anim_target_all_perfect", "ALL PERFECT"},


			// custom skins
			{"custom_skin_new", "New..."},
			{"custom_skin_folder", "Open folder"},
			{"custom_skin_reload", "Reload"},
			{"custom_skin_create", "Create"},
			{"custom_skin_none", "(pick a set)"},
			{"custom_skin_new_hint", "Name it. It starts as a copy of the default set."},
			{"custom_notes_hint", "Edited images show after a restart."},
			{"custom_sound_hint", "Missing sounds play the default set's."},

			// start screen
			{"start_screen", "Start Screen"},
			{"chart_mode", "Charting Mode"},
			{"chart_mode_choose", "Choose..."},
			{"chart_mode_ask", "Ask at startup"},
			{"chart_mode_extended", "Extended Score"},
			{"chart_mode_mmw", "Normal MMW"},
			{"chart_mode_dynamic", "Dynamic Stages"},

			// speed
			{"preview_quality", "Preview quality"},
			{"preview_quality_auto", "Auto"},
			{"preview_quality_tip", "Auto lowers it while playback is slow. The HUD stays sharp."},
		};
		return strings;
	}
}
