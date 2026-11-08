#include "../../UI.h"
#include "ChartIssuesWindow.h"
#include "../Core/ChartFixes.h"
#include "../../ScoreEditorTimeline.h"
#include "../../ApplicationConfiguration.h"
#include "../../IconsFontAwesome5.h"
#include "../../ImGui/imgui_stdlib.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <tuple>

namespace MikuMikuWorld
{
	using namespace Canvas::Issues;

	namespace
	{
		// wait this long after the last edit before re-scanning automatically
		constexpr double AUTO_SCAN_DELAY_SECONDS = 0.35;

		enum Column : ImGuiID
		{
			ColumnSeverity,
			ColumnDescription,
			ColumnMeasure,
			ColumnTick,
			ColumnTrack,
			ColumnCategory,
			ColumnDifficulty,
			ColumnCode,
			ColumnCount
		};

		ImVec4 severityColor(Severity severity)
		{
			switch (severity)
			{
			case Severity::Error: return { 0.96f, 0.38f, 0.40f, 1.0f };
			case Severity::Warning: return { 0.98f, 0.76f, 0.28f, 1.0f };
			default: return { 0.45f, 0.72f, 0.98f, 1.0f };
			}
		}

		const char* severityIcon(Severity severity)
		{
			switch (severity)
			{
			case Severity::Error: return ICON_FA_TIMES_CIRCLE;
			case Severity::Warning: return ICON_FA_EXCLAMATION_TRIANGLE;
			default: return ICON_FA_INFO_CIRCLE;
			}
		}

		std::string lowercase(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		const char* positionFormatKey(int format)
		{
			switch (format)
			{
			case 1: return "issues_position_beats";
			case 2: return "issues_position_ticks";
			case 3: return "issues_position_time";
			default: return "issues_position_fraction";
			}
		}
	}

	std::string ChartIssuesWindow::positionText(const ScoreContext& context, const Issue& issue) const
	{
		if (issue.tick < 0)
			return "";

		const Score& score = context.score;
		switch (config.issuesPositionFormat)
		{
		case 1:
		{
			const int measure = measureOf(score, issue.tick);
			const int measureStart = measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures);
			const float beat = 1.0f + static_cast<float>(issue.tick - measureStart) / TICKS_PER_BEAT;
			return IO::formatString("%d:%s", measure, IO::formatFixedFloatTrimmed(std::round(beat * 100.0f) / 100.0f, 2).c_str());
		}
		case 2:
			return IO::formatString("t%d", issue.tick);
		case 3:
		{
			const double seconds = accumulateDuration(issue.tick, TICKS_PER_BEAT, score.tempoChanges);
			return IO::formatString("%d:%05.2f", static_cast<int>(seconds / 60), std::fmod(seconds, 60.0));
		}
		default:
			return measureFraction(score, issue.tick);
		}
	}

	void ChartIssuesWindow::scan(const ScoreContext& context)
	{
		ScanOptions options;
		const float musicLength = const_cast<ScoreContext&>(context).audio.getMusicLength();
		if (musicLength > 0)
			options.musicDurationSeconds = musicLength;

		// keep the same finding selected across re-scans when it is still there
		Issue previous;
		const bool hadSelection = selected >= 0 && selected < static_cast<int>(issues.size());
		if (hadSelection)
			previous = issues[selected];

		issues = scanChart(context.score, options);
		if (songIssuesProvider)
		{
			auto song = songIssuesProvider();
			issues.insert(issues.end(), song.begin(), song.end());
		}

		selected = -1;
		if (hadSelection)
		{
			for (int i = 0; i < static_cast<int>(issues.size()); ++i)
			{
				if (issues[i].code == previous.code && issues[i].tick == previous.tick && issues[i].noteIds == previous.noteIds &&
					issues[i].difficulty == previous.difficulty)
				{
					selected = i;
					break;
				}
			}
		}

		summary = summarize(issues);
		scannedRevision = context.revision;
		scanRequested = false;
		scanned = true;
		rowsDirty = true;
	}

