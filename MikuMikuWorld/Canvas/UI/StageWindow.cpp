#include "StageWindow.h"
#include "../../UI.h"
#include "../../Constants.h"
#include "../../Localization.h"
#include "../../ScoreEditorTimeline.h"
#include "../../ScorePreview.h"	// hiddenStages / soloStage live there
#include "../../IconsFontAwesome5.h"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_stdlib.h"
#include <algorithm>

namespace St = Canvas::Stages;

namespace MikuMikuWorld
{
	namespace
	{
		std::string clock(const Score& score, int tick)
		{
			const float seconds = accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges);
			const int minutes = static_cast<int>(seconds) / 60;
			return IO::formatString("%d:%05.2f", minutes, seconds - minutes * 60.0f);
		}

		// tables scroll sideways in a narrow panel; this keeps them only as tall as their rows (capped)
		ImVec2 tableSize(size_t rows, float share)
		{
			const float wanted = (static_cast<float>(rows) + 1.0f) * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ScrollbarSize + 4.0f;
			return ImVec2(0.0f, std::min(wanted, std::max(ImGui::GetFrameHeight() * 5.0f, ImGui::GetContentRegionAvail().y * share)));
		}

		float timeColumnWidth() { return ImGui::CalcTextSize("00:00.00").x + ImGui::GetStyle().CellPadding.x * 2.0f; }
		// fixed widths: with sideways scrolling, stretched columns would squash the drag boxes to nothing
		float numberColumnWidth() { return ImGui::CalcTextSize("-0000.00x").x + ImGui::GetStyle().FramePadding.x * 2.0f; }
		float wordColumnWidth(const char* longest) { return ImGui::CalcTextSize(longest).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f; }
		float easeColumnWidth() { return wordColumnWidth("out-in elastic"); }

		const char* const judgeColors[] = { "Neutral", "Red", "Green", "Blue", "Yellow", "Purple", "Cyan", "Black" };
		const char* const lineStyles[] = { "Default", "Single" };
		const char* const borders[] = { "Default", "Light", "Off", "Medium" };
		const char* const parities[] = { "Even", "Odd" };
		const char* const anchors[] = { "Judge line", "Screen" };
		const char* const aligns[] = { "Bottom", "Centre" };

		constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;

