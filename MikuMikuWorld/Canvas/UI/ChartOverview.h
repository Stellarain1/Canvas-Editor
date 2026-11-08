#pragma once
#include "../Core/ChartIssues.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../ImGui/imgui.h"
#include <vector>

namespace MikuMikuWorld
{
	class ScoreContext;

	// The strip beside the timeline that shows the whole chart at once: measure lines, notes,
	// holds, BPM / skill / FEVER markers, where the chart issues are, and a box for the part the
	// timeline is showing. Click or drag in it to scroll there. Replaces the plain scrollbar when
	// enabled (Settings > Canvas > Interface, or right-click the strip).
	class ChartOverview
	{
	public:
		struct View
		{
			int firstTick{};	// bottom of the timeline view
			int lastTick{};		// top of the timeline view
			int cursorTick{};
		};

		// Draws into [min, max]. Returns the tick the timeline should centre on when the user
		// clicks / drags in the strip, -1 otherwise.
		int draw(ScoreContext& context, const std::vector<Canvas::Issues::Issue>* issues, const ImVec2& min, const ImVec2& max, const View& view);

	private:
		bool dragging{ false };
		int endTickCache{ 0 };
	};
}