	void ChartIssuesWindow::process(ScoreContext& context)
	{
		const double now = ImGui::GetTime();
		if (context.revision != scannedRevision)
		{
			if (revisionChangedAt < 0)
				revisionChangedAt = now;
			if (config.issuesAutoScan && now - revisionChangedAt >= AUTO_SCAN_DELAY_SECONDS)
				scanRequested = true;
		}

		if (scanRequested)
		{
			scan(context);
			revisionChangedAt = -1;
		}
	}

	void ChartIssuesWindow::jumpTo(const Issue& issue, ScoreContext& context, ScoreEditorTimeline& timeline, bool focusTimeline)
	{
		if (!issue.difficulty.empty() && onSelectDifficulty)
			onSelectDifficulty(issue.difficulty);

		if (issue.tick >= 0)
			timeline.jumpToTick(context, issue.tick);

		if (!issue.noteIds.empty())
		{
			context.clearSelection();
			for (int id : issue.noteIds)
				if (context.score.notes.find(id) != context.score.notes.end())
					context.selectedNotes.insert(id);
		}

		if (focusTimeline)
			ImGui::SetWindowFocus("###notes_timeline");
	}

	bool ChartIssuesWindow::passesFilters(const Issue& issue) const
	{
		switch (issue.severity)
		{
		case Severity::Error: if (!config.issuesShowErrors) return false; break;
		case Severity::Warning: if (!config.issuesShowWarnings) return false; break;
		default: if (!config.issuesShowInfos) return false; break;
		}

		if (categoryFilter >= 0 && static_cast<int>(issue.category) != categoryFilter)
			return false;

		if (search.empty())
			return true;

		const std::string needle = lowercase(search);
		for (const std::string& haystack : { issue.title, issue.message, std::string(categoryName(issue.category)), issue.code, trackName(issue), issue.difficulty })
			if (lowercase(haystack).find(needle) != std::string::npos)
				return true;
		return false;
	}

	void ChartIssuesWindow::rebuildRows(const ScoreContext& context)
	{
		rows.clear();
		for (int i = 0; i < static_cast<int>(issues.size()); ++i)
			if (passesFilters(issues[i]))
				rows.push_back(i);

		auto key = [&](const Issue& issue) -> std::tuple<int, std::string, int, int>
		{
			switch (sortColumn)
			{
			case ColumnDescription: return { 0, lowercase(issue.title), issue.tick, 0 };
			case ColumnMeasure:
			case ColumnTick: return { issue.tick, "", static_cast<int>(issue.severity), issue.lane };
			case ColumnTrack: return { issue.lane, "", issue.width, issue.tick };
			case ColumnCategory: return { static_cast<int>(issue.category), "", issue.tick, 0 };
			case ColumnDifficulty: return { 0, issue.difficulty, issue.tick, 0 };
			case ColumnCode: return { 0, issue.code, issue.tick, 0 };
			default: return { static_cast<int>(issue.severity), "", issue.tick, issue.lane };
			}
		};

		std::stable_sort(rows.begin(), rows.end(), [&](int a, int b)
		{
			const auto ka = key(issues[a]), kb = key(issues[b]);
			return sortAscending ? ka < kb : kb < ka;
		});
		rowsDirty = false;
	}

