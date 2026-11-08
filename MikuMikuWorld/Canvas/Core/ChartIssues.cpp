#include "ChartIssues.h"
#include "ChartTransforms.h"
#include "SkillFever.h"
#include "../../Constants.h"
#include "../../IO.h"
#include "../../MathUtils.h"
#include "../../Tempo.h"
#include "../../ScoreStats.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

using namespace MikuMikuWorld;

namespace Canvas::Issues
{
	namespace
	{
		// Every position a note occupies at an instant, as the exporters see it
		enum class PointKind
		{
			Tap,
			HoldStart,
			HoldEnd,
			Step,		// visible relay tick
			HiddenStep,
			SkipStep,	// attached tick
			GuidePoint
		};

		const char* pointKindName(PointKind kind)
		{
			switch (kind)
			{
			case PointKind::Tap: return "a tap note";
			case PointKind::HoldStart: return "a hold start";
			case PointKind::HoldEnd: return "a hold end";
			case PointKind::Step: return "a hold step";
			case PointKind::HiddenStep: return "a hidden hold step";
			case PointKind::SkipStep: return "a skip (attached) step";
			case PointKind::GuidePoint: return "a guide point";
			}
			return "a note";
		}

		// "Hold End" style names for headlines
		const char* pointKindTitle(PointKind kind)
		{
			switch (kind)
			{
			case PointKind::Tap: return "Tap";
			case PointKind::HoldStart: return "Hold Start";
			case PointKind::HoldEnd: return "Hold End";
			case PointKind::Step: return "Hold Step";
			case PointKind::HiddenStep: return "Hidden Step";
			case PointKind::SkipStep: return "Skip Step";
			case PointKind::GuidePoint: return "Guide Point";
			}
			return "Note";
		}

		bool isHoldNode(PointKind kind)
		{
			return kind == PointKind::HoldStart || kind == PointKind::HoldEnd || kind == PointKind::Step || kind == PointKind::SkipStep;
		}

		// Things the player actually has to hit or that are drawn as a note head
		bool isNoteLike(PointKind kind)
		{
			return kind == PointKind::Tap || kind == PointKind::HoldStart || kind == PointKind::HoldEnd || kind == PointKind::Step || kind == PointKind::SkipStep;
		}

		bool isHitNote(PointKind kind)
		{
			return kind == PointKind::Tap || kind == PointKind::HoldStart || kind == PointKind::HoldEnd;
		}

		struct Point
		{
			int tick;
			int lane;
			int width;
			PointKind kind;
			int noteId;
			int holdId;	// -1 for taps
		};

		struct Collector
		{
			std::vector<Issue>& out;

			void add(Severity severity, Category category, const char* code, std::string message, int tick = -1, std::vector<int> ids = {}, int lane = -1, int width = 1)
			{
				Issue issue;
				issue.severity = severity;
				issue.category = category;
				issue.code = code;
				issue.title = issueTitle(code);
				issue.message = std::move(message);
				issue.tick = tick;
				issue.noteIds = std::move(ids);
				issue.lane = lane;
				issue.width = lane >= 0 ? std::max(1, width) : 0;
				out.push_back(std::move(issue));
			}
		};

		double timeAt(const Score& score, int tick)
		{
			return accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges);
		}

		// lane interval covered by a hold at `tick` (following eases, skip steps ignored)
		struct HoldShape
		{
			int holdId;
			int startTick, endTick;
			std::vector<const Note*> joints;
			std::vector<EaseType> eases;	// ease of the segment starting at each joint
			bool invisible{ false };		// hidden start and hidden end: an effect path, not something to hold

			bool intervalAt(int tick, float& left, float& right) const
			{
				if (tick < startTick || tick > endTick || joints.size() < 2)
					return false;

				for (size_t i = 0; i + 1 < joints.size(); ++i)
				{
					const Note& a = *joints[i];
					const Note& b = *joints[i + 1];
					if (tick < a.tick || tick > b.tick)
						continue;

					const float t = b.tick == a.tick ? 0.0f : static_cast<float>(tick - a.tick) / static_cast<float>(b.tick - a.tick);
					auto ease = getEaseFunction(eases[i]);
					left = ease(static_cast<float>(a.lane), static_cast<float>(b.lane), t);
					right = ease(static_cast<float>(a.lane + a.width), static_cast<float>(b.lane + b.width), t);
					return true;
				}
				return false;
			}
		};

		std::vector<HoldShape> buildShapes(const Score& score, bool guides)
		{
			std::vector<HoldShape> shapes;
			for (const auto& [id, hold] : score.holdNotes)
			{
				if (hold.isGuide() != guides)
					continue;

				auto startIt = score.notes.find(hold.start.ID);
				auto endIt = score.notes.find(hold.end);
				if (startIt == score.notes.end() || endIt == score.notes.end())
					continue;

				HoldShape shape;
				shape.holdId = id;
				shape.invisible = hold.startType == HoldNoteType::Hidden && hold.endType == HoldNoteType::Hidden;
				shape.joints.push_back(&startIt->second);
				shape.eases.push_back(hold.start.ease);
				for (const auto& step : hold.steps)
				{
					if (step.type == HoldStepType::Skip)
						continue;
					auto it = score.notes.find(step.ID);
					if (it == score.notes.end())
						continue;
					shape.joints.push_back(&it->second);
					shape.eases.push_back(step.ease);
				}
				shape.joints.push_back(&endIt->second);
				shape.eases.push_back(EaseType::Linear);

				std::stable_sort(shape.joints.begin(), shape.joints.end(), [](const Note* a, const Note* b) { return a->tick < b->tick; });
				shape.startTick = shape.joints.front()->tick;
				shape.endTick = shape.joints.back()->tick;
				shapes.push_back(std::move(shape));
			}
			return shapes;
		}

		std::string laneRange(int lane, int width)
		{
			// lanes shown 1-12 to people
			return width <= 1 ? IO::formatString("lane %d", lane + 1) : IO::formatString("lanes %d-%d", lane + 1, lane + width);
		}

		// ------------------------------------------------------------------ checks

