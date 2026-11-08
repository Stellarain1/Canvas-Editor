#include "../../UI.h"
#include "ChartOverview.h"
#include "../Core/SkillFever.h"
#include "../../ApplicationConfiguration.h"
#include "../../Colors.h"
#include "../../Constants.h"
#include "../../ScoreContext.h"
#include <algorithm>
#include <cmath>

namespace MikuMikuWorld
{
	namespace
	{
		constexpr ImU32 STRIP_BG = IM_COL32(16, 20, 40, 240);
		constexpr ImU32 STRIP_BORDER = IM_COL32(96, 104, 168, 170);
		constexpr ImU32 MEASURE_LINE = IM_COL32(130, 140, 200, 70);
		constexpr ImU32 MEASURE_LINE_STRONG = IM_COL32(160, 170, 225, 140);
		constexpr ImU32 TAP = IM_COL32(200, 228, 255, 150);
		constexpr ImU32 CRITICAL = IM_COL32(255, 212, 84, 170);
		constexpr ImU32 FLICK = IM_COL32(255, 112, 172, 170);
		constexpr ImU32 TRACE = IM_COL32(170, 255, 210, 150);
		constexpr ImU32 HOLD = IM_COL32(88, 232, 172, 55);
		constexpr ImU32 HOLD_CRITICAL = IM_COL32(255, 212, 84, 50);
		constexpr ImU32 BPM = IM_COL32(120, 220, 110, 255);
		constexpr ImU32 SKILL = IM_COL32(120, 185, 255, 255);
		constexpr ImU32 FEVER = IM_COL32(255, 170, 40, 70);
		constexpr ImU32 FEVER_EDGE = IM_COL32(255, 170, 40, 220);
		constexpr ImU32 ISSUE_ERROR = IM_COL32(245, 92, 100, 255);
		constexpr ImU32 ISSUE_WARNING = IM_COL32(250, 196, 72, 235);
		constexpr ImU32 VIEW_FILL = IM_COL32(120, 200, 255, 40);
		constexpr ImU32 VIEW_BORDER = IM_COL32(235, 242, 255, 235);
		constexpr ImU32 CURSOR = IM_COL32(255, 84, 84, 235);

		ImU32 withAlpha(ImU32 color, float alpha)
		{
			const float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
			return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
		}
	}

