#include "BpmDetectWindow.h"
#include "../../UI.h"
#include "../../Application.h"
#include "../../ScoreContext.h"
#include "../../Constants.h"
#include "../../Tempo.h"
#include "../../IconsFontAwesome5.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace MikuMikuWorld;
namespace BD = Canvas::BeatDetect;

namespace Canvas
{
	namespace
	{
		constexpr const char* SELECT_POPUP = "###bpm_select";
		constexpr const char* INTERP_POPUP = "###bpm_interpolate";
		const char* const STAGES[] = { "bpm_stage_decode", "bpm_stage_resample", "bpm_stage_spectrogram", "bpm_stage_network", "bpm_stage_tempo", "bpm_stage_done" };
		const char* const STAGE_NAMES[] = { "decode", "resample", "spectrogram", "network", "tempo", "done" };
		const char* const STEP_KEYS[] = { "bpm_step_32", "bpm_step_16", "bpm_step_8", "bpm_step_4" };
		const int STEP_TICKS[] = { 60, 120, 240, 480 };
		const char* const CURVE_KEYS[] = { "ease_linear", "ease_in", "ease_out", "ease_in_out" };

		const char* kindKey(BD::Kind kind)
		{
			switch (kind)
			{
			case BD::Kind::HalfTime: return "bpm_kind_half";
			case BD::Kind::DoubleTime: return "bpm_kind_double";
			case BD::Kind::Simple: return "bpm_kind_simple";
			default: return "bpm_kind_best";
			}
		}

		const char* kindTip(BD::Kind kind)
		{
			switch (kind)
			{
			case BD::Kind::HalfTime: return "bpm_kind_half_tip";
			case BD::Kind::DoubleTime: return "bpm_kind_double_tip";
			case BD::Kind::Simple: return "bpm_kind_simple_tip";
			default: return "bpm_kind_best_tip";
			}
		}

		std::string meterText(const BD::Candidate& c)
		{
			std::string text;
			std::vector<int> seen;
			for (const auto& s : c.sigs)
				if (std::find(seen.begin(), seen.end(), s.numerator) == seen.end())
				{
					seen.push_back(s.numerator);
					text += (text.empty() ? "" : ", ") + std::to_string(s.numerator) + "/4";
				}
			if (c.sigChanges > 0)
				text = IO::formatString(getString("bpm_meter_changes"), text.c_str(), c.sigChanges);
			return IO::formatString(getString("bpm_meter"), text.c_str());
		}

		// BPM at the middle of each beat, for the little graph
		std::vector<float> curveOf(const BD::Candidate& c, int samples)
		{
			std::vector<float> out(static_cast<size_t>(samples), static_cast<float>(c.firstBpm));
			if (c.totalBeats <= 0)
				return out;
			for (int i = 0; i < samples; ++i)
			{
				const double beat = (i + 0.5) * c.totalBeats / samples;
				for (const auto& p : c.pieces)
					if (beat >= p.startBeat && beat < p.startBeat + p.beats + 1e-9)
					{
						const double u = p.beats > 0 ? (beat - p.startBeat) / p.beats : 0.0;
						out[static_cast<size_t>(i)] = static_cast<float>(p.bpmStart + (p.bpmEnd - p.bpmStart) * u);
						break;
					}
			}
			return out;
		}

		void drawGraph(ImDrawList* list, ImVec2 min, ImVec2 max, const std::vector<float>& values, float lo, float hi)
		{
			const ImU32 frame = ImGui::GetColorU32(ImGuiCol_Border);
			const ImU32 line = ImGui::GetColorU32(ImGuiCol_PlotLines);
			list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
			if (values.size() >= 2)
			{
				if (hi - lo < 1.0f)
				{
					lo -= 2.0f;
					hi += 2.0f;
				}
				std::vector<ImVec2> points;
				for (size_t i = 0; i < values.size(); ++i)
				{
					const float x = min.x + 4 + (max.x - min.x - 8) * i / (values.size() - 1);
					const float y = max.y - 4 - (max.y - min.y - 8) * (values[i] - lo) / (hi - lo);
					points.push_back({ x, y });
				}
				list->AddPolyline(points.data(), static_cast<int>(points.size()), line, ImDrawFlags_None, 2.0f);
			}
			list->AddRect(min, max, frame, 3.0f);
		}
	}

