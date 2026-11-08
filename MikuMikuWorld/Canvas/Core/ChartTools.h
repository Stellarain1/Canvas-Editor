#pragma once
#include "../../Score.h"
#include <string>
#include <vector>

// Editing tools added in 1.2.1: repeating hold mids along a hold, filling in hi-speed changes so
// the speed glides between two values, and division maths for any division up to 1920.
namespace Canvas::Tools
{
	// ---- divisions --------------------------------------------------------------------------------

	inline constexpr int MAX_DIVISION = 1920;	// one tick per step in 4/4 at 480 ticks per beat

	// Ticks between grid lines for "<division> notes per 4/4 measure" (may be fractional: 768 div
	// is 2.5 ticks).
	double divisionStep(int division);
	// Nearest grid tick at `division`, counted from the start of the measure `tick` is in.
	int snapToDivision(int tick, int division, const MikuMikuWorld::Score& score);
	// The grid tick after / before `tick` (strictly).
	int nextDivisionTick(int tick, int division, const MikuMikuWorld::Score& score);
	int previousDivisionTick(int tick, int division, const MikuMikuWorld::Score& score);

	// ---- repeat hold mids -------------------------------------------------------------------------

	struct RepeatMidsOptions
	{
		enum class Mode { Pattern, EveryStep };
		Mode mode{ Mode::Pattern };
		// Pattern: repeat the chosen mids (their offsets from the point before the first one)
		bool untilEnd{ true };	// else `count` repeats
		int count{ 4 };
		bool mirror{ false };	// every other repeat mirrored (left <-> right)
		bool relative{ false };	// keep moving like the pattern does instead of returning to its lanes
		// EveryStep: a mid every `stepTicks` along the hold, on its current path
		int stepTicks{ 120 };
		MikuMikuWorld::HoldStepType stepType{ MikuMikuWorld::HoldStepType::Normal };
		MikuMikuWorld::EaseType ease{ MikuMikuWorld::EaseType::Linear };
	};

	struct RepeatMidsResult
	{
		int added{};
		std::string error;	// empty on success
	};

	// The hold that `noteIds` belong to (a hold start / mid / end of one hold); -1 if they are of
	// none or of several.
	int holdOfSelection(const MikuMikuWorld::Score& score, const std::vector<int>& noteIds);

	// `patternMids`: the mids to repeat (Pattern mode); ignored for EveryStep.
	RepeatMidsResult repeatHoldMids(MikuMikuWorld::Score& score, int holdId, const std::vector<int>& patternMids, const RepeatMidsOptions& options);

	// ---- hi-speed interpolation ------------------------------------------------------------------

	enum class SpeedEase { Linear, In, Out, InOut };

	struct InterpolateOptions
	{
		int fromTick{ 0 }, toTick{ 1920 };
		float fromSpeed{ 1.0f }, toSpeed{ 2.0f };
		int stepTicks{ 60 };
		SpeedEase ease{ SpeedEase::Linear };
		bool replace{ true };	// drop the hi-speeds already between the two ticks
		int layer{ 0 };
	};

	// Writes hi-speed changes from fromTick to toTick, every stepTicks, so the speed goes from
	// fromSpeed to toSpeed along `ease`. Returns how many changes it added.
	int interpolateHiSpeeds(MikuMikuWorld::Score& score, const InterpolateOptions& options);

	// The hi-speed in force at `tick` on `layer` (1 when none).
	float hiSpeedAt(const MikuMikuWorld::Score& score, int tick, int layer);
}