	int ChartOverview::draw(ScoreContext& context, const std::vector<Canvas::Issues::Issue>* issues, const ImVec2& min, const ImVec2& max, const View& view)
	{
		const Score& score = context.score;
		ImDrawList* list = ImGui::GetWindowDrawList();
		if (!list || max.x - min.x < 8 || max.y - min.y < 20)
			return -1;

		// ---- chart length: notes, markers, the music, and wherever the timeline is looking
		int endTick = view.lastTick;
		for (const auto& [id, note] : score.notes)
			endTick = std::max(endTick, note.tick);
		for (const auto& skill : score.skills)
			endTick = std::max(endTick, skill.tick);
		endTick = std::max({ endTick, score.fever.chanceTick, score.fever.startTick });
		const float musicLength = context.audio.getMusicLength();
		if (musicLength > 0)
			endTick = std::max(endTick, accumulateTicks(musicLength + context.workingData.musicOffset / 1000.0f, TICKS_PER_BEAT, score.tempoChanges));
		endTick += TICKS_PER_BEAT * 4;
		// grow smoothly, shrink only when well past (avoids the scale jumping while scrolling)
		if (endTick > endTickCache || endTick < endTickCache * 0.8)
			endTickCache = endTick;
		endTick = std::max(1, endTickCache);

		const float pad = 3.0f;
		const ImVec2 inner0{ min.x + pad, min.y + pad }, inner1{ max.x - pad, max.y - pad };
		const float height = inner1.y - inner0.y, width = inner1.x - inner0.x;
		auto yOf = [&](int tick) { return inner1.y - height * std::clamp(static_cast<float>(tick) / endTick, 0.0f, 1.0f); };
		auto xOf = [&](float lane) { return inner0.x + width * std::clamp(lane / NUM_LANES, 0.0f, 1.0f); };
		auto tickOf = [&](float y) { return static_cast<int>(std::round((inner1.y - y) / height * endTick)); };

		list->AddRectFilled(min, max, STRIP_BG, 6.0f);
		list->AddRect(min, max, STRIP_BORDER, 6.0f, 0, 1.0f);
		list->PushClipRect(inner0, inner1, true);

		// ---- measure lines, thinned out when they would be closer than 4 px
		{
			const int measures = std::max(1, accumulateMeasures(endTick, TICKS_PER_BEAT, score.timeSignatures));
			int step = 1;
			while (height / measures * step < 4.0f && step < 64)
				step *= 2;
			for (int m = 0; m <= measures; m += step)
			{
				const float y = std::floor(yOf(measureToTicks(m, TICKS_PER_BEAT, score.timeSignatures))) + 0.5f;
				list->AddLine({ inner0.x, y }, { inner1.x, y }, m % (step * 4) == 0 ? MEASURE_LINE_STRONG : MEASURE_LINE);
			}
		}

		// ---- FEVER window
		if (config.overviewShowMarkers && score.fever.chanceTick >= 0 && score.fever.startTick > score.fever.chanceTick)
		{
			const float top = yOf(score.fever.startTick), bottom = yOf(score.fever.chanceTick);
			list->AddRectFilled({ inner1.x - width * 0.12f, top }, { inner1.x, bottom }, FEVER);
			list->AddLine({ inner0.x, top }, { inner1.x, top }, FEVER_EDGE);
			list->AddLine({ inner0.x, bottom }, { inner1.x, bottom }, FEVER_EDGE);
		}

		// ---- holds and notes (thinner when the chart is squeezed into few pixels per beat)
		if (config.overviewShowNotes)
		{
			const float pixelsPerBeat = height * TICKS_PER_BEAT / endTick;
			const float noteHalfHeight = std::clamp(pixelsPerBeat * 0.12f, 0.5f, 2.0f);
			for (const auto& [id, hold] : score.holdNotes)
			{
				auto start = score.notes.find(hold.start.ID);
				auto end = score.notes.find(hold.end);
				if (start == score.notes.end() || end == score.notes.end() || context.isLayerHidden(start->second.layer))
					continue;

				ImU32 color = start->second.critical ? HOLD_CRITICAL : HOLD;
				if (hold.isGuide())
				{
					ImVec4 swatch = guideColorSwatch(hold.guideColor);
					swatch.w = 0.18f;
					color = ImGui::ColorConvertFloat4ToU32(swatch);
				}

				// straight segments between visible joints are plenty at this size
				const Note* previous = &start->second;
				auto segment = [&](const Note& next)
				{
					const ImVec2 a0{ xOf(previous->lane), yOf(previous->tick) }, a1{ xOf(previous->lane + previous->width), yOf(previous->tick) };
					const ImVec2 b0{ xOf(next.lane), yOf(next.tick) }, b1{ xOf(next.lane + next.width), yOf(next.tick) };
					list->AddQuadFilled(a0, a1, b1, b0, color);
					previous = &next;
				};
				for (const auto& step : hold.steps)
				{
					auto it = score.notes.find(step.ID);
					if (it != score.notes.end())
						segment(it->second);
				}
				segment(end->second);
			}

			for (const auto& [id, note] : score.notes)
			{
				if (context.isLayerHidden(note.layer))
					continue;
				bool head = note.getType() == NoteType::Tap;
				if (!head && note.getType() != NoteType::HoldMid)
				{
					auto hold = score.holdNotes.find(note.getType() == NoteType::Hold ? note.ID : note.parentID);
					const HoldNoteType kind = hold == score.holdNotes.end() ? HoldNoteType::Hidden
						: (note.getType() == NoteType::Hold ? hold->second.startType : hold->second.endType);
					head = kind == HoldNoteType::Normal;
				}
				if (!head)
					continue;

				ImU32 color = TAP;
				if (note.isFlick())
					color = FLICK;
				else if (note.critical)
					color = CRITICAL;
				else if (note.friction)
					color = TRACE;
				const float y = yOf(note.tick);
				list->AddRectFilled({ xOf(note.lane) + 0.5f, y - noteHalfHeight }, { xOf(note.lane + note.width) - 0.5f, y + noteHalfHeight }, color);
			}
		}

		// ---- BPM changes and skills
		if (config.overviewShowMarkers)
		{
			for (const auto& tempo : score.tempoChanges)
			{
				const float y = yOf(tempo.tick);
				list->AddTriangleFilled({ inner0.x, y - 3 }, { inner0.x + 5, y }, { inner0.x, y + 3 }, BPM);
			}
			for (const auto& skill : score.skills)
			{
				const float y = yOf(skill.tick);
				list->AddTriangleFilled({ inner1.x, y - 3 }, { inner1.x - 5, y }, { inner1.x, y + 3 }, SKILL);
			}
		}

		// ---- where the issues are
		if (config.overviewShowIssues && issues)
		{
			for (const auto& issue : *issues)
			{
				if (issue.tick < 0 || !issue.difficulty.empty() || issue.severity == Canvas::Issues::Severity::Info)
					continue;
				const float y = std::floor(yOf(issue.tick)) + 0.5f;
				const ImU32 color = issue.severity == Canvas::Issues::Severity::Error ? ISSUE_ERROR : ISSUE_WARNING;
				list->AddLine({ inner0.x, y }, { inner1.x, y }, color, 2.0f);
			}
		}

		// ---- cursor and the part the timeline shows
		{
			const float y = yOf(view.cursorTick);
			list->AddLine({ inner0.x - 2, y }, { inner1.x + 2, y }, CURSOR, 1.5f);

			const float top = std::max(inner0.y, yOf(view.lastTick)), bottom = std::min(inner1.y, yOf(view.firstTick));
			list->AddRectFilled({ inner0.x - 1, top }, { inner1.x + 1, bottom }, VIEW_FILL, 3.0f);
			list->AddRect({ inner0.x - 1, top }, { inner1.x + 1, bottom }, VIEW_BORDER, 3.0f, 0, 2.0f);
		}
		list->PopClipRect();

		// ---- interaction: click / drag to scroll, wheel to scroll, hover for the position
		int target = -1;
		ImGui::SetCursorScreenPos(min);
		ImGui::InvisibleButton("##chart_overview", max - min);
		const ImVec2 mouse = ImGui::GetMousePos();
		if (ImGui::IsItemActive())
			target = std::clamp(tickOf(mouse.y), 0, endTick);

		if (ImGui::IsItemHovered())
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0)
			{
				const int span = std::max(TICKS_PER_BEAT, view.lastTick - view.firstTick);
				target = std::clamp((view.firstTick + view.lastTick) / 2 + static_cast<int>(wheel * span * 0.25f), 0, endTick);
			}

