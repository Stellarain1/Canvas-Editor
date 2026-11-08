#pragma once
#include "BeatDetect.h"
#include "../../Score.h"

// Chart-side BPM helpers: ramps, and writing a detected tempo map into a chart.
// The chart only stores step BPM changes (Score::tempoChanges), so a ramp is many small steps.
namespace Canvas::BpmTools
{
	enum class Curve { Linear, EaseIn, EaseOut, EaseInOut };

	// 0..1 -> 0..1
	double shape(Curve curve, double u);

	// Replaces the BPM changes in [fromTick, toTick] with a ramp from startBpm (at fromTick) to
	// endBpm (at toTick): one change every stepTicks, the last one exactly on toTick. Changes outside
	// the range stay. The BPM is clamped to the editor's range, equal neighbours are not repeated.
	// Returns how many changes the range has now. Needs fromTick < toTick and stepTicks > 0.
	int interpolateTempo(MikuMikuWorld::Score& score, int fromTick, int toTick, float startBpm, float endBpm, int stepTicks,
		Curve curve = Curve::Linear);

	// Writes the candidate's tempo map and time signatures (and the music offset) into the score.
	// keepNoteTicks: notes stay on their ticks (they move in time with the new beats).
	// Otherwise every tick-bound thing (notes, hi-speeds, SE volumes, skills, FEVER) is moved so it
	// keeps its song time under the new map; ticks are rounded, never snapped.
	void applyTiming(MikuMikuWorld::Score& score, const BeatDetect::Timing& timing, bool keepNoteTicks);

	// The retiming alone: a tick under `oldTempos` / `oldOffsetMs` goes to the tick with the same song
	// time under `newTempos` / `newOffsetMs` (audio time = chart time - offset).
	void retimeScore(MikuMikuWorld::Score& score, const std::vector<MikuMikuWorld::Tempo>& oldTempos, double oldOffsetMs,
		const std::vector<MikuMikuWorld::Tempo>& newTempos, double newOffsetMs);

	// Chart time (s) of a tick / tick of a chart time (s), in double precision.
	double tickToSeconds(int tick, const std::vector<MikuMikuWorld::Tempo>& tempos);
	double secondsToTick(double seconds, const std::vector<MikuMikuWorld::Tempo>& tempos);
}