		ImVec4 chipColor(int stageId) { return ImGui::ColorConvertU32ToFloat4(St::stageColor(stageId)); }
	}

	void StageWindow::trackEdit(ScoreContext& context)
	{
		if (ImGui::IsItemActivated())
			editStart = context.score;
		if (ImGui::IsItemDeactivatedAfterEdit() && editStart)
		{
			context.score.stages.sort();
			commit(context, *editStart);
			editStart.reset();
		}
		else if (ImGui::IsItemDeactivated())
			editStart.reset();
	}

	void StageWindow::commit(ScoreContext& context, const Score& before)
	{
		context.pushHistory(getString("stages"), before, context.score);
	}

	void StageWindow::dragCell(ScoreContext& context, const char* id, float& value, float speed, float lo, float hi, const char* format)
	{
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::DragFloat(id, &value, speed, lo, hi, format);
		trackEdit(context);
	}

	void StageWindow::comboCell(ScoreContext& context, const char* id, int& value, const char* const* names, int count)
	{
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-FLT_MIN);
		const int shown = std::clamp(value, 0, count - 1);
		if (ImGui::BeginCombo(id, names[shown]))
		{
			for (int i = 0; i < count; ++i)
				if (ImGui::Selectable(names[i], i == shown) && i != value)
				{
					const Score before = context.score;
					value = i;
					commit(context, before);
				}
			ImGui::EndCombo();
		}
	}

	void StageWindow::checkCell(ScoreContext& context, const char* id, bool& value)
	{
		ImGui::TableNextColumn();
		bool v = value;
		if (ImGui::Checkbox(id, &v))
		{
			const Score before = context.score;
			value = v;
			commit(context, before);
		}
	}

	void StageWindow::easeCell(ScoreContext& context, int& ease)
	{
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##ease", St::easeName(ease)))
		{
			// none + linear, then the families in blocks of in / out / in-out / out-in
			for (int i = 0; i < St::EaseCount; ++i)
			{
				if (i >= 2 && (i - 2) % 4 == 0)
					ImGui::Separator();
				if (ImGui::Selectable(St::easeName(i), i == ease) && i != ease)
				{
					const Score before = context.score;
					ease = i;
					commit(context, before);
				}
			}
			ImGui::EndCombo();
		}
	}

	template <typename Ev, typename Row>
	void StageWindow::jointTable(ScoreContext& context, ScoreEditorTimeline& timeline, const char* id, std::vector<Ev>& events,
		const std::vector<std::pair<const char*, float>>& columns, Row row)
	{
		if (events.empty())
		{
			ImGui::TextDisabled("%s", getString("stages_no_joints"));
			return;
		}

		if (!ImGui::BeginTable(id, static_cast<int>(columns.size()) + 3, tableFlags, tableSize(events.size(), 0.6f)))
			return;
		ImGui::TableSetupColumn(getString("stages_time"), ImGuiTableColumnFlags_WidthFixed, timeColumnWidth());
		for (const auto& [title, width] : columns)
			ImGui::TableSetupColumn(title, ImGuiTableColumnFlags_WidthFixed, width);
		ImGui::TableSetupColumn(getString("stages_ease"), ImGuiTableColumnFlags_WidthFixed, easeColumnWidth());
		ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x);
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableHeadersRow();

		int erase = -1;
		for (int i = 0; i < static_cast<int>(events.size()); ++i)
		{
			Ev& e = events[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			// click: move the cursor here
			if (ImGui::Selectable(clock(context.score, e.tick).c_str(), e.tick == context.currentTick))
				timeline.jumpToTick(context, e.tick);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", getString("stages_jump"));

			row(e);
			easeCell(context, e.ease);

			ImGui::TableNextColumn();
			if (UI::transparentButton(ICON_FA_TIMES, UI::btnSmall))
				erase = i;
			ImGui::PopID();
		}
		ImGui::EndTable();

		if (erase >= 0)
		{
			const Score before = context.score;
			events.erase(events.begin() + erase);
			commit(context, before);
		}
	}

	void StageWindow::draw(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		St::StageSet& set = context.score.stages;

		bool enabled = set.enabled;
		if (ImGui::Checkbox(getString("stages_enable"), &enabled))
		{
			const Score before = context.score;
			set.enabled = enabled;
			commit(context, before);
		}
		ImGui::TextDisabled("%s", getString(set.enabled ? "stages_help" : "stages_enable_help"));
		if (!set.enabled)
			return;

		if (!set.find(St::currentStage))
			St::currentStage = 0;

		ImGui::Separator();
		drawList(context);
		ImGui::Spacing();
		drawSettings(context, timeline);
		drawNow(context);
		ImGui::Separator();
		drawTracks(context, timeline);
	}

	void StageWindow::drawList(ScoreContext& context)
	{
		St::StageSet& set = context.score.stages;

		if (ImGui::BeginTable("##stage_list", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit,
			tableSize(set.stages.size(), 0.3f)))
		{
			ImGui::TableSetupColumn("##chip", ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x);
			ImGui::TableSetupColumn(getString("stages_name"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("##eye", ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x * 1.3f);
			ImGui::TableSetupColumn("##solo", ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x * 1.3f);
			ImGui::TableHeadersRow();

			for (St::Stage& stage : set.stages)
			{
				ImGui::PushID(stage.id);
				ImGui::TableNextRow();
				if (stage.id == St::currentStage)
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));

				ImGui::TableNextColumn();
				if (ImGui::ColorButton("##chip", chipColor(stage.id), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(UI::btnSmall.x * 0.8f, UI::btnSmall.y * 0.8f)))
					St::currentStage = stage.id;

				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputText("##name", &stage.name);
				trackEdit(context);
				if (ImGui::IsItemClicked())
					St::currentStage = stage.id;

				ImGui::TableNextColumn();
				const bool hidden = St::hiddenStages.count(stage.id) != 0;
				if (UI::transparentButton(hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE, UI::btnSmall))
				{
					if (hidden)
						St::hiddenStages.erase(stage.id);
					else
						St::hiddenStages.insert(stage.id);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", getString("stages_show"));

				ImGui::TableNextColumn();
				const bool solo = St::soloStage == stage.id;
				if (solo)
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
				if (UI::transparentButton("S", UI::btnSmall))
					St::soloStage = solo ? -1 : stage.id;
				if (solo)
					ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", getString("stages_solo"));
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("stages_add"), " ").c_str()))
		{
			const Score before = context.score;
			St::currentStage = set.addStage();
			commit(context, before);
		}
		ImGui::SameLine();
		if (ImGui::Button(IO::concat(ICON_FA_TRASH, getString("stages_remove"), " ").c_str()) && St::currentStage != 0)
		{
			// its notes go back to the main stage
			const Score before = context.score;
			const int gone = St::currentStage;
			if (set.removeStage(gone))
			{
				St::hiddenStages.erase(gone);
				if (St::soloStage == gone)
					St::soloStage = -1;
				St::currentStage = 0;
				commit(context, before);
			}
		}
	}

	void StageWindow::drawSettings(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		St::StageSet& set = context.score.stages;
		St::Stage* stage = set.find(St::currentStage);
		if (!stage)
			return;

		if (ImGui::Button(IO::concat(ICON_FA_MOUSE_POINTER, getString("stages_assign"), " ").c_str()) && !context.selectedNotes.empty())
		{
			const Score before = context.score;
			St::assignSelection(context.score, context.selectedNotes, stage->id);
			commit(context, before);
		}
		if (ImGui::Button(getString("stages_select_notes")))
		{
			const std::vector<int> ids = St::notesOfStage(context.score, stage->id);
			context.selectedNotes.clear();
			context.selectedNotes.insert(ids.begin(), ids.end());
		}

		if (!ImGui::CollapsingHeader(getString("stages_settings")))
			return;

		const float itemWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.5f);
		auto number = [&](const char* label, float& value, float speed, float lo, float hi, const char* format) {
			ImGui::SetNextItemWidth(itemWidth);
			ImGui::DragFloat(label, &value, speed, lo, hi, format);
			trackEdit(context);
		};
		auto toggle = [&](const char* label, bool& value) {
			bool v = value;
			if (ImGui::Checkbox(label, &v))
			{
				const Score before = context.score;
				value = v;
				commit(context, before);
			}
		};

		ImGui::SetNextItemWidth(itemWidth);
		ImGui::DragInt(getString("stages_lanes"), &stage->laneCount, 0.1f, 1, 24);
		trackEdit(context);
		toggle(getString("stages_from_start"), stage->fromStart);
		ImGui::SameLine();
		toggle(getString("stages_until_end"), stage->untilEnd);
		toggle(getString("stages_isolated_sim"), stage->isolatedSimLines);

		ImGui::TextDisabled("%s", getString("stages_base"));
		number(IO::concat(getString("stages_track_mask"), getString("stages_c_lane"), " ").c_str(), stage->baseMask.lane, 0.05f, -24.0f, 24.0f, "%.2f");
		number(getString("stages_c_size"), stage->baseMask.size, 0.05f, 0.0f, 24.0f, "%.2f");
		number(IO::concat(getString("stages_track_pivot"), getString("stages_c_lane"), " ").c_str(), stage->basePivot.lane, 0.05f, -24.0f, 24.0f, "%.2f");
		number(getString("stages_c_notea"), stage->baseStyle.noteAlpha, 0.01f, 0.0f, 1.0f, "%.2f");
		number(getString("stages_c_lanea"), stage->baseStyle.laneAlpha, 0.01f, 0.0f, 1.0f, "%.2f");
		number(getString("stages_c_rotate"), stage->baseTransform.rotate, 0.5f, -3600.0f, 3600.0f, "%.1f\xc2\xb0");
		number("X##base", stage->baseTransform.x, 0.05f, -100.0f, 100.0f, "%.2f");
		number("Y##base", stage->baseTransform.y, 0.05f, -100.0f, 100.0f, "%.2f");
		(void)timeline;
	}

	void StageWindow::drawNow(const ScoreContext& context)
	{
		const St::StageState now = St::evaluate(context.score.stages, St::currentStage, context.currentTick);
		ImGui::TextDisabled("%s: x %.2f  y %.2f  %.1f\xc2\xb0  [%.1f .. %.1f]%s", getString("stages_now"), now.x, now.y, now.rotate,
			now.maskLane - now.maskHalfWidth, now.maskLane + now.maskHalfWidth, now.visible ? "" : "  (hidden)");
	}

	void StageWindow::drawTracks(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		if (!ImGui::BeginTabBar("##stage_tracks"))
			return;
		const int id = St::currentStage;

		if (ImGui::BeginTabItem(getString("stages_track_mask")))
		{
			drawMask(context, timeline, id);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(getString("stages_track_pivot")))
		{
			drawPivot(context, timeline, id);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(getString("stages_track_style")))
		{
			drawStyle(context, timeline, id);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(getString("stages_track_transform")))
		{
			drawMove(context, timeline, id);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(getString("stages_track_camera")))
		{
			drawCamera(context, timeline);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	void StageWindow::drawMask(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId)
	{
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("stages_key_at_cursor"), " ").c_str()))
		{
			const Score before = context.score;
			if (St::insertMaskAt(context.score.stages, stageId, context.currentTick))
				commit(context, before);
		}
		St::Stage* stage = context.score.stages.find(stageId);
		if (!stage)
			return;
		jointTable(context, timeline, "##mask", stage->mask,
			{ { getString("stages_c_lane"), numberColumnWidth() }, { getString("stages_c_size"), numberColumnWidth() }, { getString("stages_c_clip"), numberColumnWidth() } },
			[&](St::MaskEvent& e) {
				dragCell(context, "##lane", e.lane, 0.05f, -24.0f, 24.0f, "%.2f");
				dragCell(context, "##size", e.size, 0.05f, 0.0f, 24.0f, "%.2f");
				checkCell(context, "##clip", e.maskNotes);
			});
	}

	void StageWindow::drawPivot(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId)
	{
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("stages_key_at_cursor"), " ").c_str()))
		{
			const Score before = context.score;
			if (St::insertPivotAt(context.score.stages, stageId, context.currentTick))
				commit(context, before);
		}
		St::Stage* stage = context.score.stages.find(stageId);
		if (!stage)
			return;
		jointTable(context, timeline, "##pivot", stage->pivot,
			{ { getString("stages_c_lane"), numberColumnWidth() }, { getString("stages_c_divsize"), numberColumnWidth() }, { getString("stages_c_parity"), wordColumnWidth("Even") },
				{ getString("stages_c_yoff"), numberColumnWidth() }, { getString("stages_c_ybeat"), numberColumnWidth() } },
			[&](St::PivotEvent& e) {
				dragCell(context, "##lane", e.lane, 0.05f, -24.0f, 24.0f, "%.2f");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::DragInt("##div", &e.divisionSize, 0.1f, 1, 12);
				trackEdit(context);
				comboCell(context, "##parity", e.divisionParity, parities, 2);
				dragCell(context, "##yoff", e.yOffset, 0.01f, -10.0f, 10.0f, "%.2f");
				dragCell(context, "##ybeat", e.yBeatOffset, 0.01f, -32.0f, 32.0f, "%.2f");
			});
	}

	void StageWindow::drawStyle(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId)
	{
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("stages_key_at_cursor"), " ").c_str()))
		{
			const Score before = context.score;
			if (St::insertStyleAt(context.score.stages, stageId, context.currentTick))
				commit(context, before);
		}
		St::Stage* stage = context.score.stages.find(stageId);
		if (!stage)
			return;
		const float word = wordColumnWidth("Neutral");
		jointTable(context, timeline, "##style", stage->style,
			{ { getString("stages_c_color"), word }, { getString("stages_c_line"), word }, { getString("stages_c_lborder"), word }, { getString("stages_c_rborder"), word },
				{ getString("stages_c_full"), numberColumnWidth() }, { getString("stages_c_notea"), numberColumnWidth() }, { getString("stages_c_lanea"), numberColumnWidth() },
				{ getString("stages_c_linea"), numberColumnWidth() }, { getString("stages_c_diva"), numberColumnWidth() } },
			[&](St::StyleEvent& e) {
				comboCell(context, "##color", e.judgeLineColor, judgeColors, 8);
				comboCell(context, "##line", e.judgeLineStyle, lineStyles, 2);
				comboCell(context, "##lb", e.leftBorder, borders, 4);
				comboCell(context, "##rb", e.rightBorder, borders, 4);
				checkCell(context, "##full", e.fullWidth);
				dragCell(context, "##na", e.noteAlpha, 0.01f, 0.0f, 1.0f, "%.2f");
				dragCell(context, "##la", e.laneAlpha, 0.01f, 0.0f, 1.0f, "%.2f");
				dragCell(context, "##ja", e.judgeLineAlpha, 0.01f, 0.0f, 1.0f, "%.2f");
				dragCell(context, "##da", e.divisionLineAlpha, 0.01f, 0.0f, 1.0f, "%.2f");
			});
	}

	void StageWindow::drawMove(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId)
	{
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("stages_key_at_cursor"), " ").c_str()))
		{
			const Score before = context.score;
			if (St::insertTransformAt(context.score.stages, stageId, context.currentTick))
				commit(context, before);
		}
		St::Stage* stage = context.score.stages.find(stageId);
		if (!stage)
			return;
		jointTable(context, timeline, "##move", stage->transform,
			{ { getString("stages_c_rotate"), numberColumnWidth() }, { getString("stages_c_x"), numberColumnWidth() }, { getString("stages_c_y"), numberColumnWidth() },
				{ getString("stages_c_elev"), numberColumnWidth() }, { getString("stages_c_anchor"), wordColumnWidth("Judge line") } },
			[&](St::TransformEvent& e) {
				dragCell(context, "##rot", e.rotate, 0.5f, -3600.0f, 3600.0f, "%.1f\xc2\xb0");
				dragCell(context, "##x", e.x, 0.05f, -100.0f, 100.0f, "%.2f");
				dragCell(context, "##y", e.y, 0.05f, -100.0f, 100.0f, "%.2f");
				dragCell(context, "##elev", e.elevation, 0.05f, -100.0f, 100.0f, "%.2f");
				comboCell(context, "##anchor", e.anchor, anchors, 2);
			});
	}

	void StageWindow::drawCamera(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("stages_key_at_cursor"), " ").c_str()))
		{
			const Score before = context.score;
			St::insertCameraAt(context.score.stages, context.currentTick);
			commit(context, before);
		}
		jointTable(context, timeline, "##camera", context.score.stages.camera,
			{ { getString("stages_c_lane"), numberColumnWidth() }, { getString("stages_c_size"), numberColumnWidth() }, { getString("stages_c_zoom"), numberColumnWidth() },
				{ getString("stages_c_zlane"), numberColumnWidth() }, { getString("stages_c_zy"), numberColumnWidth() }, { getString("stages_c_align"), wordColumnWidth("Bottom") },
				{ getString("stages_c_rotate"), numberColumnWidth() }, { getString("stages_c_tilt"), numberColumnWidth() } },
			[&](St::CameraEvent& e) {
				dragCell(context, "##lane", e.lane, 0.05f, -24.0f, 24.0f, "%.2f");
				dragCell(context, "##size", e.size, 0.05f, 0.5f, 24.0f, "%.2f");
				dragCell(context, "##zoom", e.zoom, 0.01f, 0.1f, 10.0f, "%.2fx");
				dragCell(context, "##zlane", e.zoomTargetLane, 0.05f, -24.0f, 24.0f, "%.2f");
				dragCell(context, "##zy", e.zoomTargetY, 0.01f, -1.0f, 1.0f, "%.2f");
				comboCell(context, "##align", e.zoomVerticalAlign, aligns, 2);
				dragCell(context, "##rot", e.rotate, 0.5f, -3600.0f, 3600.0f, "%.1f\xc2\xb0");
				dragCell(context, "##tilt", e.stageTilt, 0.01f, 0.0f, 1.0f, "%.2f");
			});
	}
}
