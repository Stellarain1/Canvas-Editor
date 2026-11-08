#pragma once
#include "../../ScoreContext.h"
#include "../Core/StageEdit.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MikuMikuWorld
{
	class ScoreEditorTimeline;

	// The Stages panel (Dynamic Stages): the stage list, one stage's settings and its four joint tracks
	// (mask, pivot, style, move) plus the global camera. The selected row is the stage new notes go to
	// (Canvas::Stages::currentStage). Every finished edit is one undo step.
	class StageWindow
	{
	public:
		void draw(ScoreContext& context, ScoreEditorTimeline& timeline);

	private:
		std::optional<Score> editStart;		// the chart before the widget being dragged / typed in

		void trackEdit(ScoreContext& context);
		void commit(ScoreContext& context, const Score& before);

		// table cells: each starts its own column and makes one undo step per finished edit
		void dragCell(ScoreContext& context, const char* id, float& value, float speed, float lo, float hi, const char* format);
		void comboCell(ScoreContext& context, const char* id, int& value, const char* const* names, int count);
		void checkCell(ScoreContext& context, const char* id, bool& value);
		void easeCell(ScoreContext& context, int& ease);
		// one joint table: time, `columns` (title, width) filled by `row`, ease, delete. Shows the "no joints" line when empty.
		template <typename Ev, typename Row>
		void jointTable(ScoreContext& context, ScoreEditorTimeline& timeline, const char* id, std::vector<Ev>& events,
			const std::vector<std::pair<const char*, float>>& columns, Row row);

		void drawList(ScoreContext& context);
		void drawSettings(ScoreContext& context, ScoreEditorTimeline& timeline);
		void drawNow(const ScoreContext& context);
		void drawTracks(ScoreContext& context, ScoreEditorTimeline& timeline);
		void drawMask(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId);
		void drawPivot(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId);
		void drawStyle(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId);
		void drawMove(ScoreContext& context, ScoreEditorTimeline& timeline, int stageId);
		void drawCamera(ScoreContext& context, ScoreEditorTimeline& timeline);
	};
}