		void checkStructure(const Score& score, Collector& c)
		{
			if (score.notes.empty())
			{
				c.add(Severity::Info, Category::Structure, "empty-chart", "The chart has no notes.");
				return;
			}

			for (const auto& [id, note] : score.notes)
			{
				if (note.tick < 0)
					c.add(Severity::Error, Category::Structure, "negative-tick", "A note is placed before the start of the chart (negative tick).", 0, { id });

				if (note.width < MIN_NOTE_WIDTH || note.width > MAX_NOTE_WIDTH)
					c.add(Severity::Error, Category::Lanes, "bad-width", IO::formatString("A note is %d lanes wide; notes must be 1-12 lanes wide.", note.width), note.tick, { id }, note.lane, note.width);

				if (note.lane < MIN_LANE || note.lane + note.width - 1 > MAX_LANE)
					c.add(Severity::Error, Category::Lanes, "lane-out-of-range",
						IO::formatString("A note covers %s, outside the 12 lanes.", laneRange(note.lane, note.width).c_str()), note.tick, { id }, note.lane, note.width);

				if ((note.getType() == NoteType::Tap || note.getType() == NoteType::Hold) && (!std::isfinite(note.speedRatio) || note.speedRatio <= 0.0f))
					c.add(Severity::Error, Category::HiSpeed, "bad-speed-ratio", "A note has a speed ratio of zero, negative or not-a-number; it would never reach the judgement line.", note.tick, { id });
				else if ((note.getType() == NoteType::Tap || note.getType() == NoteType::Hold) && (note.speedRatio > 20.0f || note.speedRatio < 0.05f))
					c.add(Severity::Warning, Category::HiSpeed, "extreme-speed-ratio",
						IO::formatString("A note scrolls at %sx its normal speed; it will flash past or crawl.", IO::formatFixedFloatTrimmed(note.speedRatio).c_str()), note.tick, { id });

				if (note.getType() == NoteType::HoldMid || note.getType() == NoteType::HoldEnd)
				{
					if (score.holdNotes.find(note.parentID) == score.holdNotes.end())
						c.add(Severity::Error, Category::Holds, "orphan-hold-point", "A hold step or end is not attached to any hold.", note.tick, { id });
				}

				if (note.layer < 0 || note.layer >= static_cast<int>(score.layers.size()))
					c.add(Severity::Error, Category::HiSpeed, "bad-layer", IO::formatString("A note is on hi-speed layer %d, which does not exist.", note.layer), note.tick, { id });
			}

			for (const auto& [id, hold] : score.holdNotes)
			{
				auto startIt = score.notes.find(hold.start.ID);
				auto endIt = score.notes.find(hold.end);
				if (startIt == score.notes.end() || endIt == score.notes.end())
				{
					c.add(Severity::Error, Category::Holds, "broken-hold", "A hold is missing its start or end note.", startIt != score.notes.end() ? startIt->second.tick : -1, { id });
					continue;
				}

				const Note& start = startIt->second;
				const Note& end = endIt->second;
				const char* what = hold.isGuide() ? "guide" : "hold";

				if (end.tick <= start.tick)
					c.add(Severity::Error, Category::Holds, "hold-zero-length",
						end.tick == start.tick ? IO::formatString("A %s starts and ends on the same tick.", what) : IO::formatString("A %s ends before it starts.", what),
						start.tick, { start.ID, end.ID });

				for (const auto& step : hold.steps)
				{
					auto stepIt = score.notes.find(step.ID);
					if (stepIt == score.notes.end())
					{
						c.add(Severity::Error, Category::Holds, "broken-hold", IO::formatString("A %s refers to a step that does not exist.", what), start.tick, { id });
						continue;
					}

					const Note& mid = stepIt->second;
					if (mid.tick < start.tick || mid.tick > end.tick)
						c.add(Severity::Error, Category::Holds, "step-outside-hold", IO::formatString("A %s step lies outside its %s (before the start or after the end).", what, what), mid.tick, { mid.ID, start.ID });
					else if ((mid.tick == start.tick || mid.tick == end.tick) && step.type != HoldStepType::Skip)
						c.add(Severity::Warning, Category::Holds, "step-on-endpoint", IO::formatString("A %s step sits on the same tick as the %s's %s.", what, what, mid.tick == start.tick ? "start" : "end"), mid.tick, { mid.ID });
				}

				if (!hold.isGuide() && end.critical && !start.critical && !end.isFlick() && !end.friction)
					c.add(Severity::Info, Category::Holds, "critical-end-only", "Only the end of this hold is critical; the game only keeps that for flick or trace ends.", end.tick, { end.ID });

				if (hold.isGuide())
				{
					// guides with the same colour never need skip/normal steps
					for (const auto& step : hold.steps)
						if (step.type != HoldStepType::Hidden)
							c.add(Severity::Warning, Category::HiddenSteps, "guide-visible-step", "A guide has a visible step; guide steps are always hidden in-game.", score.notes.count(step.ID) ? score.notes.at(step.ID).tick : start.tick, { step.ID });
				}
			}
		}

