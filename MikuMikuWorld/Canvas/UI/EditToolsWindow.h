#pragma once
#include "../Core/ChartTools.h"
#include <string>
#include <unordered_set>

namespace MikuMikuWorld
{
	class ScoreContext;
}

// Edit > Repeat Hold Mids... and Edit > Interpolate Hi-Speeds...: small dialogs over the tools
// in Canvas/Core/ChartTools. Each apply is one undo step.
namespace Canvas
{
	class EditToolsWindow
	{
	public:
		void openRepeatMids(const MikuMikuWorld::ScoreContext& context, int division);
		// fromTick / toTick: two selected hi-speeds, else the cursor and one measure later
		void openInterpolate(const MikuMikuWorld::ScoreContext& context, const std::unordered_set<int>& selectedHiSpeedTicks);
		void update(MikuMikuWorld::ScoreContext& context);

	private:
		bool repeatRequested{ false }, interpolateRequested{ false };
		int holdId{ -1 };
		std::vector<int> patternMids;
		Tools::RepeatMidsOptions repeat;
		int everyDivision{ 16 };
		Tools::InterpolateOptions interpolate;
		std::string status;

		void drawRepeat(MikuMikuWorld::ScoreContext& context);
		void drawInterpolate(MikuMikuWorld::ScoreContext& context);
	};
}
