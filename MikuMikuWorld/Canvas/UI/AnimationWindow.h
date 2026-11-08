#pragma once
#include "../../ScoreContext.h"
#include <optional>
#include <string>

namespace MikuMikuWorld
{
	class ScoreEditorTimeline;

	// The Preview Animation panel: keyframes per Score Preview part (screen, stage, each HUD piece) and
	// effects (shake, flash, fade), all placed at the cursor. Every finished edit is one undo step.
	class AnimationWindow
	{
	public:
		void draw(ScoreContext& context, ScoreEditorTimeline& timeline);

	private:
		int target{ 0 };					// index into Canvas::Animation::targets()
		bool targetPicked{ false };			// first look: jump to a part that already has keys
		std::optional<Score> editStart;		// the chart before the widget being dragged / typed in

		// call right after a widget that edits context.score in place
		void trackEdit(ScoreContext& context);
		void commit(ScoreContext& context, const Score& before);

		void drawKeys(ScoreContext& context, ScoreEditorTimeline& timeline);
		void drawEffects(ScoreContext& context, ScoreEditorTimeline& timeline);
	};
}
