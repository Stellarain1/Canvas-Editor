#pragma once
#include <map>
#include <string>

// English text for what Canvas Editor 1.2.2b added. Merged into "en" after CanvasStrings121.h.
namespace MikuMikuWorld
{
	inline const std::map<std::string, std::string>& canvas122EnglishStrings()
	{
		static const std::map<std::string, std::string> strings
		{
			// live cards
			{"live_cards", "Live Cards"},
			{"live_cards_note", "The 5 cards the preview uses: skill panel, cut-ins, co-op members."},
			{"preview_card_cutins", "Preview: Card Cut-ins"},
			{"preview_ap_combo", "Rainbow ALL PERFECT combo"},
			{"card_name", "Name"},
			{"card_skill", "Skill"},
			{"card_skill_score", "Score UP"},
			{"card_skill_judgment", "Judgement UP"},
			{"card_skill_life", "Life Recovery"},
			{"card_value", "Value"},
			{"card_image", "Icon..."},
			{"card_cutin", "Cut-in..."},
			{"card_voice", "Voice..."},
			{"card_reset", "Reset"},
			{"card_link_hint", "https://sekai.best/card/1484"},
			{"card_trained", "Trained"},
			{"card_get", "Get"},
			{"card_choose_image", "Choose Image"},
			{"card_choose_voice", "Choose Voice"},
			{"card_no_voice", "no voice"},

			// chart end
			{"chart_end_place", "Place End Here"},
			{"chart_end_remove", "Remove End"},
			{"chart_end_tooltip", "Chart end: drag to move, double-click to remove."},

			// frame rate
			{"frame_rate", "Frame Rate"},
			{"frame_rate_monitor", "Monitor (VSync)"},
			{"frame_rate_unlimited", "Unlimited"},

			// charting timer
			{"timer_session", "Session"},
			{"timer_total", "Total"},
			{"timer_tooltip", "Time charting: since Canvas opened / all time."},
			{"timer_reset_total", "Reset Total"},
		};
		return strings;
	}
}