		void checkPoints(const Score& score, Collector& c, const ScanOptions& options)
		{
			std::vector<Point> points;
			points.reserve(score.notes.size());
			for (const auto& [id, note] : score.notes)
			{
				switch (note.getType())
				{
				case NoteType::Tap:
					points.push_back({ note.tick, note.lane, note.width, PointKind::Tap, id, -1 });
					break;
				case NoteType::Hold:
				{
					auto it = score.holdNotes.find(id);
					if (it == score.holdNotes.end())
						break;
					const PointKind kind = it->second.isGuide() ? PointKind::GuidePoint : (it->second.startType == HoldNoteType::Hidden ? PointKind::HiddenStep : PointKind::HoldStart);
					points.push_back({ note.tick, note.lane, note.width, kind, id, id });
					break;
				}
				case NoteType::HoldEnd:
				{
					auto it = score.holdNotes.find(note.parentID);
					if (it == score.holdNotes.end())
						break;
					const PointKind kind = it->second.isGuide() ? PointKind::GuidePoint : (it->second.endType == HoldNoteType::Hidden ? PointKind::HiddenStep : PointKind::HoldEnd);
					points.push_back({ note.tick, note.lane, note.width, kind, id, note.parentID });
					break;
				}
				case NoteType::HoldMid:
				{
					auto it = score.holdNotes.find(note.parentID);
					if (it == score.holdNotes.end())
						break;
					const HoldNote& hold = it->second;
					const int index = findHoldStep(hold, id);
					PointKind kind = PointKind::Step;
					if (hold.isGuide())
						kind = PointKind::GuidePoint;
					else if (index >= 0 && hold.steps[index].type == HoldStepType::Hidden)
						kind = PointKind::HiddenStep;
					else if (index >= 0 && hold.steps[index].type == HoldStepType::Skip)
						kind = PointKind::SkipStep;
					points.push_back({ note.tick, note.lane, note.width, kind, id, note.parentID });
					break;
				}
				default:
					break;
				}
			}

			std::sort(points.begin(), points.end(), [](const Point& a, const Point& b)
			{
				return a.tick != b.tick ? a.tick < b.tick : a.lane < b.lane;
			});

			// reports once per pair of notes
			std::set<std::pair<int, int>> reported;
			auto once = [&reported](int a, int b) { return reported.insert({ std::min(a, b), std::max(a, b) }).second; };

			int denseMoments = 0, firstDenseTick = -1, maxSimultaneous = 0;
			std::vector<int> firstDenseIds;
			for (size_t begin = 0; begin < points.size();)
			{
				size_t end = begin;
				while (end < points.size() && points[end].tick == points[begin].tick)
					++end;

				int hitNotes = 0;
				std::vector<int> hitIds;
				for (size_t i = begin; i < end; ++i)
				{
					const Point& a = points[i];
					if (isHitNote(a.kind))
					{
						++hitNotes;
						hitIds.push_back(a.noteId);
					}

					for (size_t j = i + 1; j < end; ++j)
					{
						const Point& b = points[j];
						if (a.holdId >= 0 && a.holdId == b.holdId)
						{
							if (a.noteId != b.noteId && once(a.noteId, b.noteId) && a.kind != PointKind::GuidePoint)
								c.add(Severity::Warning, Category::Holds, "hold-points-same-tick", "Two points of the same hold are on the same tick.", a.tick, { a.noteId, b.noteId }, a.lane, a.width);
							continue;
						}

						const bool sameKey = a.lane == b.lane;	// .sus identifies a note by tick + left lane
						const bool overlap = a.lane < b.lane + b.width && b.lane < a.lane + a.width;
						if (!sameKey && !overlap)
							continue;

						// guides are decoration: overlapping them is fine, sharing a .sus key is not
						if (a.kind == PointKind::GuidePoint || b.kind == PointKind::GuidePoint)
						{
							if (sameKey && once(a.noteId, b.noteId))
							{
								// official charts do this all the time; it only matters when the two
								// disagree about being critical, because .sus stores that per key
								const Point& guidePoint = a.kind == PointKind::GuidePoint ? a : b;
								const Point& other = a.kind == PointKind::GuidePoint ? b : a;
								// (only the guide's start decides its colour)
								const auto guideNote = score.notes.find(guidePoint.noteId), otherNote = score.notes.find(other.noteId);
								const auto guideHold = score.holdNotes.find(guidePoint.holdId);
								const bool isGuideStart = guideHold != score.holdNotes.end() && guideHold->second.start.ID == guidePoint.noteId;
								if (isGuideStart && other.kind != PointKind::GuidePoint && guideNote != score.notes.end() && otherNote != score.notes.end() &&
									guideNote->second.critical != otherNote->second.critical)
									c.add(Severity::Warning, Category::Overlap, "guide-shares-key",
										IO::formatString("A %s guide point shares its tick and left lane with %s%s. In .sus they share one key, so after export the guide turns %s.",
											guideNote->second.critical ? "yellow" : "green", otherNote->second.critical ? "a critical " : "a non-critical ",
											pointKindName(other.kind) + 2, otherNote->second.critical ? "yellow" : "green"),
										a.tick, { a.noteId, b.noteId }, a.lane, a.width);
							}
							continue;
						}

						if (a.kind == PointKind::HiddenStep || b.kind == PointKind::HiddenStep)
						{
							if (sameKey && once(a.noteId, b.noteId))
							{
								const Point& other = a.kind == PointKind::HiddenStep ? b : a;
								c.add(Severity::Error, Category::HiddenSteps, "hidden-step-visible",
									IO::formatString("A hidden step (or hidden hold end) shares its tick and left lane with %s of another note. .sus merges notes by position, so the hidden point turns into a visible note or the other note disappears in-game.", pointKindName(other.kind)),
									a.tick, { a.noteId, b.noteId }, a.lane, std::max(a.width, b.width));
							}
							continue;
						}

						if (!isNoteLike(a.kind) || !isNoteLike(b.kind) || !once(a.noteId, b.noteId))
							continue;

						// "earlier" = placed first (lower id); that is the one drawn underneath
						const Point& first = a.noteId < b.noteId ? a : b;
						const Point& later = a.noteId < b.noteId ? b : a;

						if (sameKey && a.width == b.width && isHoldNode(a.kind) && isHoldNode(b.kind))
						{
							c.add(Severity::Error, Category::HoldNodeCollision, "hold-node-collision",
								IO::formatString("%s sits exactly on %s of another hold (%s). .sus stores notes by tick and lane, so on export the two merge into one: a hold loses a point or two holds get joined.",
									pointKindTitle(later.kind), pointKindName(first.kind), laneRange(a.lane, a.width).c_str()),
								a.tick, { a.noteId, b.noteId }, a.lane, a.width);
						}
						else if (sameKey && a.width == b.width && a.kind == PointKind::Tap && b.kind == PointKind::Tap)
						{
							c.add(Severity::Error, Category::Overlap, "duplicate-note",
								IO::formatString("Two notes are stacked on exactly the same spot (%s). The game keeps only one of them.", laneRange(a.lane, a.width).c_str()),
								a.tick, { a.noteId, b.noteId }, a.lane, a.width);
						}
						else if (sameKey)
						{
							c.add(Severity::Error, Category::Overlap, "notes-same-key",
								IO::formatString("%s and %s share a tick and left lane (%s). .sus merges notes by position: one disappears or they swap flags.",
									pointKindTitle(a.kind), pointKindName(b.kind), laneRange(a.lane, std::max(a.width, b.width)).c_str()),
								a.tick, { a.noteId, b.noteId }, a.lane, std::max(a.width, b.width));
						}
						else
						{
							const int lo = std::max(a.lane, b.lane), hi = std::min(a.lane + a.width, b.lane + b.width) - 1;
							const bool sharedEdge = a.lane + a.width == b.lane + b.width;
							c.add(Severity::Warning, Category::VisibleOverlap, "visible-overlap",
								IO::formatString("%s overlaps with earlier %s at the same tick%s (%s). The later note may be hidden.",
									pointKindTitle(later.kind), pointKindTitle(first.kind), sharedEdge ? " and shared edge" : "", laneRange(lo, hi - lo + 1).c_str()),
								a.tick, { a.noteId, b.noteId }, lo, hi - lo + 1);
						}
					}
				}

				if (options.checkDensity && hitNotes > 2)
				{
					if (denseMoments++ == 0)
					{
						firstDenseTick = points[begin].tick;
						firstDenseIds = hitIds;
					}
					maxSimultaneous = std::max(maxSimultaneous, hitNotes);
				}

				begin = end;
			}

			if (denseMoments > 0)
				c.add(Severity::Info, Category::Density, "many-simultaneous",
					IO::formatString("%d moment%s more than two notes hit at once (up to %d); the first is shown. Normal for APPEND, unusual elsewhere.",
						denseMoments, denseMoments == 1 ? " needs" : "s need", maxSimultaneous), firstDenseTick, firstDenseIds);
		}

