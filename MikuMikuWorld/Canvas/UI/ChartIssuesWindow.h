#pragma once
#include "../Core/ChartIssues.h"
#include "../../ScoreContext.h"
#include <functional>
#include <string>
#include <vector>

struct ImVec4;

namespace MikuMikuWorld
{
	class ScoreEditorTimeline;

	// The Issue List panel: runs Canvas::Issues on the open chart (and the whole song when one is
	// open) and lists the findings in a table that can be sorted, filtered, searched and
	// rearranged (right-click the column headers to show / hide / reorder columns).
	// Click a row to move the cursor there and select the notes; double-click to also bring the
	// timeline to the front.
	class ChartIssuesWindow
	{
	public:
		// Extra, song-wide findings (missing difficulties etc.); set by the song panel.
		std::function<std::vector<Canvas::Issues::Issue>()> songIssuesProvider;
		// clicking a finding that belongs to another difficulty switches to it
		std::function<void(const std::string&)> onSelectDifficulty;

		// Scanning; call every frame, also while the panel is hidden (the overview strip uses it).
		void process(ScoreContext& context);
		// The panel contents; call inside its window.
		void draw(ScoreContext& context, ScoreEditorTimeline& timeline);

		void requestScan() { scanRequested = true; }
		const Canvas::Issues::Summary& getSummary() const { return summary; }
		const std::vector<Canvas::Issues::Issue>& getIssues() const { return issues; }
		bool hasResults() const { return scanned; }

		// Moves the cursor to an issue (used by the overview strip too).
		void jumpTo(const Canvas::Issues::Issue& issue, ScoreContext& context, ScoreEditorTimeline& timeline, bool focusTimeline);

	private:
		std::vector<Canvas::Issues::Issue> issues;
		Canvas::Issues::Summary summary{};
		unsigned int scannedRevision{ ~0u };
		double revisionChangedAt{ -1.0 };
		bool scanRequested{ true };
		bool scanned{ false };

		std::string fixMessage;				// what the last "Fix overlaps" did
		int categoryFilter{ -1 };
		std::string search;
		int selected{ -1 };					// index into `issues`
		std::vector<int> rows;				// filtered + sorted indices into `issues`
		bool rowsDirty{ true };
		int sortColumn{ 0 };
		bool sortAscending{ true };

		void scan(const ScoreContext& context);
		bool passesFilters(const Canvas::Issues::Issue& issue) const;
		void rebuildRows(const ScoreContext& context);
		std::string positionText(const ScoreContext& context, const Canvas::Issues::Issue& issue) const;
		void drawHeader();
		void drawTable(ScoreContext& context, ScoreEditorTimeline& timeline, float height);
		void drawDetails(ScoreContext& context, ScoreEditorTimeline& timeline);
		void drawEmptyState(const char* text, const ImVec4* color = nullptr);
	};
}
