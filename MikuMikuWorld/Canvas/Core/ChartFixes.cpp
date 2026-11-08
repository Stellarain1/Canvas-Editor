#include "ChartFixes.h"
#include "ChartIssues.h"
#include "../../Constants.h"
#include <algorithm>
#include <cstdlib>
#include <set>

using namespace MikuMikuWorld;

namespace Canvas::Fixes
{
	namespace
	{
		std::set<int> guidePoints(const Score& score)
		{
			std::set<int> ids;
			for (const auto& [id, hold] : score.holdNotes)
			{
				if (!hold.isGuide())
					continue;
				ids.insert(hold.start.ID);
				ids.insert(hold.end);
				for (const auto& step : hold.steps)
					ids.insert(step.ID);
			}
			return ids;
		}

		// the tap can sit at `lane` without touching or sharing a key with any other note there
		bool freeAt(const Score& score, const Note& tap, int lane, const std::set<int>& guides)
		{
			for (const auto& [id, other] : score.notes)
			{
				if (id == tap.ID || other.tick != tap.tick || guides.count(id))
					continue;
				const bool sameKey = other.lane == lane;
				const bool overlap = lane < other.lane + other.width && other.lane < lane + tap.width;
				if (sameKey || overlap)
					return false;
			}
			return true;
		}

		bool moveAside(Score& score, Note& tap, const Note& from, const std::set<int>& guides)
		{
			const double centre = tap.lane + tap.width * 0.5, otherCentre = from.lane + from.width * 0.5;
			const int preferred = centre >= otherCentre ? 1 : -1;
			for (int distance = 1; distance < NUM_LANES; ++distance)
				for (int sign : { preferred, -preferred })
				{
					const int lane = tap.lane + sign * distance;
					if (lane < 0 || lane + tap.width > NUM_LANES)
						continue;
					if (freeAt(score, tap, lane, guides))
					{
						tap.lane = lane;
						return true;
					}
				}
			return false;
		}
	}

	bool isOverlapCode(const std::string& code)
	{
		return code == "duplicate-note" || code == "notes-same-key" || code == "visible-overlap" || code == "hold-node-collision" ||
			code == "hidden-step-visible";
	}

	Report fixOverlaps(Score& score)
	{
		Report report;
		Issues::ScanOptions options;
		options.checkSkillFever = false;
		options.checkDensity = false;
		for (int round = 0; round < 64; ++round)
		{
			const auto issues = Issues::scanChart(score, options);
			const std::set<int> guides = guidePoints(score);
			std::set<int> touched;	// one change per note per round; the next scan sees the result
			int changes = 0, open = 0;
			for (const auto& issue : issues)
			{
				if (!isOverlapCode(issue.code))
					continue;
				++open;
				if (issue.noteIds.size() < 2)
					continue;
				auto a = score.notes.find(issue.noteIds[0]);
				auto b = score.notes.find(issue.noteIds[1]);
				if (a == score.notes.end() || b == score.notes.end() || touched.count(a->first) || touched.count(b->first))
					continue;
				const bool tapA = a->second.getType() == NoteType::Tap, tapB = b->second.getType() == NoteType::Tap;

				if (issue.code == "duplicate-note" && tapA && tapB)
				{
					// keep the first placed, with what either of them had
					Note& keep = a->first < b->first ? a->second : b->second;
					const Note& drop = a->first < b->first ? b->second : a->second;
					keep.critical = keep.critical || drop.critical;
					keep.friction = keep.friction && drop.friction;
					if (keep.flick == FlickType::None)
						keep.flick = drop.flick;
					touched.insert(keep.ID);
					touched.insert(drop.ID);
					report.log.push_back("merged two identical notes at tick " + std::to_string(keep.tick));
					score.notes.erase(drop.ID);
					++report.merged;
					++changes;
					continue;
				}

				// which one moves: a tap first (the later placed of two), then a hold's middle point,
				// then a hold end, then a hold start; guide points never move
				auto rank = [&](const Note& n) {
					if (guides.count(n.ID))
						return 9;
					switch (n.getType())
					{
					case NoteType::Tap: return 0;
					case NoteType::HoldMid: return 1;
					case NoteType::HoldEnd: return 2;
					default: return 3;
					}
				};
				const int ra = rank(a->second), rb = rank(b->second);
				const bool pickA = ra < rb || (ra == rb && a->first > b->first);
				Note* mover = pickA ? &a->second : &b->second;
				const Note* other = pickA ? &b->second : &a->second;
				if (rank(*mover) == 9)
					continue;
				const int before = mover->lane, beforeWidth = mover->width;
				bool fixed = moveAside(score, *mover, *other, guides);
				// no free lane at that width: try it narrower, down to one lane
				while (!fixed && mover->width > 1)
				{
					--mover->width;
					fixed = freeAt(score, *mover, mover->lane, guides) || moveAside(score, *mover, *other, guides);
				}
				if (!fixed)
				{
					mover->width = beforeWidth;
					// a tap with nowhere to go at all is dropped (the other note is on its spot anyway)
					if (mover->getType() == NoteType::Tap)
					{
						touched.insert(mover->ID);
						report.log.push_back("removed a tap at tick " + std::to_string(mover->tick) + " with no free lane");
						score.notes.erase(mover->ID);
						++report.merged;
						++changes;
					}
					continue;
				}
				touched.insert(mover->ID);
				report.log.push_back("moved a note at tick " + std::to_string(mover->tick) + " from lane " + std::to_string(before) + " to " + std::to_string(mover->lane) +
					(mover->width != beforeWidth ? " (width " + std::to_string(beforeWidth) + " to " + std::to_string(mover->width) + ")" : ""));
				++report.moved;
				++changes;
			}
			report.left = open - changes;
			if (changes == 0)
				break;
		}
		// the final count, after the last round's changes
		int left = 0;
		for (const auto& issue : Issues::scanChart(score, options))
			left += isOverlapCode(issue.code);
		report.left = left;
		return report;
	}
}