		void checkHoldOverlaps(const Score& score, Collector& c)
		{
			std::vector<HoldShape> shapes = buildShapes(score, false);
			std::sort(shapes.begin(), shapes.end(), [](const HoldShape& a, const HoldShape& b) { return a.startTick < b.startTick; });

			for (size_t i = 0; i < shapes.size(); ++i)
			{
				const HoldShape& a = shapes[i];
				if (a.invisible)
					continue;
				for (size_t j = i + 1; j < shapes.size() && shapes[j].startTick < a.endTick; ++j)
				{
					const HoldShape& b = shapes[j];
					if (b.invisible)
						continue;
					const int from = std::max(a.startTick, b.startTick);
					const int to = std::min(a.endTick, b.endTick);
					if (to <= from)
						continue;

					// sample at every joint of either hold inside the shared window, plus a
					// regular grid so eased curves are not missed
					std::set<int> samples;
					for (const Note* n : a.joints) if (n->tick >= from && n->tick <= to) samples.insert(n->tick);
					for (const Note* n : b.joints) if (n->tick >= from && n->tick <= to) samples.insert(n->tick);
					const int step = std::max(1, (to - from) / 32);
					for (int t = from; t <= to; t += step)
						samples.insert(t);
					samples.insert(to);

					// Official charts overlap and cross holds on purpose all the time. What goes
					// wrong is two holds lying (almost) on top of each other for a while: the player
					// sees one path. So: paths that share at least 75% of their width, for half a beat.
					constexpr int SUSTAIN_TICKS = TICKS_PER_BEAT / 2;
					int overlapTick = -1;
					int runStart = -1;
					int previousSign = 0;
					int crossTick = -1;
					for (int t : samples)
					{
						// the instant one hold starts exactly where the other ends is a chain, not an overlap
						if ((t == a.endTick && t == b.startTick) || (t == b.endTick && t == a.startTick))
							continue;

						float al, ar, bl, br;
						if (!a.intervalAt(t, al, ar) || !b.intervalAt(t, bl, br))
							continue;

						// measured against the wider hold, so a wide effect slide sweeping over a
						// normal hold (official charts do this) does not count
						const float covered = std::min(ar, br) - std::max(al, bl);
						const float wider = std::max(ar - al, br - bl);
						if (covered > 0.5f && covered >= 0.75f * wider)
						{
							if (runStart < 0)
								runStart = t;
							if (t - runStart >= SUSTAIN_TICKS)
							{
								overlapTick = runStart;
								break;
							}
						}
						else
						{
							runStart = -1;
						}

						const int sign = (al + ar) < (bl + br) ? -1 : 1;
						if (previousSign != 0 && sign != previousSign && crossTick < 0)
							crossTick = t;
						previousSign = sign;
					}

					const std::vector<int> ids{ a.holdId, b.holdId };
					if (overlapTick >= 0)
						c.add(Severity::Info, Category::Holds, "holds-overlap",
							"Two holds lie on top of each other for a while, so they look like one path. Some APPEND charts stack holds on purpose; elsewhere it is usually a mistake.", overlapTick, ids);
					else if (crossTick >= 0)
						c.add(Severity::Info, Category::Holds, "holds-cross",
							"Two holds cross each other; the paths pass through each other between their steps.", crossTick, ids);
				}
			}
		}

