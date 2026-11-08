#include "AnimationWindow.h"
#include "../../UI.h"
#include "../../Constants.h"
#include "../../Localization.h"
#include "../../ScoreEditorTimeline.h"
#include "../../IconsFontAwesome5.h"
#include "../../ImGui/imgui.h"
#include <algorithm>

namespace Anim = Canvas::Animation;

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

		// tables scroll sideways in a narrow panel; this keeps them only as tall as their rows (capped) so
		// the keys table never pushes the effects off the bottom
		ImVec2 tableSize(size_t rows, float share)
		{
			const float wanted = (static_cast<float>(rows) + 1.0f) * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ScrollbarSize + 4.0f;
			return ImVec2(0.0f, std::min(wanted, std::max(ImGui::GetFrameHeight() * 4.0f, ImGui::GetContentRegionAvail().y * share)));
		}

		float timeColumnWidth() { return ImGui::CalcTextSize("00:00.00").x + ImGui::GetStyle().CellPadding.x * 2.0f; }
		// fixed widths: with sideways scrolling, stretched columns would squash the drag boxes to nothing
		float numberColumnWidth() { return ImGui::CalcTextSize("-0000.00x").x + ImGui::GetStyle().FramePadding.x * 2.0f; }
		float easeColumnWidth() { return ImGui::CalcTextSize("Ease in-out").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f; }

		bool easeCombo(const char* id, Anim::Ease& ease)
		{
			bool changed = false;
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo(id, Anim::easeName(ease)))
			{
				for (int i = 0; i < static_cast<int>(Anim::Ease::Count); ++i)
					if (ImGui::Selectable(Anim::easeName(static_cast<Anim::Ease>(i)), static_cast<int>(ease) == i))
					{
						ease = static_cast<Anim::Ease>(i);
						changed = true;
					}
				ImGui::EndCombo();
			}
			return changed;
		}
	}

	void AnimationWindow::trackEdit(ScoreContext& context)
	{
		if (ImGui::IsItemActivated())
			editStart = context.score;
		if (ImGui::IsItemDeactivatedAfterEdit() && editStart)
		{
			context.score.animation.sort();
			commit(context, *editStart);
			editStart.reset();
		}
		else if (ImGui::IsItemDeactivated())
			editStart.reset();
	}

	void AnimationWindow::commit(ScoreContext& context, const Score& before)
	{
		context.pushHistory(getString("preview_animation"), before, context.score);
	}

	void AnimationWindow::draw(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		ImGui::TextWrapped("%s", getString("anim_help"));
		ImGui::Separator();
		drawKeys(context, timeline);
		ImGui::Spacing();
		ImGui::Separator();
		drawEffects(context, timeline);
	}

	void AnimationWindow::drawKeys(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		const auto& targets = Anim::targets();
		if (!targetPicked)
		{
			for (int i = 0; i < static_cast<int>(targets.size()); ++i)
				if (const Anim::Track* t = context.score.animation.track(targets[i].id); t && !t->keys.empty())
				{
					target = i;
					break;
				}
			targetPicked = true;
		}
		target = std::clamp(target, 0, static_cast<int>(targets.size()) - 1);
		const std::string id = targets[target].id;

		// the target list says which parts already move
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		if (ImGui::BeginCombo("##anim_target", getString(targets[target].label)))
		{
			for (int i = 0; i < static_cast<int>(targets.size()); ++i)
			{
				const Anim::Track* t = context.score.animation.track(targets[i].id);
				const std::string label = t && !t->keys.empty()
					? IO::formatString("%s (%d)", getString(targets[i].label), static_cast<int>(t->keys.size()))
					: std::string(getString(targets[i].label));
				if (ImGui::Selectable(label.c_str(), i == target))
					target = i;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("anim_add_key"), " ").c_str()))
		{
			// starts from what the part looks like right now, so adding a key never makes it jump
			const Score before = context.score;
			const Anim::Transform now = Anim::evaluate(context.score.animation, id, context.currentTick);
			Anim::Key key;
			key.tick = context.currentTick;
			key.x = now.x;
			key.y = now.y;
			key.scale = now.scale;
			key.rotation = now.rotation;
			key.opacity = now.opacity;
			context.score.animation.setKey(id, key);
			commit(context, before);
		}

		Anim::Track* track = nullptr;
		for (Anim::Track& t : context.score.animation.tracks)
			if (t.target == id)
				track = &t;
		if (!track || track->keys.empty())
		{
			ImGui::TextDisabled("%s", getString("anim_no_keys"));
			return;
		}

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
		const bool hud = targets[target].hud;
		if (!ImGui::BeginTable("##anim_keys", 8, flags, tableSize(track->keys.size(), 0.55f)))
			return;
		ImGui::TableSetupColumn(getString("anim_time"), ImGuiTableColumnFlags_WidthFixed, timeColumnWidth());
		for (const char* name : { "X", "Y", getString("anim_scale"), getString("anim_turn"), getString("anim_opacity") })
			ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthFixed, numberColumnWidth());
		ImGui::TableSetupColumn(getString("anim_ease"), ImGuiTableColumnFlags_WidthFixed, easeColumnWidth());
		ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x);
		ImGui::TableHeadersRow();

		int erase = -1;
		for (int i = 0; i < static_cast<int>(track->keys.size()); ++i)
		{
			Anim::Key& k = track->keys[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			// click: move the cursor here
			if (ImGui::Selectable(clock(context.score, k.tick).c_str(), k.tick == context.currentTick))
				timeline.jumpToTick(context, k.tick);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", getString("anim_jump"));

			auto cell = [&](const char* label, float& value, float speed, float lo, float hi, const char* format) {
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::DragFloat(label, &value, speed, lo, hi, format);
				trackEdit(context);
			};
			cell("##x", k.x, 1.0f, -4000.0f, 4000.0f, "%.0f");
			cell("##y", k.y, 1.0f, -4000.0f, 4000.0f, "%.0f");
			cell("##scale", k.scale, 0.01f, 0.0f, 10.0f, "%.2fx");
			cell("##rot", k.rotation, 0.5f, -3600.0f, 3600.0f, "%.0f\xc2\xb0");

			ImGui::TableNextColumn();
			if (hud)
			{
				float percent = k.opacity * 100.0f;
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::DragFloat("##opacity", &percent, 1.0f, 0.0f, 100.0f, "%.0f%%"))
					k.opacity = std::clamp(percent / 100.0f, 0.0f, 1.0f);
				trackEdit(context);
			}
			else
				ImGui::TextDisabled("-");

			ImGui::TableNextColumn();
			Anim::Ease ease = k.ease;
			if (easeCombo("##ease", ease))
			{
				const Score before = context.score;
				track->keys[i].ease = ease;
				commit(context, before);
			}

			ImGui::TableNextColumn();
			if (UI::transparentButton(ICON_FA_TIMES, UI::btnSmall))
				erase = i;

			ImGui::PopID();
		}
		ImGui::EndTable();

		if (erase >= 0)
		{
			const Score before = context.score;
			Anim::Track& t = context.score.animation.trackFor(id);
			t.keys.erase(t.keys.begin() + erase);
			context.score.animation.sort();
			commit(context, before);
		}
	}

	void AnimationWindow::drawEffects(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		ImGui::TextUnformatted(getString("anim_effects"));
		for (int kind = 0; kind < static_cast<int>(Anim::EffectKind::Count); ++kind)
		{
			if (kind > 0)
				ImGui::SameLine();
			const std::string label = IO::concat(ICON_FA_PLUS, getString(Anim::effectName(static_cast<Anim::EffectKind>(kind))), " ");
			if (ImGui::Button(label.c_str()))
			{
				const Score before = context.score;
				Anim::Effect fx;
				fx.kind = static_cast<Anim::EffectKind>(kind);
				fx.tick = context.currentTick;
				if (fx.kind == Anim::EffectKind::Flash)
					fx.duration = 0.35f;
				if (fx.kind == Anim::EffectKind::Fade)
				{
					// a fade to black over a second; another fade with 0% brings the picture back
					fx.duration = 1.0f;
					fx.strength = 1.0f;
					fx.color = 0xff000000u;
				}
				context.score.animation.effects.push_back(fx);
				context.score.animation.sort();
				commit(context, before);
			}
		}

		auto& effects = context.score.animation.effects;
		if (effects.empty())
			return;

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
		if (!ImGui::BeginTable("##anim_effects", 6, flags, tableSize(effects.size(), 1.0f)))
			return;
		ImGui::TableSetupColumn(getString("anim_time"), ImGuiTableColumnFlags_WidthFixed, timeColumnWidth());
		ImGui::TableSetupColumn(getString("anim_kind"), ImGuiTableColumnFlags_WidthFixed, numberColumnWidth());
		ImGui::TableSetupColumn(getString("anim_length"), ImGuiTableColumnFlags_WidthFixed, numberColumnWidth());
		ImGui::TableSetupColumn(getString("anim_strength"), ImGuiTableColumnFlags_WidthFixed, numberColumnWidth());
		ImGui::TableSetupColumn(getString("anim_colour"), ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x * 1.5f);
		ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, UI::btnSmall.x);
		ImGui::TableHeadersRow();

		int erase = -1;
		for (int i = 0; i < static_cast<int>(effects.size()); ++i)
		{
			Anim::Effect& fx = effects[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			if (ImGui::Selectable(clock(context.score, fx.tick).c_str(), fx.tick == context.currentTick))
				timeline.jumpToTick(context, fx.tick);

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(getString(Anim::effectName(fx.kind)));

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::DragFloat("##len", &fx.duration, 0.01f, 0.01f, 30.0f, "%.2fs");
			trackEdit(context);

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (fx.kind == Anim::EffectKind::Shake)
				ImGui::DragFloat("##str", &fx.strength, 0.5f, 0.0f, 400.0f, "%.0f px");
			else if (fx.kind == Anim::EffectKind::Fade)
			{
				float percent = fx.strength * 100.0f;
				if (ImGui::DragFloat("##str", &percent, 1.0f, 0.0f, 100.0f, "%.0f%%"))
					fx.strength = std::clamp(percent / 100.0f, 0.0f, 1.0f);
			}
			else
				ImGui::TextDisabled("-");
			if (fx.kind != Anim::EffectKind::Flash)
				trackEdit(context);

			ImGui::TableNextColumn();
			if (fx.kind != Anim::EffectKind::Shake)
			{
				ImVec4 colour = ImGui::ColorConvertU32ToFloat4(fx.color);
				if (ImGui::ColorEdit4("##colour", &colour.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf))
					fx.color = ImGui::ColorConvertFloat4ToU32(colour);
				trackEdit(context);
			}

			ImGui::TableNextColumn();
			if (UI::transparentButton(ICON_FA_TIMES, UI::btnSmall))
				erase = i;
			ImGui::PopID();
		}
		ImGui::EndTable();

		if (erase >= 0)
		{
			const Score before = context.score;
			effects.erase(effects.begin() + erase);
			commit(context, before);
		}
	}
}