	BpmDetectWindow::~BpmDetectWindow()
	{
		stop();
	}

	void BpmDetectWindow::stop()
	{
		if (job)
			job->cancel = true;
		if (worker.joinable())
			worker.join();
		job.reset();
	}

	void BpmDetectWindow::start()
	{
		stop();
		candidates.clear();
		error.clear();
		selected = 0;
		state = State::Running;
		job = std::make_shared<Job>();
		auto shared = job;
		const std::string file = musicFile;
		const BD::Runtime runtime = BD::locate(Application::getAppDir());
		worker = std::thread([shared, file, runtime] {
			auto progress = [&](float value, const char* what) {
				shared->progress = value;
				for (int i = 0; i < 6; ++i)
					if (!std::strcmp(what, STAGE_NAMES[i]))
						shared->stage = i;
			};
			shared->result = BD::detect(file, runtime, progress, &shared->cancel);
			shared->done = true;
		});
	}

	void BpmDetectWindow::open(const std::string& file)
	{
		musicFile = file;
		openRequested = true;
		if (file.empty())
		{
			state = State::Failed;
			error = getString("bpm_no_music");
			return;
		}
		start();
	}

	void BpmDetectWindow::openInterpolate(const ScoreContext& context)
	{
		int from = context.currentTick, to = context.currentTick + TICKS_PER_BEAT * 4;
		if (context.selectedNotes.size() >= 2)
		{
			int lo = INT32_MAX, hi = 0;
			for (int id : context.selectedNotes)
			{
				auto it = context.score.notes.find(id);
				if (it == context.score.notes.end())
					continue;
				lo = std::min(lo, it->second.tick);
				hi = std::max(hi, it->second.tick);
			}
			if (hi > lo)
			{
				from = lo;
				to = hi;
			}
		}
		fromBeat = static_cast<float>(from) / TICKS_PER_BEAT;
		toBeat = static_cast<float>(to) / TICKS_PER_BEAT;
		startBpm = getTempoAt(from, context.score.tempoChanges).bpm;
		endBpm = startBpm;
		interpolateRequested = true;
	}

	bool BpmDetectWindow::update(ScoreContext& context)
	{
		bool changed = false;

		if (job && job->done)
		{
			if (worker.joinable())
				worker.join();
			BD::Result result = std::move(job->result);
			job.reset();
			if (result.ok)
			{
				candidates = std::move(result.candidates);
				state = State::Ready;
			}
			else
			{
				state = State::Failed;
				error = result.error;
			}
		}

		drawSelect(context, changed);
		drawInterpolate(context, changed);
		return changed;
	}

	void BpmDetectWindow::drawCandidate(int index, float height)
	{
		const BD::Candidate& c = candidates[static_cast<size_t>(index)];
		ImGui::PushID(index);
		const float width = ImGui::GetContentRegionAvail().x;
		if (ImGui::Selectable("##card", selected == index, 0, ImVec2(width, height)))
			selected = index;
		if (ImGui::IsItemHovered())
			UI::tooltip(getString(kindTip(c.kind)));
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		ImDrawList* list = ImGui::GetWindowDrawList();
		const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text), dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		const std::string summary = c.maxBpm - c.minBpm < 0.5
			? IO::formatString(getString("bpm_summary_const"), c.firstBpm)
			: IO::formatString(getString("bpm_summary_range"), c.minBpm, c.maxBpm);
		const std::string detail = c.tempoChanges == 0 ? std::string(getString("bpm_detail_none"))
			: IO::formatString(getString("bpm_detail"), c.tempoChanges, c.ramps);
		const std::string fit = IO::formatString(getString("bpm_fit"), c.rmsMs, c.maxMs);

