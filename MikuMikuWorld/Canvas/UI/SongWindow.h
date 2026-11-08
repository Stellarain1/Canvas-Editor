#pragma once
#include "../Core/ChartIssues.h"
#include "../Core/SongProject.h"
#include "../../HistoryManager.h"
#include "../../ScoreContext.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	// The "Song" panel: a song project (.canvassong) groups the difficulties of one song.
	// Every difficulty keeps its own chart and undo history in memory; switching swaps them in
	// and out of the editor. Title, artist, music, offset and jacket are shared.
	class SongWindow
	{
	public:
		struct Host
		{
			// the chart in the editor was replaced: recompute stats, preview, scroll range, title
			std::function<void()> chartSwapped;
			// music file / offset of the song changed
			std::function<void(const std::string& file)> loadMusic;
		};

		void update(ScoreContext& context, Host& host);

		bool isOpen() const { return !songPath.empty(); }
		const std::string& getSongPath() const { return songPath; }
		const std::string& currentDifficulty() const { return current; }
		bool hasUnsavedDifficulties(const ScoreContext& context) const;

		bool openSong(const std::string& path, ScoreContext& context, Host& host, std::string& error);
		bool createFromCurrent(const std::string& path, const std::string& difficulty, ScoreContext& context, Host& host, std::string& error);
		bool saveAll(ScoreContext& context, std::string& error);
		void close();

		bool switchTo(const std::string& difficulty, ScoreContext& context, Host& host);
		// Makes `difficulty` the chart in the editor: switches to it, or adds it (blank) when the
		// song does not have it yet. False when no song is open.
		bool useDifficulty(const std::string& difficulty, ScoreContext& context, Host& host);
		void setLevel(const std::string& difficulty, int level);
		// the play level set for a difficulty (0 = not set or no such difficulty)
		int levelOf(const std::string& difficulty) const;
		// With no song open: which difficulty the loaded chart is (the "This chart is" choice).
		void setStandaloneDifficulty(const std::string& difficulty);
		const std::string& standaloneDifficulty() const { return standalone; }
		void copyMarkersToAll(ScoreContext& context);

		// every difficulty's chart (the current one taken from the editor), in song order
		std::vector<std::pair<std::string, Score>> allCharts(const ScoreContext& context);

		// cross-difficulty findings for the Chart Issues panel
		std::vector<Canvas::Issues::Issue> songIssues(const ScoreContext& context);

	private:
		struct State
		{
			Score score;
			HistoryManager history;
			bool dirty{ false };
			bool fileMissing{ false };
		};

		std::string songPath;
		Canvas::SongProject project;
		std::map<std::string, State> states;
		std::string current;
		std::string standalone{ "master" };	// no song open: what the loaded chart is

		// "add difficulty" UI
		int addChoice{ 0 };
		std::array<char, 32> customName{};
		bool startFromCurrent{ false };

		// cached per-difficulty numbers for the table
		struct Stats
		{
			int combo{};
			int errors{}, warnings{};
		};
		std::map<std::string, Stats> stats;
		unsigned int statsRevision{ ~0u };

		void stash(ScoreContext& context);
		void activate(const std::string& difficulty, ScoreContext& context, Host& host);
		void syncProjectFromEditor(const ScoreContext& context);
		void addDifficulty(const std::string& name, ScoreContext& context, Host& host);
		void removeDifficulty(const std::string& name, ScoreContext& context, Host& host);
		void refreshStats(const ScoreContext& context);
		Score blankChartLike(const Score& reference) const;
	};
}
