#include "AutoFixWindow.h"
#include "../../ApplicationConfiguration.h"
#include "../../ImGui/imgui.h"
#include "../../IO.h"
#include "../../Localization.h"
#include "../../ScoreContext.h"
#include "../../UI.h"
#include <algorithm>
#include <chrono>

using namespace Canvas::Fixes;

namespace MikuMikuWorld
{
	namespace
	{
		struct GroupRow
		{
			int bit;
			const char* labelKey;
		};

		const GroupRow rows[] = {
			{ FixOverlaps, "auto_fix_overlaps" },
			{ FixHolds, "auto_fix_holds" },
			{ FixHiSpeed, "auto_fix_hispeed" },
			{ FixSkillFever, "auto_fix_skill_fever" },
			{ FixTiming, "auto_fix_timing" },
		};

		int bitIndex(int bit)
		{
			int index = 0;
			while ((bit >> index) > 1)
				++index;
			return index;
		}

		int changesOf(const AutoFixReport& report, int bit)
		{
			switch (bit)
			{
			case FixOverlaps: return report.overlaps;
			case FixHolds: return report.holds;
			case FixHiSpeed: return report.hiSpeed;
			case FixSkillFever: return report.skillFever;
			default: return report.timing;
			}
		}
	}

	std::string AutoFixWindow::stageText()
	{
		std::lock_guard<std::mutex> lock(stageLock);
		return stage;
	}

	void AutoFixWindow::setStage(const std::string& text)
	{
		std::lock_guard<std::mutex> lock(stageLock);
		stage = text;
	}

	void AutoFixWindow::open(const ScoreContext& context)
	{
		if (state == State::Running || state == State::Scanning)
		{
			openRequested = true;
			return;
		}

		musicSeconds = const_cast<ScoreContext&>(context).audio.getMusicLength();
		report = {};
		state = State::Scanning;
		const Score snapshot = context.score;
		const double seconds = musicSeconds > 0 ? musicSeconds : -1.0;
		scanJob = std::async(std::launch::async, [snapshot, seconds]() { return previewAutoFix(snapshot, seconds); });
		openRequested = true;
	}

	void AutoFixWindow::startFix(const ScoreContext& context)
	{
		AutoFixOptions options;
		options.groups = config.autoFixMask & FixAll;
		options.aggressiveHiSpeed = config.autoFixAggressiveHiSpeed;
		options.musicDurationSeconds = musicSeconds > 0 ? musicSeconds : -1.0;
		const Score snapshot = context.score;
		setStage("");
		state = State::Running;
		fixJob = std::async(std::launch::async, [this, snapshot, options]() mutable {
			Output output;
			output.score = snapshot;
			output.report = autoFix(output.score, options, [this](const std::string& text) { setStage(text); });
			return output;
		});
	}

	std::optional<AutoFixWindow::Output> AutoFixWindow::update(ScoreContext& context)
	{
		const char* id = "###auto_fix_chart";
		std::optional<Output> finished;

		if (openRequested)
		{
			ImGui::OpenPopup(id);
			openRequested = false;
		}

		// workers
		if (state == State::Scanning && scanJob.valid() && scanJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			preview = scanJob.get();
			state = State::Ready;
		}
		if (state == State::Ready && startWhenReady)
		{
			startWhenReady = false;
			startFix(context);
		}
		if (state == State::Running && fixJob.valid() && fixJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			Output output = fixJob.get();
			report = output.report;
			state = State::Done;
			if (output.report.changed())
				finished = std::move(output);
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		const std::string title = IO::formatString("%s%s", getString("auto_fix_title"), id);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return finished;

		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::PushTextWrapPos(width);
		ImGui::TextDisabled("%s", getString("auto_fix_note"));
		ImGui::PopTextWrapPos();
		ImGui::Separator();

		if (state == State::Scanning)
		{
			ImGui::TextUnformatted(getString("auto_fix_scanning"));
			ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0));
		}
		else if (state == State::Running)
		{
			ImGui::TextUnformatted(getString("auto_fix_running"));
			const std::string label = stageText();
			if (!label.empty())
				ImGui::TextDisabled("%s", label.c_str());
			ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0));
		}
		else
		{
			const bool done = state == State::Done;
			for (const GroupRow& row : rows)
			{
				const int found = preview.perGroup[bitIndex(row.bit)];
				bool on = (config.autoFixMask & row.bit) != 0;
				std::string label = IO::formatString("%s", getString(row.labelKey));
				if (done)
					label += IO::formatString("   %d", changesOf(report, row.bit));
				else
					label += IO::formatString("   (%d)", found);
				label += IO::formatString("###fix_%d", row.bit);
				ImGui::BeginDisabled(done);
				if (ImGui::Checkbox(label.c_str(), &on))
					config.autoFixMask = on ? (config.autoFixMask | row.bit) : (config.autoFixMask & ~row.bit);
				ImGui::EndDisabled();
			}

			ImGui::BeginDisabled(done || !(config.autoFixMask & FixHiSpeed));
			ImGui::Indent();
			ImGui::Checkbox(getString("auto_fix_hispeed_aggressive"), &config.autoFixAggressiveHiSpeed);
			UI::tooltip(getString("auto_fix_hispeed_aggressive_tooltip"));
			ImGui::Unindent();
			ImGui::EndDisabled();

			if (!done)
			{
				ImGui::Separator();
				ImGui::TextDisabled("%s", IO::formatString(getString("auto_fix_found_summary"), preview.errors, preview.warnings, preview.infos).c_str());
			}
			else
			{
				ImGui::Separator();
				ImGui::TextUnformatted(report.changed()
					? IO::formatString(getString("auto_fix_result"), report.total(), report.after).c_str()
					: getString("auto_fix_nothing"));
				ImGui::TextDisabled("%s", IO::formatString(getString("auto_fix_before_after"), report.before, report.after).c_str());
				if (!report.log.empty())
				{
					ImGui::BeginChild("##auto_fix_log", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8.0f), true);
					for (size_t i = 0; i < report.log.size() && i < 300; ++i)
					{
						ImGui::Bullet();
						ImGui::TextWrapped("%s", report.log[i].c_str());
					}
					if (report.log.size() > 300)
						ImGui::TextDisabled("...");
					ImGui::EndChild();
				}
			}
		}

		ImGui::Separator();
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
		if (state == State::Ready)
		{
			ImGui::BeginDisabled((config.autoFixMask & FixAll) == 0);
			if (ImGui::Button(getString("auto_fix_start"), ImVec2(half, 0)))
				startFix(context);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button(getString("cancel"), ImVec2(half, 0)))
				ImGui::CloseCurrentPopup();
		}
		else if (state == State::Done)
		{
			if (ImGui::Button(getString("auto_fix_close"), ImVec2(-1, 0)))
			{
				state = State::Ready;
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			ImGui::BeginDisabled();
			ImGui::Button(getString("auto_fix_close"), ImVec2(-1, 0));
			ImGui::EndDisabled();
		}

		ImGui::EndPopup();
		return finished;
	}
}
