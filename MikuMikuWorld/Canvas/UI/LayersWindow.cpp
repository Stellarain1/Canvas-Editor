#include "LayersWindow.h"
#include "../../UI.h"
#include "../../ApplicationConfiguration.h"
#include "../../IconsFontAwesome5.h"
#include <algorithm>
#include <cstdio>

namespace MikuMikuWorld
{
	void LayersWindow::removeLayer(ScoreContext& context, int layer)
	{
		if (layer <= 0 || layer >= static_cast<int>(context.score.layers.size()))
			return;

		Score prev = context.score;

		// notes fall back to the default layer, the layer's own hi-speed changes go away,
		// and everything above shifts down one index
		for (auto& [id, note] : context.score.notes)
		{
			if (note.layer == layer)
				note.layer = 0;
			else if (note.layer > layer)
				--note.layer;
		}

		auto& changes = context.score.hiSpeedChanges;
		changes.erase(std::remove_if(changes.begin(), changes.end(), [layer](const HiSpeedChange& c) { return c.layer == layer; }), changes.end());
		for (auto& change : changes)
			if (change.layer > layer)
				--change.layer;

		context.score.layers.erase(context.score.layers.begin() + layer);
		context.currentLayer = context.clampLayer(context.currentLayer >= layer ? context.currentLayer - 1 : context.currentLayer);
		context.pushHistory("Remove layer", prev, context.score);
	}

	void LayersWindow::update(ScoreContext& context)
	{
		auto& layers = context.score.layers;
		if (layers.empty())
			layers.push_back({ "default" });

		context.currentLayer = context.clampLayer(context.currentLayer);

		ImGui::Checkbox(getString("dim_other_layers"), &config.dimOtherLayers);
		UI::tooltip(getString("dim_other_layers_tooltip"));
		ImGui::Separator();

		int removeIndex = -1;
		const float rowButton = ImGui::GetFrameHeight();
		for (int i = 0; i < static_cast<int>(layers.size()); ++i)
		{
			ImGui::PushID(i);
			HiSpeedLayer& layer = layers[i];

			if (ImGui::RadioButton("##current", context.currentLayer == i))
				context.currentLayer = i;
			UI::tooltip(getString("layer_make_current"));
			ImGui::SameLine();

			if (ImGui::Button(layer.hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE, { rowButton, rowButton }))
			{
				Score prev = context.score;
				layer.hidden = !layer.hidden;
				context.pushHistory(layer.hidden ? "Hide layer" : "Show layer", prev, context.score);
			}
			UI::tooltip(getString("layer_toggle_visibility"));
			ImGui::SameLine();

			if (renamingLayer == i)
			{
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - rowButton * 2 - ImGui::GetStyle().ItemSpacing.x * 2);
				if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
					ImGui::SetKeyboardFocusHere();
				const bool done = ImGui::InputText("##rename", renameBuffer.data(), renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (done || ImGui::IsItemDeactivated())
				{
					if (renameBuffer[0] != '\0' && layer.name != renameBuffer.data())
					{
						Score prev = context.score;
						layer.name = renameBuffer.data();
						context.pushHistory("Rename layer", prev, context.score);
					}
					renamingLayer = -1;
				}
			}
			else
			{
				int noteCount = 0;
				for (const auto& [id, note] : context.score.notes)
					noteCount += note.layer == i && (note.getType() == NoteType::Tap || note.getType() == NoteType::Hold);
				int speedCount = 0;
				for (const auto& change : context.score.hiSpeedChanges)
					speedCount += change.layer == i;

				ImGui::BeginDisabled(layer.hidden);
				ImGui::Text("%s", layer.name.c_str());
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					renamingLayer = i;
					std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", layer.name.c_str());
				}
				ImGui::SameLine();
				ImGui::TextDisabled("(%d / %d)", noteCount, speedCount);
				UI::tooltip(getString("layer_counts_tooltip"));
			}

			ImGui::SameLine(ImGui::GetContentRegionMax().x - rowButton * 2 - ImGui::GetStyle().ItemSpacing.x);
			ImGui::BeginDisabled(context.selectedNotes.empty());
			if (ImGui::Button(ICON_FA_SIGN_IN_ALT, { rowButton, rowButton }))
				context.moveSelectionToLayer(i);
			ImGui::EndDisabled();
			UI::tooltip(getString("layer_move_selection_here"));

			ImGui::SameLine();
			ImGui::BeginDisabled(i == 0);
			if (ImGui::Button(ICON_FA_TRASH, { rowButton, rowButton }))
				removeIndex = i;
			ImGui::EndDisabled();
			UI::tooltip(getString(i == 0 ? "layer_default_cannot_remove" : "layer_remove"));

			ImGui::PopID();
		}

		if (removeIndex > 0)
			removeLayer(context, removeIndex);

		ImGui::Separator();
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("layer_add"), " ").c_str(), { -1, 0 }))
		{
			Score prev = context.score;
			layers.push_back({ IO::formatString("Layer %d", static_cast<int>(layers.size())) });
			context.currentLayer = static_cast<int>(layers.size()) - 1;
			context.pushHistory("Add layer", prev, context.score);
		}

		if (layers.size() > 1)
			ImGui::TextWrapped("%s", getString("layer_game_note"));
	}
}