	void ChartIssuesWindow::drawEmptyState(const char* text, const ImVec4* color)
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const float wrap = std::max(50.0f, width - 20.0f);
		const ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, wrap);
		ImGui::Dummy({ 0, ImGui::GetTextLineHeight() * 0.5f });
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (width - size.x) / 2));
		if (color)
			ImGui::PushStyleColor(ImGuiCol_Text, *color);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		if (color)
			ImGui::PopStyleColor();
	}

	void ChartIssuesWindow::drawHeader()
	{
		const ImGuiStyle& style = ImGui::GetStyle();

		// ---- counts
		auto count = [&](Severity severity, int value, const char* key)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, severityColor(severity));
			ImGui::Text("%s %d %s", severityIcon(severity), value, getString(key));
			ImGui::PopStyleColor();
			ImGui::SameLine(0, style.ItemSpacing.x * 3 + 4);
		};
		count(Severity::Error, summary.errors, "issues_errors_short");
		count(Severity::Warning, summary.warnings, "issues_warnings_short");
		count(Severity::Info, summary.infos, "issues_infos_short");
		ImGui::Text("%s %d", getString("issues_total"), summary.errors + summary.warnings + summary.infos);

		// ---- right side: detect, auto, options
		const std::string detect = IO::concat(ICON_FA_SYNC, getString("issues_detect"), " ");
		// the group's real width (buttons = text + padding, checkbox = box + gap + label); when it does
		// not fit beside the counts it moves to its own row instead of sitting on top of them
		const float right = ImGui::CalcTextSize(detect.c_str()).x + style.FramePadding.x * 2 +
			ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(getString("issues_auto_scan")).x +
			ImGui::CalcTextSize(ICON_FA_COG).x + style.FramePadding.x * 2 + style.ItemSpacing.x * 2;
		ImGui::SameLine();
		const float rightStart = ImGui::GetWindowContentRegionMax().x - right;
		if (ImGui::GetCursorPosX() + style.ItemSpacing.x * 2 > rightStart)
			ImGui::NewLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightStart));
		if (ImGui::Button(detect.c_str()))
			scanRequested = true;
		UI::tooltip(IO::formatString("%s (%s)", getString("issues_detect_tooltip"), ToShortcutString(config.input.detectIssues)).c_str());
		ImGui::SameLine();
		ImGui::Checkbox(getString("issues_auto_scan"), &config.issuesAutoScan);
		UI::tooltip(getString("issues_auto_scan_tooltip"));
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_COG "##issue_options"))
			ImGui::OpenPopup("##issue_options_popup");
		UI::tooltip(getString("issues_options"));

		if (ImGui::BeginPopup("##issue_options_popup"))
		{
			ImGui::Checkbox(getString("issues_show_details"), &config.issuesShowDetails);
			ImGui::Checkbox(getString("issues_click_focuses_timeline"), &config.issuesClickFocusesTimeline);
			ImGui::SetNextItemWidth(200);
			if (ImGui::BeginCombo(getString("issues_position_format"), getString(positionFormatKey(config.issuesPositionFormat))))
			{
				for (int format = 0; format < 4; ++format)
					if (ImGui::Selectable(getString(positionFormatKey(format)), config.issuesPositionFormat == format))
						config.issuesPositionFormat = format;
				ImGui::EndCombo();
			}
			ImGui::Separator();
			ImGui::TextDisabled("%s", getString("issues_columns_hint"));
			ImGui::EndPopup();
		}

		// ---- severity filters and category
		bool changed = false;
		changed |= ImGui::Checkbox(getString("issues_errors_short"), &config.issuesShowErrors);
		ImGui::SameLine();
		changed |= ImGui::Checkbox(getString("issues_warnings_short"), &config.issuesShowWarnings);
		ImGui::SameLine();
		changed |= ImGui::Checkbox(getString("issues_infos_short"), &config.issuesShowInfos);
		ImGui::SameLine(0, style.ItemSpacing.x * 3 + 4);
		ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x));
		const char* preview = categoryFilter < 0 ? getString("issues_all_categories") : categoryName(static_cast<Category>(categoryFilter));
		if (ImGui::BeginCombo("##issue_category", preview))
		{
			if (ImGui::Selectable(getString("issues_all_categories"), categoryFilter < 0))
			{
				categoryFilter = -1;
				changed = true;
			}
			for (int c = 0; c < static_cast<int>(Category::CategoryCount); ++c)
			{
				if (ImGui::Selectable(categoryName(static_cast<Category>(c)), categoryFilter == c))
				{
					categoryFilter = c;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}

		// ---- search
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, style.ItemSpacing.y));
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - UI::btnSmall.x - 2);
		changed |= ImGui::InputTextWithHint("##issue_search", IO::concat(ICON_FA_SEARCH, getString("search"), " ").c_str(), &search);
		ImGui::SameLine(0, 2);
		if (ImGui::Button(ICON_FA_TIMES "##clear_issue_search", UI::btnSmall))
		{
			search.clear();
			changed = true;
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		if (changed)
			rowsDirty = true;
	}

	void ChartIssuesWindow::drawTable(ScoreContext& context, ScoreEditorTimeline& timeline, float height)
	{
		const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
			ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuter |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if (!ImGui::BeginTable("##issue_table", ColumnCount, flags, { 0, height }))
			return;

		const float digits = ImGui::CalcTextSize("000000").x;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHide |
			ImGuiTableColumnFlags_DefaultSort, ImGui::GetFrameHeight(), ColumnSeverity);
		ImGui::TableSetupColumn(getString("issues_description"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 4.0f, ColumnDescription);
		ImGui::TableSetupColumn(getString("issues_measure"), ImGuiTableColumnFlags_WidthFixed, std::max(digits * 1.3f, ImGui::CalcTextSize(getString("issues_measure")).x + ImGui::GetFontSize()), ColumnMeasure);
		ImGui::TableSetupColumn(getString("issues_tick"), ImGuiTableColumnFlags_WidthFixed, digits * 1.3f, ColumnTick);
		ImGui::TableSetupColumn(getString("issues_track"), ImGuiTableColumnFlags_WidthFixed, digits * 1.8f, ColumnTrack);
		ImGui::TableSetupColumn(getString("issues_category"), ImGuiTableColumnFlags_WidthFixed, digits * 3.2f, ColumnCategory);
		ImGui::TableSetupColumn(getString("song_difficulty"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, digits * 1.6f, ColumnDifficulty);
		ImGui::TableSetupColumn(getString("issues_code"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, digits * 2.6f, ColumnCode);
		ImGui::TableHeadersRow();

		if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
		{
			if (specs->SpecsDirty && specs->SpecsCount > 0)
			{
				sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
				sortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
				rowsDirty = true;
			}
			specs->SpecsDirty = false;
		}
		if (rowsDirty)
			rebuildRows(context);

		// rows get taller with the interface density setting; text is centred in them
		const ImGuiStyle& style = ImGui::GetStyle();
		const float lineHeight = ImGui::GetTextLineHeight();
		const float rowHeight = lineHeight + style.CellPadding.y * 2 + 5.0f * config.uiDensity;
		const float textOffset = (rowHeight - style.CellPadding.y * 2 - lineHeight) / 2;
		auto cell = [&](int column)
		{
			if (!ImGui::TableSetColumnIndex(column))
				return false;
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textOffset);
			return true;
		};

		ImVec4 selectedColor = UI::accentColors[config.accentColor];
		selectedColor.w = 0.35f;
		ImGui::PushStyleColor(ImGuiCol_Header, selectedColor);
		selectedColor.w = 0.25f;
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selectedColor);

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows.size()), rowHeight);
		while (clipper.Step())
		{
			for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
			{
				const int index = rows[r];
				const Issue& issue = issues[index];
				ImGui::PushID(index);
				ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

				ImGui::TableSetColumnIndex(ColumnSeverity);
				const bool clicked = ImGui::Selectable("##row", selected == index,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
					{ 0, rowHeight - style.CellPadding.y * 2 });
				const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
				const bool doubleClicked = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				ImGui::SameLine(0, 0);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textOffset);
				ImGui::PushStyleColor(ImGuiCol_Text, severityColor(issue.severity));
				ImGui::TextUnformatted(severityIcon(issue.severity));
				ImGui::PopStyleColor();

				if (cell(ColumnDescription))
				{
					if (!issue.difficulty.empty())
					{
						ImGui::TextDisabled("%s", issue.difficulty.c_str());
						ImGui::SameLine();
					}
					ImGui::TextUnformatted(issue.title.c_str());
				}
				if (cell(ColumnMeasure))
				{
					if (issue.tick >= 0)
						ImGui::Text("%d", measureOf(context.score, issue.tick));
					else
						ImGui::TextDisabled("-");
				}
				if (cell(ColumnTick))
				{
					if (issue.tick >= 0)
						ImGui::Text("%d", issue.tick);
					else
						ImGui::TextDisabled("-");
				}
				if (cell(ColumnTrack))
					ImGui::TextUnformatted(trackName(issue).c_str());
				if (cell(ColumnCategory))
					ImGui::TextUnformatted(categoryName(issue.category));
				if (cell(ColumnDifficulty))
					ImGui::TextUnformatted(issue.difficulty.c_str());
				if (cell(ColumnCode))
					ImGui::TextDisabled("%s", issue.code.c_str());

				if (hovered && ImGui::BeginTooltip())
				{
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
					ImGui::TextUnformatted(issue.message.c_str());
					const std::string where = positionText(context, issue);
					if (!where.empty())
						ImGui::TextDisabled("%s", where.c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}

				if (clicked)
				{
					selected = index;
					jumpTo(issue, context, timeline, doubleClicked || config.issuesClickFocusesTimeline);
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleColor(2);
		ImGui::EndTable();
	}

	void ChartIssuesWindow::drawDetails(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		ImGui::BeginChild("##issue_details", { 0, 0 }, ImGuiChildFlags_Borders);
		if (selected < 0 || selected >= static_cast<int>(issues.size()))
		{
			ImGui::TextDisabled("%s", getString("issues_select_hint"));
			ImGui::EndChild();
			return;
		}

		const Issue& issue = issues[selected];
		ImGui::PushStyleColor(ImGuiCol_Text, severityColor(issue.severity));
		ImGui::TextUnformatted(severityIcon(issue.severity));
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextUnformatted(issue.title.c_str());

		std::string where = positionText(context, issue);
		for (const std::string& part : { trackName(issue), std::string(categoryName(issue.category)), issue.difficulty })
			if (!part.empty())
				where += (where.empty() ? "" : " | ") + part;
		ImGui::TextDisabled("%s", where.c_str());

		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(issue.message.c_str());
		ImGui::PopTextWrapPos();

		if (issue.tick >= 0 || !issue.noteIds.empty())
		{
			if (ImGui::SmallButton(IO::concat(ICON_FA_CROSSHAIRS, getString("issues_show_in_timeline"), " ").c_str()))
				jumpTo(issue, context, timeline, true);
			ImGui::SameLine();
		}
		if (ImGui::SmallButton(IO::concat(ICON_FA_COPY, getString("copy"), " ").c_str()))
			ImGui::SetClipboardText(IO::formatString("[%s] %s (%s)\n%s", issue.code.c_str(), issue.title.c_str(), where.c_str(), issue.message.c_str()).c_str());
		ImGui::EndChild();
	}

	void ChartIssuesWindow::draw(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		drawHeader();

		if (!scanned)
		{
			drawEmptyState(IO::formatString(getString("issues_not_scanned"), ToShortcutString(config.input.detectIssues)).c_str());
			return;
		}

		if (issues.empty())
		{
			const ImVec4 green{ 0.45f, 0.85f, 0.55f, 1.0f };
			drawEmptyState(IO::concat(ICON_FA_CHECK_CIRCLE, getString("issues_none"), " ").c_str(), &green);
			return;
		}

		if (context.revision != scannedRevision && !config.issuesAutoScan)
			ImGui::TextDisabled("%s", getString("issues_out_of_date"));

		// one click for the overlaps that have an obvious fix (duplicates, taps on other notes)
		{
			int overlaps = 0;
			for (const auto& issue : issues)
				overlaps += issue.difficulty.empty() && Canvas::Fixes::isOverlapCode(issue.code);
			if (overlaps > 0)
			{
				if (ImGui::Button(IO::formatString("%s %s (%d)", ICON_FA_MAGIC, getString("issues_fix_overlaps"), overlaps).c_str()))
				{
					const Score prev = context.score;
					const auto report = Canvas::Fixes::fixOverlaps(context.score);
					if (report.changed())
						context.pushHistory(getString("issues_fix_overlaps"), prev, context.score);
					fixMessage = IO::formatString(getString("issues_fix_overlaps_result"), report.merged, report.moved, report.left);
					scanRequested = true;
				}
				UI::tooltip(getString("issues_fix_overlaps_tooltip"));
				if (!fixMessage.empty())
					ImGui::SameLine();
			}
			if (!fixMessage.empty())
				ImGui::TextDisabled("%s", fixMessage.c_str());
		}

		if (rowsDirty)
			rebuildRows(context);
		if (rows.empty())
		{
			drawEmptyState(getString("issues_filtered_out"));
			return;
		}

		const float detailsHeight = config.issuesShowDetails ? ImGui::GetTextLineHeightWithSpacing() * 4.5f + ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2 : 0.0f;
		const float tableHeight = std::max(ImGui::GetFrameHeight() * 3, ImGui::GetContentRegionAvail().y - detailsHeight);
		drawTable(context, timeline, tableHeight);
		if (config.issuesShowDetails)
			drawDetails(context, timeline);
	}
}
