#include "../../UI.h"
#include "SongWindow.h"
#include "../Core/ChartIO.h"
#include "../Core/SkillFever.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../IconsFontAwesome5.h"
#include "../../NativeScoreSerializer.h"
#include "../../ScoreStats.h"
#include <algorithm>

namespace MikuMikuWorld
{
	namespace
	{
		// in-game difficulty colours
		ImVec4 difficultyColor(const std::string& name)
		{
			if (name == "easy") return { 0.40f, 0.87f, 0.07f, 1.0f };
			if (name == "normal") return { 0.20f, 0.73f, 0.93f, 1.0f };
			if (name == "hard") return { 1.00f, 0.67f, 0.00f, 1.0f };
			if (name == "expert") return { 0.93f, 0.27f, 0.40f, 1.0f };
			if (name == "master") return { 0.73f, 0.20f, 0.93f, 1.0f };
			if (name == "append") return { 1.00f, 0.53f, 0.80f, 1.0f };
			return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}

		IO::FileDialogFilter songFilter() { return { "Canvas Song", "*.canvassong" }; }
	}

	bool SongWindow::hasUnsavedDifficulties(const ScoreContext& context) const
	{
		if (!isOpen())
			return false;
		for (const auto& [name, state] : states)
			if (name != current && state.dirty)
				return true;
		return false;
	}

	void SongWindow::syncProjectFromEditor(const ScoreContext& context)
	{
		project.title = context.workingData.title;
		project.artist = context.workingData.artist;
		project.musicFile = context.workingData.musicFilename;
		project.musicOffset = context.workingData.musicOffset;
		project.jacketFile = context.workingData.jacket.getFilename();
		if (project.designer.empty())
			project.designer = context.workingData.designer;
	}

	void SongWindow::stash(ScoreContext& context)
	{
		if (current.empty())
			return;
		syncProjectFromEditor(context);
		State& state = states[current];
		state.score = context.score;
		state.score.metadata = context.workingData.toScoreMetadata();
		state.history = context.history;
		state.dirty = !context.upToDate;
	}

	void SongWindow::activate(const std::string& difficulty, ScoreContext& context, Host& host)
	{
		const Canvas::SongDifficulty* entry = project.find(difficulty);
		if (!entry)
			return;

		State& state = states[difficulty];
		current = difficulty;

		ScoreMetadata metadata = state.score.metadata;
		metadata.title = project.title;
		metadata.artist = project.artist;
		metadata.musicFile = project.musicFile;
		metadata.jacketFile = project.jacketFile;
		metadata.musicOffset = project.musicOffset;
		if (metadata.author.empty())
			metadata.author = project.designer;

		context.clearSelection();
		context.score = state.score;
		context.score.metadata = metadata;
		context.history = state.history;
		context.currentLayer = 0;
		context.workingData = EditorScoreData(metadata, entry->file);
		context.upToDate = !state.dirty;
		++context.revision;
		statsRevision = ~0u;

		if (host.chartSwapped)
			host.chartSwapped();
	}

	Score SongWindow::blankChartLike(const Score& reference) const
	{
		// charts of one song share timing and (for multi-live) skill / FEVER positions
		Score score;
		score.metadata = reference.metadata;
		score.tempoChanges = reference.tempoChanges;
		score.timeSignatures = reference.timeSignatures;
		Canvas::SkillFever::applyMarkers(score, Canvas::SkillFever::readMarkers(reference));
		return score;
	}