			if (!ImGui::IsItemActive())
			{
				const int tick = std::clamp(tickOf(mouse.y), 0, endTick);
				const int measure = accumulateMeasures(tick, TICKS_PER_BEAT, score.timeSignatures);
				const float seconds = accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges);
				list->AddLine({ min.x, mouse.y }, { max.x, mouse.y }, withAlpha(VIEW_BORDER, 0.6f));

				if (ImGui::BeginTooltip())
				{
					ImGui::Text("%s %d  %d:%05.2f", getString("issues_measure"), measure, static_cast<int>(seconds / 60), std::fmod(seconds, 60.0f));
					if (issues && config.overviewShowIssues)
					{
						int shown = 0;
						for (const auto& issue : *issues)
						{
							if (issue.tick < 0 || !issue.difficulty.empty() || issue.severity == Canvas::Issues::Severity::Info)
								continue;
							if (std::abs(yOf(issue.tick) - mouse.y) > 4.0f)
								continue;
							if (shown++ < 4)
								ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(issue.severity == Canvas::Issues::Severity::Error ? ISSUE_ERROR : ISSUE_WARNING),
									"%s", issue.title.c_str());
						}
						if (shown > 4)
							ImGui::TextDisabled("+%d", shown - 4);
					}
					ImGui::EndTooltip();
				}
			}
		}

		// options live on the strip itself
		if (ImGui::BeginPopupContextItem("##overview_menu"))
		{
			ImGui::MenuItem(getString("overview_show_notes"), nullptr, &config.overviewShowNotes);
			ImGui::MenuItem(getString("overview_show_issues"), nullptr, &config.overviewShowIssues);
			ImGui::MenuItem(getString("overview_show_markers"), nullptr, &config.overviewShowMarkers);
			ImGui::MenuItem(getString("overview_on_left"), nullptr, &config.overviewOnLeft);
			ImGui::SetNextItemWidth(120);
			ImGui::SliderInt(getString("overview_width"), &config.overviewWidth, 20, 160);
			ImGui::Separator();
			if (ImGui::MenuItem(getString("overview_hide")))
				config.overviewEnabled = false;
			ImGui::EndPopup();
		}

		return target;
	}
}
