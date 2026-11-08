#pragma once
#include <vector>

namespace MikuMikuWorld
{
	struct Score;

	struct ComboEvent
	{
		int tick{};
		float weight{};
		bool showJudge{};
	};

	std::vector<ComboEvent> calculateComboEvents(const Score& score);
	std::vector<int> calculateComboTicks(const Score& score);

	class ScoreStats
	{
	private:
		int taps, flicks, holds, steps, traces, total, combo;

		void resetCounts();
		void resetCombo();

	public:
		ScoreStats();

		void calculateStats(const Score& score);
		void calculateCombo(const Score& score);
		void reset();

		int getTaps() const { return taps; }
		int getFlicks() const { return flicks; }
		int getHolds() const { return holds; }
		int getSteps() const { return steps; }
		int getTraces() const { return traces; }
		int getTotal() const { return total; }
		int getCombo() const { return combo; }
	};
}