	bool SongWindow::openSong(const std::string& path, ScoreContext& context, Host& host, std::string& error)
	{
		try
		{
			Canvas::SongProject loaded = Canvas::SongProject::load(path);
			std::map<std::string, State> loadedStates;
			const Score* reference = nullptr;

			for (const auto& difficulty : loaded.difficulties)
			{
				State state;
				if (IO::File::exists(difficulty.file))
				{
					state.score = Canvas::loadChart(difficulty.file).score;
				}
				else
				{
					state.fileMissing = true;
					state.dirty = true;
				}
				loadedStates[difficulty.name] = std::move(state);
			}

			for (const auto& [name, state] : loadedStates)
				if (!state.fileMissing && (!reference || state.score.notes.size() > reference->notes.size()))
					reference = &state.score;
			if (reference)
				for (auto& [name, state] : loadedStates)
					if (state.fileMissing)
						state.score = blankChartLike(*reference);

			if (loaded.difficulties.empty())
			{
				loaded.difficulties.push_back({ "master", Canvas::defaultDifficultyFile(path, "master"), 0 });
				loadedStates["master"].dirty = true;
			}

			project = std::move(loaded);
			states = std::move(loadedStates);
			songPath = path;
			current.clear();

			// start on the hardest standard difficulty that has a file
			std::string start = project.difficulties.front().name;
			for (const auto& name : Canvas::SkillFever::hardestFirst())
			{
				auto it = states.find(name);
				if (it != states.end() && !it->second.fileMissing)
				{
					start = name;
					break;
				}
			}

			activate(start, context, host);
			if (host.loadMusic)
				host.loadMusic(project.musicFile);
			return true;
		}
		catch (const std::exception& ex)
		{
			error = ex.what();
			return false;
		}
	}

	bool SongWindow::createFromCurrent(const std::string& path, const std::string& difficulty, ScoreContext& context, Host& host, std::string& error)
	{
		project = {};
		states.clear();
		songPath = path;
		syncProjectFromEditor(context);
		project.designer = context.workingData.designer;
		project.difficulties.push_back({ difficulty, Canvas::defaultDifficultyFile(path, difficulty), 0 });

		State& state = states[difficulty];
		state.score = context.score;
		state.score.metadata = context.workingData.toScoreMetadata();
		state.history = context.history;
		state.dirty = true;
		current = difficulty;
		context.workingData.filename = project.difficulties.front().file;
		context.upToDate = false;

		if (!saveAll(context, error))
		{
			close();
			return false;
		}
		if (host.chartSwapped)
			host.chartSwapped();
		return true;
	}

	bool SongWindow::saveAll(ScoreContext& context, std::string& error)
	{
		if (!isOpen())
			return false;

		try
		{
			stash(context);
			for (const auto& difficulty : project.difficulties)
			{
				State& state = states[difficulty.name];
				if (!state.dirty && !state.fileMissing && IO::File::exists(difficulty.file))
					continue;

				Score out = state.score;
				out.metadata.title = project.title;
				out.metadata.artist = project.artist;
				out.metadata.musicFile = project.musicFile;
				out.metadata.jacketFile = project.jacketFile;
				out.metadata.musicOffset = project.musicOffset;
				NativeScoreSerializer().serialize(out, difficulty.file);
				state.dirty = false;
				state.fileMissing = false;
			}

			project.save(songPath);
			context.upToDate = true;
			UI::setWindowTitle(IO::File::getFilename(songPath) + " - " + current);
			return true;
		}
		catch (const std::exception& ex)
		{
			error = ex.what();
			return false;
		}
	}

	void SongWindow::close()
	{
		songPath.clear();
		project = {};
		states.clear();
		current.clear();
		stats.clear();
	}

	bool SongWindow::switchTo(const std::string& difficulty, ScoreContext& context, Host& host)
	{
		if (!isOpen() || difficulty == current || !project.find(difficulty))
			return false;

		stash(context);
		activate(difficulty, context, host);
		return true;
	}

	bool SongWindow::useDifficulty(const std::string& difficulty, ScoreContext& context, Host& host)
	{
		if (!isOpen() || difficulty.empty())
			return false;
		if (difficulty == current)
			return true;
		if (project.find(difficulty))
			return switchTo(difficulty, context, host);
		const bool keep = startFromCurrent;
		startFromCurrent = false;
		addDifficulty(difficulty, context, host);
		startFromCurrent = keep;
		return current == difficulty;
	}

	void SongWindow::setLevel(const std::string& difficulty, int level)
	{
		for (auto& d : project.difficulties)
			if (d.name == difficulty)
				d.level = level;
	}