		const float line = ImGui::GetTextLineHeightWithSpacing();
		const float x = min.x + 10;
		float y = min.y + 6;
		list->AddText(ImVec2(x, y), dim, getString(kindKey(c.kind)));
		y += line;
		ImGui::PushFont(ImGui::GetFont());
		list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.25f, ImVec2(x, y), text, summary.c_str());
		ImGui::PopFont();
		y += line * 1.25f;
		list->AddText(ImVec2(x, y), text, detail.c_str());
		y += line;
		list->AddText(ImVec2(x, y), text, meterText(c).c_str());
		y += line;
		list->AddText(ImVec2(x, y), dim, fit.c_str());

		const float graphWidth = std::min(220.0f, width * 0.42f);
		drawGraph(list, ImVec2(max.x - graphWidth - 8, min.y + 8), ImVec2(max.x - 8, max.y - 8), curveOf(c, 96),
			static_cast<float>(c.minBpm), static_cast<float>(c.maxBpm));
		ImGui::PopID();
	}

	void BpmDetectWindow::drawSelect(ScoreContext& context, bool& changed)
	{
		if (openRequested)
		{
			ImGui::OpenPopup(SELECT_POPUP);
			openRequested = false;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		const std::string title = IO::formatString("%s %s%s", ICON_FA_STOPWATCH, getString("bpm_select_title"), SELECT_POPUP);
		bool open = true;
		if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 600);
		if (state == State::Running && job)
		{
			const int stage = std::clamp(job->stage.load(), 0, 5);
			ImGui::TextUnformatted(job->cancel ? getString("bpm_cancelling") : getString(STAGES[stage]));
			ImGui::ProgressBar(job->progress, ImVec2(-1, 0));
			if (ImGui::Button(getString("cancel"), ImVec2(-1, 0)))
			{
				job->cancel = true;
				// the thread stops between two chunks; wait for it, then close
				if (worker.joinable())
					worker.join();
				job.reset();
				state = State::Idle;
				ImGui::CloseCurrentPopup();
			}
		}
		else if (state == State::Failed)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s",
				IO::formatString(getString("bpm_failed"), error.c_str()).c_str());
			if (!musicFile.empty() && ImGui::Button(getString("bpm_retry"), ImVec2(200, 0)))
				start();
			ImGui::SameLine();
			if (ImGui::Button(getString("close"), ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
		}
		else if (state == State::Ready)
		{
			ImGui::TextDisabled("%s %s", ICON_FA_MUSIC, IO::File::getFilename(musicFile).c_str());
			ImGui::TextUnformatted(getString("bpm_pick"));
			ImGui::Spacing();
			for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
				drawCandidate(i, ImGui::GetTextLineHeightWithSpacing() * 5.4f);

			if (!context.score.notes.empty())
			{
				ImGui::Spacing();
				ImGui::TextDisabled("%s", getString("bpm_notes_hint"));
				ImGui::SameLine();
				if (ImGui::RadioButton(getString("bpm_keep_ticks"), keepNoteTicks))
					keepNoteTicks = true;
				UI::tooltip(getString("bpm_keep_ticks_tooltip"));
				ImGui::SameLine();
				if (ImGui::RadioButton(getString("bpm_keep_time"), !keepNoteTicks))
					keepNoteTicks = false;
				UI::tooltip(getString("bpm_keep_time_tooltip"));
			}

			ImGui::Separator();
			const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
			if (ImGui::Button(getString("apply"), ImVec2(half, 0)) && selected < static_cast<int>(candidates.size()))
			{
				const BD::Timing timing = BD::toTiming(candidates[static_cast<size_t>(selected)]);
				Score prev = context.score;
				BpmTools::applyTiming(context.score, timing, keepNoteTicks);
				context.workingData.musicOffset = static_cast<float>(timing.musicOffsetMs);
				context.audio.setMusicOffset(0, context.workingData.musicOffset);
				context.pushHistory("Detect BPM", prev, context.score);
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(getString("cancel"), ImVec2(half, 0)))
				ImGui::CloseCurrentPopup();
		}
		else if (state == State::Idle)
			ImGui::CloseCurrentPopup();
		ImGui::PopTextWrapPos();

		if (!open)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void BpmDetectWindow::drawInterpolate(ScoreContext& context, bool& changed)
	{
		if (interpolateRequested)
		{
			ImGui::OpenPopup(INTERP_POPUP);
			interpolateRequested = false;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		const std::string title = IO::formatString("%s %s%s", ICON_FA_CHART_LINE, getString("bpm_interp_title"), INTERP_POPUP);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::TextDisabled("%s", getString("bpm_interp_help"));
		if (ImGui::BeginTable("##bpm_interp", 2, ImGuiTableFlags_SizingStretchProp))
		{
			auto row = [](const char* label) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(label);
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
			};
			row(getString("bpm_interp_from"));
			ImGui::InputFloat("##from", &fromBeat, 1.0f, 4.0f, "%.2f");
			row(getString("bpm_interp_to"));
			ImGui::InputFloat("##to", &toBeat, 1.0f, 4.0f, "%.2f");
			row(getString("bpm_interp_start"));
			ImGui::InputFloat("##start", &startBpm, 1.0f, 10.0f, "%.2f");
			row(getString("bpm_interp_end"));
			ImGui::InputFloat("##end", &endBpm, 1.0f, 10.0f, "%.2f");
			row(getString("bpm_interp_step"));
			if (ImGui::BeginCombo("##step", getString(STEP_KEYS[stepIndex])))
			{
				for (int i = 0; i < 4; ++i)
					if (ImGui::Selectable(getString(STEP_KEYS[i]), stepIndex == i))
						stepIndex = i;
				ImGui::EndCombo();
			}
			row(getString("bpm_interp_curve"));
			if (ImGui::BeginCombo("##curve", getString(CURVE_KEYS[curveIndex])))
			{
				for (int i = 0; i < 4; ++i)
					if (ImGui::Selectable(getString(CURVE_KEYS[i]), curveIndex == i))
						curveIndex = i;
				ImGui::EndCombo();
			}
			ImGui::EndTable();
		}
		startBpm = std::clamp(startBpm, MIN_BPM, MAX_BPM);
		endBpm = std::clamp(endBpm, MIN_BPM, MAX_BPM);
		fromBeat = std::max(0.0f, fromBeat);

		const int fromTick = static_cast<int>(std::lround(fromBeat * TICKS_PER_BEAT));
		const int toTick = static_cast<int>(std::lround(toBeat * TICKS_PER_BEAT));
		const bool valid = toTick > fromTick;

		// preview: the same steps the chart will get
		Score preview;
		preview.tempoChanges.clear();
		if (valid)
			BpmTools::interpolateTempo(preview, fromTick, toTick, startBpm, endBpm, STEP_TICKS[stepIndex], static_cast<BpmTools::Curve>(curveIndex));
		std::vector<float> values;
		for (const auto& t : preview.tempoChanges)
			values.push_back(t.bpm);
		const ImVec2 size(ImGui::GetContentRegionAvail().x, 60);
		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImGui::Dummy(size);
		drawGraph(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y), values, std::min(startBpm, endBpm), std::max(startBpm, endBpm));
		ImGui::TextDisabled("%s", IO::formatString(getString("bpm_interp_count"), static_cast<int>(values.size())).c_str());

		ImGui::Separator();
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
		ImGui::BeginDisabled(!valid);
		if (ImGui::Button(getString("apply"), ImVec2(half, 0)))
		{
			Score prev = context.score;
			BpmTools::interpolateTempo(context.score, fromTick, toTick, startBpm, endBpm, STEP_TICKS[stepIndex], static_cast<BpmTools::Curve>(curveIndex));
			context.pushHistory("Interpolate BPM", prev, context.score);
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(getString("cancel"), ImVec2(half, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}
