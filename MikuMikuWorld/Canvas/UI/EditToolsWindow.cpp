#include "EditToolsWindow.h"
#include "../../UI.h"
#include "../../ScoreContext.h"
#include "../../Constants.h"
#include "../../Tempo.h"
#include "../../ImGui/imgui_stdlib.h"
#include <algorithm>

using namespace MikuMikuWorld;

namespace Canvas
{
	namespace
	{
		const char* stepTypeKeys[] = { "normal", "hidden", "skip" };
		const char* speedEaseKeys[] = { "ease_linear", "ease_in", "ease_out", "ease_in_out" };
	}

	void EditToolsWindow::openRepeatMids(const ScoreContext& context, int division)
	{
		const std::vector<int> selected(context.selectedNotes.begin(), context.selectedNotes.end());
		holdId = Tools::holdOfSelection(context.score, selected);
		patternMids.clear();
		for (int id : selected)
		{
			auto it = context.score.notes.find(id);
			if (it != context.score.notes.end() && it->second.getType() == NoteType::HoldMid)
				patternMids.push_back(id);
		}
		// no mids picked: lay them along the hold every division step instead
		repeat.mode = patternMids.empty() ? Tools::RepeatMidsOptions::Mode::EveryStep : Tools::RepeatMidsOptions::Mode::Pattern;
		everyDivision = std::clamp(division, 4, Tools::MAX_DIVISION);
		status.clear();
		repeatRequested = true;
	}

	void EditToolsWindow::openInterpolate(const ScoreContext& context, const std::unordered_set<int>& selectedHiSpeedTicks)
	{
		std::vector<int> ticks(selectedHiSpeedTicks.begin(), selectedHiSpeedTicks.end());
		std::sort(ticks.begin(), ticks.end());
		interpolate.layer = context.currentLayer;
		if (ticks.size() >= 2)
		{
			interpolate.fromTick = ticks.front();
			interpolate.toTick = ticks.back();
		}
		else
		{
			interpolate.fromTick = context.currentTick;
			interpolate.toTick = context.currentTick + TICKS_PER_BEAT * 4;
		}
		interpolate.fromSpeed = Tools::hiSpeedAt(context.score, interpolate.fromTick, interpolate.layer);
		interpolate.toSpeed = ticks.size() >= 2 ? Tools::hiSpeedAt(context.score, interpolate.toTick, interpolate.layer) : interpolate.fromSpeed * 2.0f;
		status.clear();
		interpolateRequested = true;
	}

	void EditToolsWindow::update(ScoreContext& context)
	{
		drawRepeat(context);
		drawInterpolate(context);
	}

