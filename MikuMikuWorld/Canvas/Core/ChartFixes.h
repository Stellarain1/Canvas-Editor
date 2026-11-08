#pragma once
#include "../../Score.h"
#include <string>
#include <vector>

// Automatic fixes for chart issues that have one obviously right answer.
namespace Canvas::Fixes
{
	struct Report
	{
		int merged{};		// duplicate notes folded into one
		int moved{};		// taps moved sideways off another note
		int left{};			// overlap issues still there (need a person: hold points on hold points)
		std::vector<std::string> log;
		bool changed() const { return merged + moved > 0; }
	};

	// The Issue List codes fixOverlaps works on.
	bool isOverlapCode(const std::string& code);

	// Resolves notes that overlap at the same tick without changing any timing:
	// - two identical taps on one spot become one (keeping critical / flick if either had it);
	// - a tap that overlaps or shares a .sus key with another note moves sideways to the nearest
	//   free spot at that tick, away from the note it covered;
	// - hold points on hold points are left alone (moving them would reshape the holds) and
	//   counted in `left`.
	Report fixOverlaps(MikuMikuWorld::Score& score);
}
