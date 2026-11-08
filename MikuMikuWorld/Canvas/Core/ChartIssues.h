#pragma once
#include "../../Score.h"
#include <string>
#include <vector>

// Chart issue detection: finds real problems in a chart (or a whole song) and says where.
// Pure analysis of the chart model; the editor's Chart Issues panel and `canvas-cli check`
// both call scanChart / scanSong.
namespace Canvas::Issues
{
	enum class Severity
	{
		Error,		// will break or visibly misbehave in-game / in the exported file
		Warning,	// almost certainly unintended
		Info		// worth a look, often intentional
	};

	enum class Category
	{
		Overlap,			// "Stacked notes": notes on the same tick and left lane
		HoldNodeCollision,	// points of different holds on exactly the same spot
		VisibleOverlap,		// notes at the same moment whose lanes overlap
		Density,
		Holds,
		Lanes,
		HiddenSteps,
		Timing,
		HiSpeed,
		SkillFever,
		Difficulties,
		Structure,
		CategoryCount
	};

	struct Issue
	{
		Severity severity{ Severity::Warning };
		Category category{ Category::Structure };
		std::string code;			// stable id, e.g. "visible-overlap"
		std::string title;			// short headline, e.g. "Visible overlap hides a note"
		std::string message;		// plain-language explanation
		int tick{ -1 };				// where to jump; -1 = no position
		int lane{ -1 };				// first lane (0-11), -1 = none
		int width{ 0 };				// lanes covered from `lane`
		std::vector<int> noteIds;	// notes to select when jumping
		std::string difficulty;		// set by scanSong
	};

	struct ScanOptions
	{
		// Length of the loaded music in seconds; <= 0 when unknown.
		double musicDurationSeconds{ -1.0 };
		bool checkSkillFever{ true };
		bool checkDensity{ true };
		// scanSong only: skip the per-chart scans and report just the cross-difficulty checks
		bool songChecksOnly{ false };
	};

	std::vector<Issue> scanChart(const MikuMikuWorld::Score& score, const ScanOptions& options = {});

	struct DifficultyChart
	{
		std::string name;	// "easy", "normal", "hard", "expert", "master", "append", or custom
		const MikuMikuWorld::Score* score{ nullptr };
	};

	// Per-chart issues for every difficulty (tagged with its name) plus cross-difficulty checks:
	// missing / duplicate difficulties, mismatched skill/fever (FEVER is shared in multi-live),
	// timing that differs between difficulties.
	std::vector<Issue> scanSong(const std::vector<DifficultyChart>& charts, const ScanOptions& options = {});

	const char* severityName(Severity severity);
	const char* categoryName(Category category);

	// Short headline for an issue code ("Exact overlap between hold nodes"); the code itself
	// when it has none.
	const char* issueTitle(const std::string& code);

	// "measure 12, beat 3" style position for a tick (measures counted from 0 like the timeline).
	std::string describePosition(const MikuMikuWorld::Score& score, int tick);

	// Measure (counted from 0 like the timeline) containing `tick`.
	int measureOf(const MikuMikuWorld::Score& score, int tick);

	// "M012 3/8": measure number plus how far into the measure, as a reduced fraction of it.
	std::string measureFraction(const MikuMikuWorld::Score& score, int tick);

	// "Lane 5-7" (lanes counted 1-12), "" when the issue has no lanes.
	std::string trackName(const Issue& issue);

	struct Summary
	{
		int errors{}, warnings{}, infos{};
	};
	Summary summarize(const std::vector<Issue>& issues);
}