		void checkTiming(const Score& score, Collector& c, const ScanOptions& options)
		{
			if (score.tempoChanges.empty())
			{
				c.add(Severity::Error, Category::Timing, "no-bpm", "The chart has no BPM.");
				return;
			}

			std::map<int, int> tempoTicks;
			bool hasZero = false;
			for (const auto& tempo : score.tempoChanges)
			{
				hasZero |= tempo.tick == 0;
				if (!std::isfinite(tempo.bpm) || tempo.bpm <= 0)
					c.add(Severity::Error, Category::Timing, "bad-bpm", IO::formatString("A BPM change has an invalid value (%g).", tempo.bpm), tempo.tick);
				else if (tempo.bpm < MIN_BPM || tempo.bpm > MAX_BPM)
					c.add(Severity::Warning, Category::Timing, "extreme-bpm", IO::formatString("A BPM change of %g is outside %g-%g.", tempo.bpm, MIN_BPM, MAX_BPM), tempo.tick);
				if (++tempoTicks[tempo.tick] == 2)
					c.add(Severity::Warning, Category::Timing, "bpm-duplicate", "Two BPM changes are on the same tick; only one of them takes effect.", tempo.tick);
			}
			if (!hasZero)
				c.add(Severity::Error, Category::Timing, "no-bpm-at-start", "There is no BPM at the very start of the chart (tick 0).", 0);

			for (const auto& [measure, ts] : score.timeSignatures)
			{
				if (ts.numerator < MIN_TIME_SIGNATURE || ts.denominator < MIN_TIME_SIGNATURE)
					c.add(Severity::Error, Category::Timing, "bad-time-signature", IO::formatString("Time signature %d/%d at measure %d is invalid.", ts.numerator, ts.denominator, measure), measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures));
				else if ((ts.denominator & (ts.denominator - 1)) != 0)
					c.add(Severity::Warning, Category::Timing, "odd-time-signature", IO::formatString("Time signature %d/%d: the denominator is not a power of two, which .sus cannot express exactly.", ts.numerator, ts.denominator), measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures));
			}

			int firstTick = 0, lastTick = 0;
			if (!playableTickRange(score, firstTick, lastTick))
				return;

			const double offsetSeconds = score.metadata.musicOffset / 1000.0;
			const double lastTime = timeAt(score, lastTick);
			if (options.musicDurationSeconds > 0 && lastTime - offsetSeconds > options.musicDurationSeconds + 0.001)
				c.add(Severity::Warning, Category::Timing, "notes-after-music",
					IO::formatString("Notes continue %.1f s after the music ends.", lastTime - offsetSeconds - options.musicDurationSeconds), lastTick);

			const double firstTime = timeAt(score, firstTick);
			if (firstTime < 1.0)
				c.add(Severity::Info, Category::Timing, "first-note-early", IO::formatString("The first note comes %.2f s into the chart; players usually get a couple of seconds.", firstTime), firstTick);
		}

		void checkHiSpeedReadability(const Score& score, Collector& c);

		void checkHiSpeed(const Score& score, Collector& c)
		{
			std::map<std::pair<int, int>, int> seen;
			for (const auto& change : score.hiSpeedChanges)
			{
				if (change.tick < 0)
					c.add(Severity::Error, Category::HiSpeed, "hispeed-negative-tick", "A hi-speed change is placed before the start of the chart.", 0);

				if (change.layer < 0 || change.layer >= static_cast<int>(score.layers.size()))
					c.add(Severity::Error, Category::HiSpeed, "bad-layer", IO::formatString("A hi-speed change is on layer %d, which does not exist.", change.layer), change.tick);

				if (!std::isfinite(change.speed))
					c.add(Severity::Error, Category::HiSpeed, "hispeed-not-finite", "A hi-speed change is not a number; notes after it cannot be drawn.", change.tick);
				else if (change.speed < 0)
					c.add(Severity::Warning, Category::HiSpeed, "hispeed-negative",
						IO::formatString("Hi-speed %sx makes notes scroll backwards; some players and the preview video may draw it wrong.", IO::formatFixedFloatTrimmed(change.speed).c_str()), change.tick);
				else if (change.speed == 0)
					c.add(Severity::Info, Category::HiSpeed, "hispeed-zero", "Hi-speed 0 stops the notes where they are.", change.tick);
				else if (std::abs(change.speed) > 20.0f)
					c.add(Severity::Info, Category::HiSpeed, "hispeed-extreme",
						IO::formatString("Hi-speed %sx: notes will cross the screen in a few frames.", IO::formatFixedFloatTrimmed(change.speed).c_str()), change.tick);

				if (++seen[{ change.layer, change.tick }] == 2)
					c.add(Severity::Warning, Category::HiSpeed, "hispeed-duplicate", "Two hi-speed changes are on the same tick; which one the game uses is a coin flip.", change.tick);
			}

			if (usesLayers(score))
			{
				int notesOffDefault = 0;
				int firstTick = -1;
				for (const auto& [id, note] : score.notes)
				{
					if (note.layer != 0 && (note.getType() == NoteType::Tap || note.getType() == NoteType::Hold))
					{
						++notesOffDefault;
						if (firstTick < 0 || note.tick < firstTick)
							firstTick = note.tick;
					}
				}
				c.add(Severity::Warning, Category::HiSpeed, "layers-not-in-game",
					IO::formatString("%d note(s) are on hi-speed layers other than the default. Project SEKAI only reads #TIL00: exporting to .sus / MusicScoreData moves them to the default layer.", notesOffDefault),
					firstTick);
			}

			checkHiSpeedReadability(score, c);

			for (size_t layer = 0; layer < score.layers.size(); ++layer)
			{
				if (!score.layers[layer].hidden)
					continue;
				for (const auto& [id, note] : score.notes)
				{
					if (note.layer == static_cast<int>(layer))
					{
						c.add(Severity::Info, Category::HiSpeed, "hidden-layer-has-notes",
							IO::formatString("Layer \"%s\" is hidden in the editor but its notes are still exported.", score.layers[layer].name.c_str()), note.tick, { id });
						break;
					}
				}
			}
		}

		// Hi-speed readability: simulates the scroll the way the game draws it (each note's position
		// is its scaled time minus the scaled time now, over its on-screen window) at a typical
		// player note speed, and reports what players actually trip over. Nothing here can be sure
		// that a gimmick is unintended, so these are warnings / infos with the reason spelt out.
		void checkHiSpeedReadability(const Score& score, Collector& c)
		{
			if (score.hiSpeedChanges.empty() || score.tempoChanges.empty())
				return;
			constexpr double NOTE_SPEED = 10.0;	// the in-game setting most players use (10-11)
			const double window = lerpD(0.35, 4.0, std::pow(unlerpD(12, 1, NOTE_SPEED), 1.31));	// seconds on screen at 1x

			// the notes a player reads: taps, hold starts and visible hold ends (no guides, no hidden ends)
			std::set<int> skip;
			for (const auto& [id, hold] : score.holdNotes)
			{
				if (hold.isGuide())
				{
					skip.insert(hold.start.ID);
					skip.insert(hold.end);
				}
				else if (hold.endType == HoldNoteType::Hidden)
					skip.insert(hold.end);
			}
			struct Read
			{
				int id, tick, lane, width, layer;
				double time, scaled, span;	// real hit time, scaled hit time, scaled on-screen window
			};
			const auto layerSpeeds = splitHiSpeedLayers(score);
			std::vector<Read> notes;
			for (const auto& [id, note] : score.notes)
			{
				const NoteType t = note.getType();
				if ((t != NoteType::Tap && t != NoteType::Hold && t != NoteType::HoldEnd) || skip.count(id) || note.tick < 0)
					continue;
				const int layer = std::clamp(note.layer, 0, static_cast<int>(layerSpeeds.size()) - 1);
				const double ratio = std::max(0.01f, getEffectiveSpeedRatio(note, score));
				notes.push_back({ id, note.tick, note.lane, note.width, layer, accumulateDuration(note.tick, TICKS_PER_BEAT, score.tempoChanges),
					accumulateScaledDuration(note.tick, TICKS_PER_BEAT, score.tempoChanges, layerSpeeds[layer]), window / ratio });
			}
			if (notes.empty())
				return;
			std::sort(notes.begin(), notes.end(), [](const Read& a, const Read& b) { return a.time < b.time; });

			// the scaled clock of each layer, sampled in real time
			// notes scroll in before the chart starts, so the clock starts a few seconds early (at the
			// speed in force at tick 0)
			constexpr double DT = 1.0 / 240.0;
			constexpr double LEAD = 8.0;
			const double end = notes.back().time + 0.1;
			const size_t samples = static_cast<size_t>((end + LEAD) / DT) + 2;
			std::vector<std::vector<double>> clock(layerSpeeds.size());
			std::vector<bool> used(layerSpeeds.size(), false);
			for (const Read& n : notes)
				used[n.layer] = true;
			for (size_t layer = 0; layer < layerSpeeds.size(); ++layer)
			{
				if (!used[layer])
					continue;
				clock[layer].resize(samples);
				double startSpeed = 1.0;
				for (const auto& change : layerSpeeds[layer])
					if (change.tick <= 0)
						startSpeed = change.speed;
				for (size_t k = 0; k < samples; ++k)
				{
					const double t = k * DT - LEAD;
					if (t < 0)
					{
						clock[layer][k] = t * startSpeed;
						continue;
					}
					const int tick = accumulateTicks(static_cast<float>(t), TICKS_PER_BEAT, score.tempoChanges);
					// between ticks the clock moves linearly; ticks are fine enough at 240 Hz
					clock[layer][k] = accumulateScaledDuration(tick, TICKS_PER_BEAT, score.tempoChanges, layerSpeeds[layer]);
				}
			}
			auto position = [&](const Read& n, size_t k) { return (n.scaled - clock[n.layer][k]) / n.span; };

			// 1. flash notes: on screen for far less time than normal
			{
				std::vector<int> ids;
				int firstTick = -1;
				double shortest = 1e9, lastTime = -1e9;
				bool longExtreme = false;
				// Official charts pop notes in on purpose all the time (a burst of 1000x for a beat).
				// What looks like a mistake is an extreme speed left running for a measure or more.
				auto insideLongExtreme = [&](int tick, int layer) {
					const auto& changes = layerSpeeds[layer];
					for (size_t k = 0; k < changes.size(); ++k)
					{
						const int from = changes[k].tick;
						const int to = k + 1 < changes.size() ? changes[k + 1].tick : std::numeric_limits<int>::max();
						if (tick >= from && tick < to)
							return std::abs(changes[k].speed) >= 10.0f && static_cast<long long>(to) - from >= 1920;
					}
					return false;
				};
				auto flush = [&]() {
					if (ids.empty())
						return;
					c.add(longExtreme ? Severity::Warning : Severity::Info, Category::HiSpeed, "hispeed-flash",
						IO::formatString("%d note%s on screen for only %.2f s (normally %.2f s at note speed 10) because of the hi-speed here. %s",
							static_cast<int>(ids.size()), ids.size() == 1 ? " is" : "s are", shortest, window,
							longExtreme ? " The speed stays extreme for a measure or more here, which looks like a typo (e.g. 100 instead of 1.0)." : " Fine if it is a deliberate pop-in; check that it reads in the preview."),
						firstTick, ids);
					ids.clear();
					shortest = 1e9;
				};
				for (const Read& n : notes)
				{
					const size_t hit = std::min(samples - 1, static_cast<size_t>((n.time + LEAD) / DT));
					const size_t earliest = hit > static_cast<size_t>(30.0 / DT) ? hit - static_cast<size_t>(30.0 / DT) : 0;
					double visible = 0.0;
					for (size_t k = hit; k-- > earliest;)
					{
						const double p = position(n, k);
						if (p > 0.0 && p <= 1.0)
							visible += DT;
					}
					const double normal = n.span;	// at 1x the scaled clock runs in seconds
					if (visible < std::min(0.25, normal * 0.8) || visible < normal * 0.35)
					{
						if (n.time - lastTime > 2.0)
							flush();
						if (ids.empty())
						{
							firstTick = n.tick;
							longExtreme = false;
						}
						longExtreme |= insideLongExtreme(n.tick, n.layer);
						ids.push_back(n.id);
						shortest = std::min(shortest, visible);
						lastTime = n.time;
					}
				}
				flush();
			}

			// 2. stacked by hi-speed: hit at different times but drawn on the same spot (a stop, or a
			//    crawl, squeezes them together) - the player sees one note
			{
				std::set<std::pair<int, int>> reported;
				int count = 0, firstTick = -1;
				std::vector<int> firstIds;
				for (size_t i = 0; i < notes.size(); ++i)
					for (size_t j = i + 1; j < notes.size() && notes[j].time - notes[i].time < 4.0; ++j)
					{
						const Read& a = notes[i];
						const Read& b = notes[j];
						if (a.layer != b.layer || b.time - a.time < 0.03 || a.tick == b.tick)
							continue;
						if (!(a.lane < b.lane + b.width && b.lane < a.lane + a.width))
							continue;
						// closer than a note's thickness on screen (about 2% of the lane height)
						if (std::abs(b.scaled - a.scaled) < 0.02 * std::min(a.span, b.span))
						{
							if (count++ == 0)
							{
								firstTick = a.tick;
								firstIds = { a.id, b.id };
							}
						}
					}
				if (count > 0)
					c.add(Severity::Warning, Category::HiSpeed, "hispeed-stacked",
						IO::formatString("%d pair%s of notes are hit at different times but drawn on top of each other because the scroll stops or crawls between them; the player sees one note where there are two. The first is shown.",
							count, count == 1 ? "" : "s"), firstTick, firstIds);
			}

			// 3. overtaking: a later note drawn below an earlier one (negative speeds, or notes on
			//    different layers moving at different speeds)
			bool reverses = false;
			for (const auto& change : score.hiSpeedChanges)
				reverses |= change.speed < 0;
			int layersUsed = 0;
			for (bool u : used)
				layersUsed += u;
			if (reverses || layersUsed > 1)
			{
				int count = 0, firstTick = -1;
				std::vector<int> firstIds;
				double lastReported = -1e9;
				for (size_t k = 0; k < samples; k += 4)	// 60 Hz
				{
					const double now = k * DT - LEAD;
					std::vector<std::pair<double, const Read*>> onScreen;
					for (const Read& n : notes)
					{
						if (n.time < now)
							continue;
						if (n.time > now + 30.0)
							break;
						const double p = position(n, k);
						if (p > 0.0 && p <= 1.0)
							onScreen.push_back({ p, &n });
					}
					for (size_t x = 0; x < onScreen.size(); ++x)
						for (size_t y = 0; y < onScreen.size(); ++y)
						{
							const Read& a = *onScreen[x].second;
							const Read& b = *onScreen[y].second;
							// a is hit clearly before b, yet drawn clearly further from the line
							if (b.time - a.time > 0.05 && onScreen[x].first > onScreen[y].first + 0.05 && now - lastReported > 1.0)
							{
								if (count++ == 0)
								{
									firstTick = a.tick;
									firstIds = { a.id, b.id };
								}
								lastReported = now;
							}
						}
				}
				if (count > 0)
					c.add(Severity::Warning, Category::HiSpeed, "hispeed-overtake",
						IO::formatString("On %d occasion%s a note is drawn further up than a note that comes after it, so the order on screen is not the order to hit them (negative speeds or layers scrolling at different speeds). The first is shown.",
							count, count == 1 ? "" : "s"), firstTick, firstIds);
			}
		}

		void checkSkillFever(const Score& score, Collector& c)
		{
			const SkillFever::Markers markers = SkillFever::readMarkers(score);
			int firstTick = 0, lastTick = 0;
			const bool hasNotes = playableTickRange(score, firstTick, lastTick);

			if (markers.skills.empty())
			{
				c.add(Severity::Warning, Category::SkillFever, "skill-missing",
					"The chart has no skill triggers, so it plays with no skills. Use Chart > Auto-place Skills & FEVER to add the six official-style ones.");
			}
			else
			{
				if (static_cast<int>(markers.skills.size()) != SkillFever::OFFICIAL_SKILL_COUNT)
					c.add(Severity::Warning, Category::SkillFever, "skill-count",
						IO::formatString("The chart has %d skill trigger(s); official charts always have exactly 6.", static_cast<int>(markers.skills.size())), markers.skills.front());

				for (size_t i = 0; i < markers.skills.size(); ++i)
				{
					const int tick = markers.skills[i];
					if (i > 0 && tick == markers.skills[i - 1])
						c.add(Severity::Error, Category::SkillFever, "skill-duplicate", "Two skill triggers are on the same tick.", tick);
					else if (i > 0 && timeAt(score, tick) - timeAt(score, markers.skills[i - 1]) < 5.0)
						c.add(Severity::Warning, Category::SkillFever, "skill-too-close",
							IO::formatString("Skill triggers %d and %d are %.1f s apart; a skill lasts about 5 s, so they overlap.",
								static_cast<int>(i), static_cast<int>(i + 1), timeAt(score, tick) - timeAt(score, markers.skills[i - 1])), tick);

					if (hasNotes && (tick < firstTick || tick > lastTick))
						c.add(Severity::Warning, Category::SkillFever, "skill-outside-notes", "A skill trigger lies outside the part of the chart that has notes.", tick);
				}
			}

			if (markers.feverChance < 0)
				c.add(Severity::Warning, Category::SkillFever, "fever-chance-missing", "There is no FEVER chance marker (#mmm1f:11); multi-live FEVER cannot start.");
			if (markers.feverStart < 0)
				c.add(Severity::Warning, Category::SkillFever, "fever-start-missing", "There is no FEVER start marker (#mmm1f:21).");

			if (markers.feverChance >= 0 && markers.feverStart >= 0)
			{
				if (markers.feverStart <= markers.feverChance)
					c.add(Severity::Error, Category::SkillFever, "fever-order", "FEVER start is not after FEVER chance.", markers.feverStart);
				else
				{
					for (int tick : markers.skills)
						if (tick > markers.feverChance && tick < markers.feverStart)
							c.add(Severity::Info, Category::SkillFever, "skill-in-fever-window", "A skill trigger sits between FEVER chance and FEVER start; official charts never place one there.", tick);
				}
			}

			if (hasNotes)
			{
				for (int tick : { markers.feverChance, markers.feverStart })
					if (tick >= 0 && (tick < firstTick || tick > lastTick))
						c.add(Severity::Warning, Category::SkillFever, "fever-outside-notes", "A FEVER marker lies outside the part of the chart that has notes.", tick);
			}
		}
	}

	namespace
	{
		// Every hold has its start, end and steps. The point and overlap passes index straight into
		// the note map, so a broken hold (reported by checkStructure) keeps them out.
		bool holdsAreIntact(const Score& score)
		{
			for (const auto& [id, hold] : score.holdNotes)
			{
				if (!score.notes.count(hold.start.ID) || !score.notes.count(hold.end))
					return false;
				for (const HoldStep& step : hold.steps)
					if (!score.notes.count(step.ID))
						return false;
			}
			return true;
		}
	}

	std::vector<Issue> scanChart(const Score& score, const ScanOptions& options)
	{
		std::vector<Issue> issues;
		Collector c{ issues };

		checkStructure(score, c);
		if (holdsAreIntact(score))
		{
			checkPoints(score, c, options);
			checkHoldOverlaps(score, c);
		}
		checkTiming(score, c, options);
		checkHiSpeed(score, c);
		if (options.checkSkillFever && !score.notes.empty())
			checkSkillFever(score, c);

		std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b)
		{
			if (a.severity != b.severity)
				return a.severity < b.severity;
			return a.tick < b.tick;
		});
		return issues;
	}

	std::vector<Issue> scanSong(const std::vector<DifficultyChart>& charts, const ScanOptions& options)
	{
		std::vector<Issue> issues;
		for (const auto& chart : charts)
		{
			if (!chart.score || options.songChecksOnly)
				continue;
			for (Issue issue : scanChart(*chart.score, options))
			{
				issue.difficulty = chart.name;
				issues.push_back(std::move(issue));
			}
		}

		Collector c{ issues };
		static const std::vector<std::string> standard{ "easy", "normal", "hard", "expert", "master" };

		std::map<std::string, int> counts;
		for (const auto& chart : charts)
			++counts[chart.name];

		for (const auto& name : standard)
			if (!counts.count(name))
				c.add(Severity::Warning, Category::Difficulties, "difficulty-missing", IO::formatString("The song has no %s chart.", name.c_str()));

		for (const auto& [name, count] : counts)
		{
			if (count > 1)
				c.add(Severity::Error, Category::Difficulties, "difficulty-duplicate", IO::formatString("There are %d charts named \"%s\".", count, name.c_str()));
			if (std::find(standard.begin(), standard.end(), name) == standard.end() && name != "append")
				c.add(Severity::Info, Category::Difficulties, "difficulty-unknown", IO::formatString("\"%s\" is not a Project SEKAI difficulty name (easy, normal, hard, expert, master, append).", name.c_str()));
		}

		// FEVER is shared between players on different difficulties: markers must match
		const DifficultyChart* donor = nullptr;
		for (const auto& name : SkillFever::hardestFirst())
		{
			auto it = std::find_if(charts.begin(), charts.end(), [&](const DifficultyChart& d) { return d.name == name && d.score && SkillFever::hasMarkers(*d.score); });
			if (it != charts.end())
			{
				donor = &*it;
				break;
			}
		}

		if (donor)
		{
			const SkillFever::Markers reference = SkillFever::readMarkers(*donor->score);
			for (const auto& chart : charts)
			{
				if (!chart.score || &chart == donor)
					continue;
				const SkillFever::Markers markers = SkillFever::readMarkers(*chart.score);
				if (!markers.empty() && !(markers == reference))
				{
					Issue issue;
					issue.severity = Severity::Warning;
					issue.category = Category::SkillFever;
					issue.code = "skill-fever-mismatch";
					issue.message = IO::formatString("Skill/FEVER markers differ from the %s chart. FEVER is shared in multi-live, so official songs use the same positions on every difficulty (Song > Copy Skills & FEVER to all difficulties).", donor->name.c_str());
					issue.difficulty = chart.name;
					issue.tick = markers.skills.empty() ? markers.feverChance : markers.skills.front();
					issues.push_back(issue);
				}
			}
		}

		// timing: same song, so the same BPM map and offset everywhere
		const DifficultyChart* timingRef = nullptr;
		for (const auto& chart : charts)
			if (chart.score && (!timingRef || chart.score->notes.size() > timingRef->score->notes.size()))
				timingRef = &chart;

		if (timingRef)
		{
			for (const auto& chart : charts)
			{
				if (!chart.score || &chart == timingRef)
					continue;

				const auto& a = chart.score->tempoChanges;
				const auto& b = timingRef->score->tempoChanges;
				bool same = a.size() == b.size();
				for (size_t i = 0; same && i < a.size(); ++i)
					same = a[i].tick == b[i].tick && std::abs(a[i].bpm - b[i].bpm) < 0.001f;
				if (!same)
				{
					Issue issue;
					issue.severity = Severity::Warning;
					issue.category = Category::Difficulties;
					issue.code = "timing-mismatch";
					issue.message = IO::formatString("BPM changes differ from the %s chart; charts of one song normally share their timing.", timingRef->name.c_str());
					issue.difficulty = chart.name;
					issues.push_back(issue);
				}

				if (std::abs(chart.score->metadata.musicOffset - timingRef->score->metadata.musicOffset) > 0.5f)
				{
					Issue issue;
					issue.severity = Severity::Warning;
					issue.category = Category::Difficulties;
					issue.code = "offset-mismatch";
					issue.message = IO::formatString("Music offset (%.1f ms) differs from the %s chart (%.1f ms).",
						chart.score->metadata.musicOffset, timingRef->name.c_str(), timingRef->score->metadata.musicOffset);
					issue.difficulty = chart.name;
					issues.push_back(issue);
				}
			}
		}

		// easier difficulties should not have more notes than harder ones
		std::vector<std::pair<std::string, int>> combos;
		for (const auto& name : standard)
		{
			auto it = std::find_if(charts.begin(), charts.end(), [&](const DifficultyChart& d) { return d.name == name && d.score; });
			if (it != charts.end())
				combos.push_back({ name, static_cast<int>(calculateComboEvents(*it->score).size()) });
		}
		for (size_t i = 1; i < combos.size(); ++i)
		{
			if (combos[i].second < combos[i - 1].second)
			{
				Issue issue;
				issue.severity = Severity::Info;
				issue.category = Category::Difficulties;
				issue.code = "combo-order";
				issue.message = IO::formatString("%s has fewer combo (%d) than %s (%d).", combos[i].first.c_str(), combos[i].second, combos[i - 1].first.c_str(), combos[i - 1].second);
				issue.difficulty = combos[i].first;
				issues.push_back(issue);
			}
		}

		for (Issue& issue : issues)
			if (issue.title.empty())
				issue.title = issueTitle(issue.code);
		std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) { return a.severity < b.severity; });
		return issues;
	}

	const char* severityName(Severity severity)
	{
		switch (severity)
		{
		case Severity::Error: return "error";
		case Severity::Warning: return "warning";
		default: return "info";
		}
	}

	const char* categoryName(Category category)
	{
		switch (category)
		{
		case Category::Overlap: return "Stacked Notes";
		case Category::HoldNodeCollision: return "Hold Node Collision";
		case Category::VisibleOverlap: return "Visible Overlap";
		case Category::Density: return "Density";
		case Category::Holds: return "Holds";
		case Category::Lanes: return "Lanes";
		case Category::HiddenSteps: return "Hidden Steps";
		case Category::Timing: return "BPM & Timing";
		case Category::HiSpeed: return "Hi-Speed";
		case Category::SkillFever: return "Skill & FEVER";
		case Category::Difficulties: return "Difficulties";
		case Category::Structure: return "Structure";
		default: return "Other";
		}
	}

	const char* issueTitle(const std::string& code)
	{
		static const std::map<std::string, const char*> titles{
			{ "empty-chart", "Chart has no notes" },
			{ "negative-tick", "Note before the start" },
			{ "bad-width", "Invalid note width" },
			{ "lane-out-of-range", "Note outside the lanes" },
			{ "bad-speed-ratio", "Invalid note speed" },
			{ "extreme-speed-ratio", "Extreme note speed" },
			{ "orphan-hold-point", "Hold point without a hold" },
			{ "bad-layer", "Missing hi-speed layer" },
			{ "broken-hold", "Broken hold" },
			{ "hold-zero-length", "Hold has no length" },
			{ "step-outside-hold", "Step outside its hold" },
			{ "step-on-endpoint", "Step on the hold's start or end" },
			{ "critical-end-only", "Only the hold end is critical" },
			{ "guide-visible-step", "Visible step on a guide" },
			{ "hold-points-same-tick", "Hold points on the same tick" },
			{ "guide-shares-key", "Guide colour changes on export" },
			{ "hidden-step-visible", "Hidden step turns visible" },
			{ "hold-node-collision", "Exact overlap between hold nodes" },
			{ "duplicate-note", "Duplicate notes on one spot" },
			{ "notes-same-key", "Notes share a position" },
			{ "visible-overlap", "Visible overlap hides a note" },
			{ "many-simultaneous", "More than two notes at once" },
			{ "holds-overlap", "Holds on top of each other" },
			{ "holds-cross", "Holds cross" },
			{ "no-bpm", "No BPM" },
			{ "bad-bpm", "Invalid BPM" },
			{ "extreme-bpm", "Extreme BPM" },
			{ "bpm-duplicate", "Duplicate BPM change" },
			{ "no-bpm-at-start", "No BPM at the start" },
			{ "bad-time-signature", "Invalid time signature" },
			{ "odd-time-signature", "Time signature .sus cannot store" },
			{ "notes-after-music", "Notes after the music ends" },
			{ "first-note-early", "First note comes very early" },
			{ "hispeed-negative-tick", "Hi-speed before the start" },
			{ "hispeed-not-finite", "Hi-speed is not a number" },
			{ "hispeed-negative", "Negative hi-speed" },
			{ "hispeed-zero", "Hi-speed of zero" },
			{ "hispeed-extreme", "Extreme hi-speed" },
			{ "hispeed-duplicate", "Duplicate hi-speed change" },
			{ "hispeed-flash", "Notes flash past" },
			{ "hispeed-stacked", "Hi-speed stacks notes" },
			{ "hispeed-overtake", "Notes overtake each other" },
			{ "layers-not-in-game", "Layers the game ignores" },
			{ "hidden-layer-has-notes", "Notes on a hidden layer" },
			{ "skill-missing", "No skills" },
			{ "skill-count", "Unusual number of skills" },
			{ "skill-duplicate", "Duplicate skill" },
			{ "skill-too-close", "Skills too close together" },
			{ "skill-outside-notes", "Skill outside the notes" },
			{ "fever-chance-missing", "No FEVER chance" },
			{ "fever-start-missing", "No FEVER start" },
			{ "fever-order", "FEVER start before FEVER chance" },
			{ "skill-in-fever-window", "Skill inside the FEVER window" },
			{ "fever-outside-notes", "FEVER outside the notes" },
			{ "difficulty-missing", "Missing difficulty" },
			{ "difficulty-duplicate", "Duplicate difficulty" },
			{ "difficulty-unknown", "Unknown difficulty name" },
			{ "skill-fever-mismatch", "Skill/FEVER differ between difficulties" },
			{ "timing-mismatch", "BPM differs between difficulties" },
			{ "offset-mismatch", "Offset differs between difficulties" },
			{ "combo-order", "Easier difficulty has more notes" },
		};
		auto it = titles.find(code);
		return it == titles.end() ? code.c_str() : it->second;
	}

	int measureOf(const Score& score, int tick)
	{
		return accumulateMeasures(std::max(0, tick), TICKS_PER_BEAT, score.timeSignatures);
	}

	std::string measureFraction(const Score& score, int tick)
	{
		if (tick < 0)
			return "";

		const int measure = measureOf(score, tick);
		const int measureTick = measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures);
		const int nextMeasureTick = measureToTicks(measure + 1, TICKS_PER_BEAT, score.timeSignatures);
		int numerator = tick - measureTick;
		int denominator = std::max(1, nextMeasureTick - measureTick);
		if (numerator <= 0)
			return IO::formatString("M%03d", measure);

		int a = numerator, b = denominator;
		while (b != 0)
		{
			const int t = a % b;
			a = b;
			b = t;
		}
		numerator /= a;
		denominator /= a;
		return IO::formatString("M%03d %d/%d", measure, numerator, denominator);
	}

	std::string trackName(const Issue& issue)
	{
		if (issue.lane < 0 || issue.width <= 0)
			return "";
		return issue.width == 1 ? IO::formatString("Lane %d", issue.lane + 1) : IO::formatString("Lane %d-%d", issue.lane + 1, issue.lane + issue.width);
	}

	std::string describePosition(const Score& score, int tick)
	{
		if (tick < 0)
			return "";

		const int measure = accumulateMeasures(tick, TICKS_PER_BEAT, score.timeSignatures);
		const int measureTick = measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures);
		const int tsMeasure = findTimeSignature(measure, score.timeSignatures);
		const auto ts = score.timeSignatures.find(tsMeasure);
		const int beatTicks = ts == score.timeSignatures.end() ? TICKS_PER_BEAT : std::max(1, TICKS_PER_BEAT * 4 / std::max(1, ts->second.denominator));
		const double beat = 1.0 + static_cast<double>(tick - measureTick) / beatTicks;
		return IO::formatString("measure %d, beat %s", measure, IO::formatFixedFloatTrimmed(static_cast<float>(beat), 3).c_str());
	}

	Summary summarize(const std::vector<Issue>& issues)
	{
		Summary summary;
		for (const auto& issue : issues)
		{
			switch (issue.severity)
			{
			case Severity::Error: ++summary.errors; break;
			case Severity::Warning: ++summary.warnings; break;
			default: ++summary.infos; break;
			}
		}
		return summary;
	}
}