	void EditToolsWindow::drawRepeat(ScoreContext& context)
	{
		const char* id = "###repeat_hold_mids";
		if (repeatRequested)
		{
			ImGui::OpenPopup(id);
			repeatRequested = false;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		const std::string title = IO::formatString("%s%s", getString("repeat_hold_mids_title"), id);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (holdId < 0)
		{
			ImGui::TextUnformatted(getString("repeat_needs_hold"));
			if (ImGui::Button("OK", ImVec2(-1, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		int mode = static_cast<int>(repeat.mode);
		ImGui::BeginDisabled(patternMids.empty());
		ImGui::RadioButton(getString("repeat_mode_pattern"), &mode, 0);
		ImGui::EndDisabled();
		ImGui::RadioButton(getString("repeat_mode_every"), &mode, 1);
		repeat.mode = static_cast<Tools::RepeatMidsOptions::Mode>(mode);
		ImGui::Separator();

		UI::beginPropertyColumns();
		if (repeat.mode == Tools::RepeatMidsOptions::Mode::Pattern)
		{
			int until = repeat.untilEnd ? 0 : 1;
			static const char* untilKeys[] = { "repeat_until_end", "repeat_until_count" };
			UI::addSelectProperty(getString("repeat_until"), until, untilKeys, 2);
			repeat.untilEnd = until == 0;
			if (!repeat.untilEnd)
				UI::addIntProperty(getString("repeat_count"), repeat.count, 1, 512);
			UI::addCheckboxProperty(getString("repeat_mirror"), repeat.mirror);
		}
		else
		{
			UI::propertyLabel(getString("division"));
			static const int divisions[] = { 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 384, 768, 1920 };
			UI::divisionSelect(getString("division"), everyDivision, divisions, sizeof(divisions) / sizeof(int));
			ImGui::NextColumn();
			UI::addSelectProperty(getString("repeat_step_type"), repeat.stepType, stepTypeKeys, 3);
		}
		UI::endPropertyColumns();

		if (!status.empty())
			ImGui::TextDisabled("%s", status.c_str());
		ImGui::Separator();
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
		if (ImGui::Button(getString("apply"), ImVec2(half, 0)))
		{
			repeat.stepTicks = std::max(1, static_cast<int>(std::lround(Tools::divisionStep(everyDivision))));
			Score prev = context.score;
			const auto result = Tools::repeatHoldMids(context.score, holdId, patternMids, repeat);
			if (result.added > 0)
			{
				context.pushHistory("Repeat hold mids", prev, context.score);
				status = IO::formatString(getString("repeat_done"), result.added);
				ImGui::CloseCurrentPopup();
			}
			else
				status = result.error;
		}
		ImGui::SameLine();
		if (ImGui::Button(getString("cancel"), ImVec2(half, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void EditToolsWindow::drawInterpolate(ScoreContext& context)
	{
		const char* id = "###interpolate_hispeed";
		if (interpolateRequested)
		{
			ImGui::OpenPopup(id);
			interpolateRequested = false;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		const std::string title = IO::formatString("%s%s", getString("interpolate_hispeed_title"), id);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::PushTextWrapPos(420);
		ImGui::TextDisabled("%s", getString("interpolate_hispeed_tooltip"));
		ImGui::PopTextWrapPos();
		UI::beginPropertyColumns();
		UI::addIntProperty(getString("interpolate_from"), interpolate.fromTick, 0, 1 << 28);
		UI::addFloatProperty(getString("interpolate_from_speed"), interpolate.fromSpeed, "%.3fx");
		UI::addIntProperty(getString("interpolate_to"), interpolate.toTick, 0, 1 << 28);
		UI::addFloatProperty(getString("interpolate_to_speed"), interpolate.toSpeed, "%.3fx");
		static int stepDivision = 32;
		UI::propertyLabel(getString("interpolate_step"));
		static const int divisions[] = { 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 384, 768, 1920 };
		UI::divisionSelect(getString("interpolate_step"), stepDivision, divisions, sizeof(divisions) / sizeof(int));
		ImGui::NextColumn();
		int ease = static_cast<int>(interpolate.ease);
		UI::addSelectProperty(getString("interpolate_ease"), ease, speedEaseKeys, 4);
		interpolate.ease = static_cast<Tools::SpeedEase>(ease);
		UI::addCheckboxProperty(getString("interpolate_replace"), interpolate.replace);
		UI::endPropertyColumns();

		if (!status.empty())
			ImGui::TextDisabled("%s", status.c_str());
		ImGui::Separator();
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
		if (ImGui::Button(getString("apply"), ImVec2(half, 0)))
		{
			if (interpolate.fromTick == interpolate.toTick)
				status = getString("interpolate_needs_range");
			else
			{
				interpolate.stepTicks = std::max(1, static_cast<int>(std::lround(Tools::divisionStep(stepDivision))));
				Score prev = context.score;
				const int added = Tools::interpolateHiSpeeds(context.score, interpolate);
				context.pushHistory("Interpolate hi-speeds", prev, context.score);
				status = IO::formatString(getString("interpolate_done"), added);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(getString("cancel"), ImVec2(half, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}