	void SongWindow::addDifficulty(const std::string& name, ScoreContext& context, Host& host)
	{
		if (name.empty() || project.find(name))
			return;

		std::string file = Canvas::defaultDifficultyFile(songPath, name);
		for (int n = 2; std::any_of(project.difficulties.begin(), project.difficulties.end(),
			[&](const Canvas::SongDifficulty& d) { return d.file == file; }); ++n)
			file = Canvas::defaultDifficultyFile(songPath, name + "_" + std::to_string(n));

		stash(context);
		State state;
		state.score = startFromCurrent ? context.score : blankChartLike(context.score);
		state.score.metadata = context.workingData.toScoreMetadata();
		state.dirty = true;
		state.fileMissing = true;
		states[name] = std::move(state);

		project.difficulties.push_back({ name, file, 0 });
		std::stable_sort(project.difficulties.begin(), project.difficulties.end(),
			[](const Canvas::SongDifficulty& a, const Canvas::SongDifficulty& b) { return Canvas::difficultyOrder(a.name) < Canvas::difficultyOrder(b.name); });

		try { project.save(songPath); } catch (...) {}
		activate(name, context, host);
	}

	void SongWindow::removeDifficulty(const std::string& name, ScoreContext& context, Host& host)
	{
		if (project.difficulties.size() <= 1 || !project.find(name))
			return;

		if (name == current)
		{
			const auto other = std::find_if(project.difficulties.begin(), project.difficulties.end(),
				[&](const Canvas::SongDifficulty& d) { return d.name != name; });
			switchTo(other->name, context, host);
		}

		project.difficulties.erase(std::remove_if(project.difficulties.begin(), project.difficulties.end(),
			[&](const Canvas::SongDifficulty& d) { return d.name == name; }), project.difficulties.end());
		states.erase(name);
		stats.erase(name);
		try { project.save(songPath); } catch (...) {}
	}

	void SongWindow::copyMarkersToAll(ScoreContext& context)
	{
		namespace SF = Canvas::SkillFever;
		stash(context);

		SF::Markers markers = SF::readMarkers(context.score);
		for (const auto& name : SF::hardestFirst())
		{
			if (!markers.empty())
				break;
			auto it = states.find(name);
			if (it != states.end() && SF::hasMarkers(it->second.score))
				markers = SF::readMarkers(it->second.score);
		}
		for (const auto& name : SF::hardestFirst())
		{
			if (!markers.empty())
				break;
			auto it = states.find(name);
			if (it != states.end())
				markers = SF::generateOfficialStyle(it->second.score);
		}
		if (markers.empty())
			markers = SF::generateOfficialStyle(context.score);
		if (markers.empty())
			return;

		for (auto& [name, state] : states)
		{
			if (name == current)
			{
				if (!(SF::readMarkers(context.score) == markers))
				{
					Score prev = context.score;
					SF::applyMarkers(context.score, markers);
					context.pushHistory("Copy skills & FEVER to all difficulties", prev, context.score);
					state.score = context.score;
				}
			}
			else if (!(SF::readMarkers(state.score) == markers))
			{
				SF::applyMarkers(state.score, markers);
				state.dirty = true;
			}
		}
		statsRevision = ~0u;
	}

	int SongWindow::levelOf(const std::string& difficulty) const
	{
		const Canvas::SongDifficulty* d = project.find(difficulty);
		return d ? d->level : 0;
	}

	std::vector<std::pair<std::string, Score>> SongWindow::allCharts(const ScoreContext& context)
	{
		std::vector<std::pair<std::string, Score>> charts;
		for (const auto& difficulty : project.difficulties)
		{
			Score score = difficulty.name == current ? context.score : states[difficulty.name].score;
			if (difficulty.name == current)
				score.metadata = context.workingData.toScoreMetadata();
			score.metadata.title = project.title;
			score.metadata.artist = project.artist;
			charts.emplace_back(difficulty.name, std::move(score));
		}
		return charts;
	}

	std::vector<Canvas::Issues::Issue> SongWindow::songIssues(const ScoreContext& context)
	{
		if (!isOpen())
			return {};

		std::vector<Canvas::Issues::DifficultyChart> charts;
		for (const auto& difficulty : project.difficulties)
		{
			const Score* score = difficulty.name == current ? &context.score : &states[difficulty.name].score;
			charts.push_back({ difficulty.name, score });
		}

		Canvas::Issues::ScanOptions options;
		options.songChecksOnly = true;
		return Canvas::Issues::scanSong(charts, options);
	}

