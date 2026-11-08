#pragma once
#include <map>
#include <string>

// English text for the Beat This! BPM detector and the Interpolate BPM dialog
// (Canvas/UI/BpmDetectWindow). Merge into "en" like CanvasStrings130.h.
namespace MikuMikuWorld
{
	inline const std::map<std::string, std::string>& canvasBpmEnglishStrings()
	{
		static const std::map<std::string, std::string> strings
		{
			// menu
			{"bpm_detect", "Detect BPM..."},
			{"bpm_detect_tooltip", "Finds the BPM, tempo changes, ramps and time signatures of the song."},
			{"bpm_interpolate", "Interpolate BPM..."},
			{"bpm_interpolate_tooltip", "Ramps the BPM from one value to another."},

			// Select BPM
			{"bpm_select_title", "Select BPM"},
			{"bpm_no_music", "Load a song first."},
			{"bpm_unavailable", "BPM detection is not available: %s"},
			{"bpm_stage_decode", "Reading the song..."},
			{"bpm_stage_resample", "Resampling..."},
			{"bpm_stage_spectrogram", "Building the spectrogram..."},
			{"bpm_stage_network", "Listening for beats..."},
			{"bpm_stage_tempo", "Working out the tempo..."},
			{"bpm_stage_done", "Done"},
			{"bpm_cancelling", "Cancelling..."},
			{"bpm_failed", "Detection failed: %s"},
			{"bpm_retry", "Try again"},
			{"bpm_pick", "Pick the one that sounds right:"},
			{"bpm_kind_best", "Best guess"},
			{"bpm_kind_half", "Half time"},
			{"bpm_kind_double", "Double time"},
			{"bpm_kind_simple", "Steady"},
			{"bpm_kind_best_tip", "Tempo changes and ramps as heard."},
			{"bpm_kind_half_tip", "The same song counted at half the speed."},
			{"bpm_kind_double_tip", "The same song counted at double the speed."},
			{"bpm_kind_simple_tip", "One rounded BPM for the whole song."},
			{"bpm_summary_const", "%.2f BPM"},
			{"bpm_summary_range", "%.1f - %.1f BPM"},
			{"bpm_detail_none", "No tempo changes"},
			{"bpm_detail", "%d tempo change(s), %d ramp(s)"},
			{"bpm_meter", "Meter %s"},
			{"bpm_meter_changes", "%s (%d change(s))"},
			{"bpm_fit", "Beats within ~%.0f ms (worst %.0f ms)"},
			{"bpm_notes_hint", "This chart has notes:"},
			{"bpm_keep_ticks", "Keep note ticks"},
			{"bpm_keep_ticks_tooltip", "Notes stay on their beats and move in time with the new tempo."},
			{"bpm_keep_time", "Keep note timing"},
			{"bpm_keep_time_tooltip", "Notes keep their place in the song; their ticks change."},

			// Interpolate BPM
			{"bpm_interp_title", "Interpolate BPM"},
			{"bpm_interp_help", "Writes small BPM steps from the start to the end."},
			{"bpm_interp_from", "From beat"},
			{"bpm_interp_to", "To beat"},
			{"bpm_interp_start", "Start BPM"},
			{"bpm_interp_end", "End BPM"},
			{"bpm_interp_step", "Step"},
			{"bpm_interp_curve", "Curve"},
			{"bpm_interp_count", "%d tempo change(s)"},
			{"bpm_step_32", "1/32 note"},
			{"bpm_step_16", "1/16 note"},
			{"bpm_step_8", "1/8 note"},
			{"bpm_step_4", "1/4 note"},
		};
		return strings;
	}
}