	void SongWindow::refreshStats(const ScoreContext& context)
	{
		if (statsRevision == context.revision)
			return;

		for (const auto& difficulty : project.difficulties)
		{
			const bool isCurrent = difficulty.name == current;
			if (!isCurrent && stats.count(difficulty.name) && statsRevision != ~0u)
				continue;

			const Score& score = isCurrent ? context.score : states[difficulty.name].score;
			Stats entry;
			entry.combo = static_cast<int>(calculateComboEvents(score).size());
			const auto summary = Canvas::Issues::summarize(Canvas::Issues::scanChart(score));
			entry.errors = summary.errors;
			entry.warnings = summary.warnings;
			stats[difficulty.name] = entry;
		}
		statsRevision = context.revision;
	}

	void SongWindow::update(ScoreContext& context, Host& host)
	{
		std::string error;

		if (!isOpen())
		{
			ImGui::TextWrapped("%s", getString("song_none"));
			ImGui::Spacing();

			const auto& names = Canvas::standardDifficulties();
			int createAs = 4;	// master
			for (int i = 0; i < static_cast<int>(names.size()); ++i)
				if (names[i] == standalone)
					createAs = i;
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##create_as", IO::formatString("%s: %s", getString("song_current_chart_is"), names[createAs].c_str()).c_str()))
			{
				for (int i = 0; i < static_cast<int>(names.size()); ++i)
					if (ImGui::Selectable(names[i].c_str(), createAs == i))
					{
						createAs = i;
						standalone = names[i];
					}
				ImGui::EndCombo();
			}

			if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("song_create"), " ").c_str(), { -1, 0 }))
			{
				IO::FileDialog dialog{};
				dialog.title = getString("song_create");
				dialog.filters = { songFilter() };
				dialog.defaultExtension = "canvassong";
				dialog.inputFilename = context.workingData.title.empty() ? "song" : context.workingData.title;
				dialog.parentWindowHandle = Application::windowState.windowHandle;
				if (dialog.saveFile() == IO::FileDialogResult::OK && !createFromCurrent(dialog.outputFilename, names[createAs], context, host, error))
					IO::messageBox(APP_NAME, error, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error, Application::windowState.windowHandle);
			}

			if (ImGui::Button(IO::concat(ICON_FA_FOLDER_OPEN, getString("song_open"), " ").c_str(), { -1, 0 }))
			{
				IO::FileDialog dialog{};
				dialog.title = getString("song_open");
				dialog.filters = { songFilter(), IO::allFilter };
				dialog.parentWindowHandle = Application::windowState.windowHandle;
				if (dialog.openFile() == IO::FileDialogResult::OK && !openSong(dialog.outputFilename, context, host, error))
					IO::messageBox(APP_NAME, error, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error, Application::windowState.windowHandle);
			}
			return;
		}

		refreshStats(context);

		ImGui::TextUnformatted(project.title.empty() ? IO::File::getFilenameWithoutExtension(songPath).c_str() : project.title.c_str());
		if (!project.artist.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("- %s", project.artist.c_str());
		}
		ImGui::TextDisabled("%s", IO::File::getFilename(songPath).c_str());
		UI::tooltip(songPath.c_str());

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
		std::string removeRequest, switchRequest;
		if (ImGui::BeginTable("##difficulties", 4, flags))
		{
			ImGui::TableSetupColumn(getString("song_difficulty"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(getString("song_level"), ImGuiTableColumnFlags_WidthFixed, 56);
			ImGui::TableSetupColumn(getString("song_combo"), ImGuiTableColumnFlags_WidthFixed, 48);
			ImGui::TableSetupColumn(getString("chart_issues"), ImGuiTableColumnFlags_WidthFixed, 64);
			ImGui::TableHeadersRow();

			for (auto& difficulty : project.difficulties)
			{
				ImGui::PushID(difficulty.name.c_str());
				ImGui::TableNextRow();
				const bool isCurrent = difficulty.name == current;
				const State& state = states[difficulty.name];
				const bool dirty = isCurrent ? !context.upToDate : state.dirty;

				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable("##row", isCurrent, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap) && !isCurrent)
					switchRequest = difficulty.name;
				if (ImGui::BeginPopupContextItem("##row_menu"))
				{
					if (ImGui::MenuItem(getString("song_remove_difficulty"), nullptr, false, project.difficulties.size() > 1))
						removeRequest = difficulty.name;
					ImGui::EndPopup();
				}
				ImGui::SameLine(0, 0);
				ImGui::PushStyleColor(ImGuiCol_Text, difficultyColor(difficulty.name));
				ImGui::Text("%s %s%s", isCurrent ? ICON_FA_PLAY : "  ", difficulty.name.c_str(), dirty ? " *" : "");
				ImGui::PopStyleColor();
				if (state.fileMissing)
					UI::tooltip(getString("song_not_saved_yet"));

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				int level = difficulty.level;
				if (ImGui::InputInt("##level", &level, 0, 0))
				{
					difficulty.level = std::clamp(level, 0, 99);
					try { project.save(songPath); } catch (...) {}
				}

				const auto it = stats.find(difficulty.name);
				ImGui::TableSetColumnIndex(2);
				if (it != stats.end())
					ImGui::Text("%d", it->second.combo);

				ImGui::TableSetColumnIndex(3);
				if (it != stats.end())
				{
					if (it->second.errors)
						ImGui::TextColored({ 0.96f, 0.38f, 0.40f, 1 }, "%s%d", ICON_FA_TIMES_CIRCLE, it->second.errors);
					else if (it->second.warnings)
						ImGui::TextColored({ 0.98f, 0.76f, 0.28f, 1 }, "%s%d", ICON_FA_EXCLAMATION_TRIANGLE, it->second.warnings);
					else
						ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1 }, "%s", ICON_FA_CHECK);
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if (!switchRequest.empty())
			switchTo(switchRequest, context, host);
		if (!removeRequest.empty())
			removeDifficulty(removeRequest, context, host);

		// ---- add a difficulty
		ImGui::Separator();
		std::vector<std::string> choices;
		for (const auto& name : Canvas::standardDifficulties())
			if (!project.find(name))
				choices.push_back(name);
		choices.push_back(getString("song_custom_difficulty"));
		addChoice = std::clamp(addChoice, 0, static_cast<int>(choices.size()) - 1);
		const bool custom = addChoice == static_cast<int>(choices.size()) - 1;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		if (ImGui::BeginCombo("##add_choice", choices[addChoice].c_str()))
		{
			for (int i = 0; i < static_cast<int>(choices.size()); ++i)
				if (ImGui::Selectable(choices[i].c_str(), addChoice == i))
					addChoice = i;
			ImGui::EndCombo();
		}
		if (custom)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##custom_name", getString("song_custom_name_hint"), customName.data(), customName.size());
		}
		ImGui::Checkbox(getString("song_start_from_current"), &startFromCurrent);
		UI::tooltip(getString("song_start_from_current_tooltip"));
		ImGui::SameLine();
		const std::string newName = custom ? std::string(customName.data()) : choices[addChoice];
		ImGui::BeginDisabled(newName.empty() || project.find(newName));
		if (ImGui::Button(IO::concat(ICON_FA_PLUS, getString("song_add_difficulty"), " ").c_str(), { -1, 0 }))
		{
			addDifficulty(newName, context, host);
			customName.fill(0);
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		if (ImGui::Button(IO::concat(ICON_FA_STAR, getString("song_copy_skill_fever"), " ").c_str(), { -1, 0 }))
			copyMarkersToAll(context);
		UI::tooltip(getString("song_copy_skill_fever_tooltip"));

		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
		if (ImGui::Button(IO::concat(ICON_FA_SAVE, getString("song_save"), " ").c_str(), { half, 0 }) && !saveAll(context, error))
			IO::messageBox(APP_NAME, error, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error, Application::windowState.windowHandle);
		ImGui::SameLine();
		if (ImGui::Button(IO::concat(ICON_FA_TIMES, getString("song_close"), " ").c_str(), { half, 0 }))
		{
			if (!hasUnsavedDifficulties(context) ||
				IO::messageBox(APP_NAME, getString("song_close_unsaved"), IO::MessageBoxButtons::YesNo, IO::MessageBoxIcon::Warning,
					Application::windowState.windowHandle) == IO::MessageBoxResult::Yes)
				close();
		}
	}

	void SongWindow::setStandaloneDifficulty(const std::string& difficulty)
	{
		for (const auto& name : Canvas::standardDifficulties())
			if (name == difficulty)
				standalone = difficulty;
	}
}
