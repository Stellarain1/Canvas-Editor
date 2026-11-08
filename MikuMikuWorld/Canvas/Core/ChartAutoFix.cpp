#include "ChartAutoFix.h"
#include "ChartFixes.h"
#include "ChartIssues.h"
#include "SkillFever.h"
#include "../../Constants.h"
#include "../../IO.h"
#include "../../MathUtils.h"
#include "../../Tempo.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>

using namespace MikuMikuWorld;

namespace Canvas::Fixes
{
	namespace
	{
		constexpr int STEP_FALLBACK_TICKS = 120;	// a hold with no length gets a 16th

		std::string plural(int n, const char* one, const char* many)
		{
			return std::to_string(n) + " " + (n == 1 ? one : many);
		}

		// ---- timing ---------------------------------------------------------------------------------

		int fixTiming(Score& score, AutoFixReport& report)
		{
			int changes = 0;
			auto& tempos = score.tempoChanges;

			const size_t before = tempos.size();
			tempos.erase(std::remove_if(tempos.begin(), tempos.end(), [](const Tempo& t) { return !std::isfinite(t.bpm) || t.bpm <= 0.0f; }), tempos.end());
			if (before != tempos.size())
			{
				report.log.push_back("removed " + plural(static_cast<int>(before - tempos.size()), "invalid BPM change", "invalid BPM changes"));
				changes += static_cast<int>(before - tempos.size());
			}
			if (tempos.empty())
			{
				tempos.push_back(Tempo(0, 120.0f));
				report.log.push_back("the chart had no BPM: added 120 BPM at the start");
				++changes;
			}

			for (Tempo& t : tempos)
				if (t.tick < 0)
				{
					t.tick = 0;
					++changes;
				}
			std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });

			// two changes on one tick: the last one is the one that was meant
			std::vector<Tempo> unique;
			for (const Tempo& t : tempos)
			{
				if (!unique.empty() && unique.back().tick == t.tick)
				{
					unique.back() = t;
					++changes;
					report.log.push_back("two BPM changes shared a tick: kept the later one at tick " + std::to_string(t.tick));
				}
				else
					unique.push_back(t);
			}
			tempos = unique;

			// the first BPM already rules everything before it, so moving it to tick 0 changes no timing
			if (tempos.front().tick > 0)
			{
				report.log.push_back("moved the first BPM (" + IO::formatFixedFloatTrimmed(tempos.front().bpm) + ") from tick " + std::to_string(tempos.front().tick) + " to the start");
				tempos.front().tick = 0;
				++changes;
			}

			for (auto it = score.timeSignatures.begin(); it != score.timeSignatures.end();)
			{
				if (it->second.numerator < MIN_TIME_SIGNATURE || it->second.denominator < MIN_TIME_SIGNATURE)
				{
					report.log.push_back("removed the invalid time signature " + std::to_string(it->second.numerator) + "/" + std::to_string(it->second.denominator) + " at measure " + std::to_string(it->first));
					it = score.timeSignatures.erase(it);
					++changes;
				}
				else
					++it;
			}
			if (score.timeSignatures.empty() || !score.timeSignatures.count(0))
				score.timeSignatures[0] = TimeSignature{ 0, 4, 4 };
			return changes;
		}

		// ---- holds ----------------------------------------------------------------------------------

		void eraseHoldNotes(Score& score, const HoldNote& hold)
		{
			score.notes.erase(hold.start.ID);
			score.notes.erase(hold.end);
			for (const HoldStep& step : hold.steps)
				score.notes.erase(step.ID);
		}

		int fixHolds(Score& score, AutoFixReport& report)
		{
			int changes = 0;

			// notes pulled back into the chart and the 12 lanes
			int pulled = 0;
			for (auto& [id, note] : score.notes)
			{
				bool moved = false;
				if (note.tick < 0)
				{
					note.tick = 0;
					moved = true;
				}
				const int width = std::clamp(note.width, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
				const int lane = std::clamp(note.lane, MIN_LANE, MAX_LANE - width + 1);
				if (width != note.width || lane != note.lane)
				{
					note.width = width;
					note.lane = lane;
					moved = true;
				}
				pulled += moved;
			}
			if (pulled)
			{
				report.log.push_back("pulled " + plural(pulled, "note", "notes") + " back into the chart and the 12 lanes");
				changes += pulled;
			}

			// hold steps and ends whose hold does not exist
			std::vector<int> orphans;
			for (const auto& [id, note] : score.notes)
				if ((note.getType() == NoteType::HoldMid || note.getType() == NoteType::HoldEnd) && !score.holdNotes.count(note.parentID))
					orphans.push_back(id);
			for (int id : orphans)
				score.notes.erase(id);
			if (!orphans.empty())
			{
				report.log.push_back("removed " + plural(static_cast<int>(orphans.size()), "hold point", "hold points") + " that belonged to no hold");
				changes += static_cast<int>(orphans.size());
			}

			for (auto it = score.holdNotes.begin(); it != score.holdNotes.end();)
			{
				HoldNote& hold = it->second;
				auto startIt = score.notes.find(hold.start.ID);
				auto endIt = score.notes.find(hold.end);
				if (startIt == score.notes.end() || endIt == score.notes.end())
				{
					report.log.push_back("removed a hold that had lost its " + std::string(startIt == score.notes.end() ? "start" : "end"));
					eraseHoldNotes(score, hold);
					it = score.holdNotes.erase(it);
					++changes;
					continue;
				}

				Note& start = startIt->second;
				Note& end = endIt->second;

				// steps that point at nothing
				const size_t stepCount = hold.steps.size();
				hold.steps.erase(std::remove_if(hold.steps.begin(), hold.steps.end(), [&](const HoldStep& s) { return !score.notes.count(s.ID); }), hold.steps.end());
				if (hold.steps.size() != stepCount)
				{
					report.log.push_back("dropped " + plural(static_cast<int>(stepCount - hold.steps.size()), "step", "steps") + " of the hold at tick " + std::to_string(start.tick) + " that no longer existed");
					changes += static_cast<int>(stepCount - hold.steps.size());
				}

				if (end.tick < start.tick)
				{
					std::swap(start.tick, end.tick);
					std::swap(start.lane, end.lane);
					std::swap(start.width, end.width);
					report.log.push_back("a hold ended before it started: flipped it at tick " + std::to_string(start.tick));
					++changes;
				}
				else if (end.tick == start.tick)
				{
					end.tick = start.tick + STEP_FALLBACK_TICKS;
					report.log.push_back("a hold had no length: gave it a 16th at tick " + std::to_string(start.tick));
					++changes;
				}

				// steps outside the hold go; on a normal hold so do steps that sit on its ends or on another
				// step. Guides keep theirs: official APPEND guides put steps on their ends on purpose.
				const bool guide = hold.isGuide();
				std::vector<HoldStep> kept;
				std::set<int> usedTicks{ start.tick, end.tick };
				std::vector<HoldStep> ordered = hold.steps;
				std::stable_sort(ordered.begin(), ordered.end(), [&](const HoldStep& a, const HoldStep& b) {
					const Note& na = score.notes.at(a.ID);
					const Note& nb = score.notes.at(b.ID);
					return na.tick != nb.tick ? na.tick < nb.tick : a.ID < b.ID;
				});
				for (const HoldStep& step : ordered)
				{
					const int tick = score.notes.at(step.ID).tick;
					const bool outside = tick < start.tick || tick > end.tick;
					const bool crowded = !guide && !outside && !usedTicks.insert(tick).second;
					if (outside || crowded)
					{
						score.notes.erase(step.ID);
						++changes;
						report.log.push_back("removed a hold step at tick " + std::to_string(tick) + (outside ? " (outside its hold)" : " (on the same tick as another point of the hold)"));
					}
					else
						kept.push_back(step);
				}
				hold.steps = kept;
				++it;
			}
			return changes;
		}

		// ---- hi-speed ---------------------------------------------------------------------------------

		float speedAt(const std::vector<HiSpeedChange>& sorted, int tick)
		{
			float speed = 1.0f;
			for (const HiSpeedChange& change : sorted)
			{
				if (change.tick > tick)
					break;
				speed = change.speed;
			}
			return speed;
		}

		void flatten(Score& score, const std::vector<std::vector<HiSpeedChange>>& layers)
		{
			score.hiSpeedChanges.clear();
			for (const auto& layer : layers)
				score.hiSpeedChanges.insert(score.hiSpeedChanges.end(), layer.begin(), layer.end());
		}

		// Speeds in [a, b) never fall below `minSpeed`; what comes before a and from b on is untouched.
		void raiseBetween(std::vector<HiSpeedChange>& changes, int layer, int a, int b, float minSpeed)
		{
			const float atA = speedAt(changes, a);
			const float atB = speedAt(changes, b);
			bool hasA = false, hasB = false;
			for (HiSpeedChange& change : changes)
			{
				if (change.tick == a)
				{
					change.speed = std::max(change.speed, minSpeed);
					hasA = true;
				}
				else if (change.tick > a && change.tick < b)
					change.speed = std::max(change.speed, minSpeed);
				hasB |= change.tick == b;
			}
			if (!hasA)
				changes.push_back({ a, std::max(atA, minSpeed), layer });
			if (!hasB)
				changes.push_back({ b, atB, layer });
			std::stable_sort(changes.begin(), changes.end(), [](const HiSpeedChange& x, const HiSpeedChange& y) { return x.tick < y.tick; });
		}

		// Pairs of notes hit at different times but drawn on the same spot (the scroll stops or crawls
		// between them): speeds between them are raised until the two read as two notes.
		int fixStackedNotes(Score& score, AutoFixReport& report)
		{
			if (score.hiSpeedChanges.empty() || score.tempoChanges.empty())
				return 0;

			constexpr double NOTE_SPEED = 10.0;
			const double window = lerpD(0.35, 4.0, std::pow(unlerpD(12, 1, NOTE_SPEED), 1.31));

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
				int tick, lane, width, layer;
				double time, span;
			};
			std::vector<Read> notes;
			for (const auto& [id, note] : score.notes)
			{
				const NoteType t = note.getType();
				if ((t != NoteType::Tap && t != NoteType::Hold && t != NoteType::HoldEnd) || skip.count(id) || note.tick < 0)
					continue;
				const double ratio = std::max(0.01f, getEffectiveSpeedRatio(note, score));
				notes.push_back({ note.tick, note.lane, note.width, std::clamp(note.layer, 0, static_cast<int>(score.layers.size()) - 1),
					accumulateDuration(note.tick, TICKS_PER_BEAT, score.tempoChanges), window / ratio });
			}
			std::sort(notes.begin(), notes.end(), [](const Read& a, const Read& b) { return a.time < b.time; });

			int fixes = 0;
			for (int round = 0; round < 8; ++round)
			{
				auto layers = splitHiSpeedLayers(score);
				std::vector<double> scaled(notes.size());
				for (size_t i = 0; i < notes.size(); ++i)
					scaled[i] = accumulateScaledDuration(notes[i].tick, TICKS_PER_BEAT, score.tempoChanges, layers[notes[i].layer]);

				struct Raise { int layer, a, b; float minSpeed; };
				std::vector<Raise> raises;
				for (size_t i = 0; i < notes.size(); ++i)
					for (size_t j = i + 1; j < notes.size() && notes[j].time - notes[i].time < 4.0; ++j)
					{
						const Read& a = notes[i];
						const Read& b = notes[j];
						if (a.layer != b.layer || b.time - a.time < 0.03 || a.tick == b.tick)
							continue;
						if (!(a.lane < b.lane + b.width && b.lane < a.lane + a.width))
							continue;
						const double needed = 0.02 * std::min(a.span, b.span);
						if (std::abs(scaled[j] - scaled[i]) < needed)
							raises.push_back({ a.layer, a.tick, b.tick, static_cast<float>(needed * 1.2 / (b.time - a.time)) });
					}
				if (raises.empty())
					break;

				for (const Raise& r : raises)
				{
					raiseBetween(layers[r.layer], r.layer, r.a, r.b, r.minSpeed);
					++fixes;
				}
				flatten(score, layers);
			}
			if (fixes)
				report.log.push_back("sped up the scroll in " + plural(fixes, "spot", "spots") + " where two notes were drawn on top of each other");
			return fixes;
		}

		int fixHiSpeed(Score& score, AutoFixReport& report, bool aggressive)
		{
			int changes = 0;
			const int layerCount = static_cast<int>(score.layers.size());

			// notes: a speed ratio that can never reach the line, a layer that does not exist
			int ratios = 0, layerFixes = 0;
			for (auto& [id, note] : score.notes)
			{
				const bool speedNote = note.getType() == NoteType::Tap || note.getType() == NoteType::Hold;
				if (speedNote && (!std::isfinite(note.speedRatio) || note.speedRatio <= 0.0f))
				{
					note.speedRatio = 1.0f;
					++ratios;
				}
				else if (aggressive && speedNote && (note.speedRatio > 20.0f || note.speedRatio < 0.05f))
				{
					note.speedRatio = std::clamp(note.speedRatio, 0.05f, 20.0f);
					++ratios;
				}
				if (note.layer < 0 || note.layer >= layerCount)
				{
					note.layer = 0;
					++layerFixes;
				}
			}
			if (ratios)
				report.log.push_back("reset " + plural(ratios, "note speed", "note speeds") + " to something playable");
			if (layerFixes)
				report.log.push_back("moved " + plural(layerFixes, "note", "notes") + " from a missing hi-speed layer to the default one");
			changes += ratios + layerFixes;

			// changes that cannot work
			const size_t before = score.hiSpeedChanges.size();
			score.hiSpeedChanges.erase(std::remove_if(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(), [&](const HiSpeedChange& c) {
				return !std::isfinite(c.speed) || c.layer < 0 || c.layer >= layerCount;
			}), score.hiSpeedChanges.end());
			if (before != score.hiSpeedChanges.size())
			{
				report.log.push_back("removed " + plural(static_cast<int>(before - score.hiSpeedChanges.size()), "unusable hi-speed change", "unusable hi-speed changes") + " (not a number, or on a missing layer)");
				changes += static_cast<int>(before - score.hiSpeedChanges.size());
			}
			for (HiSpeedChange& c : score.hiSpeedChanges)
				if (c.tick < 0)
				{
					c.tick = 0;
					++changes;
				}

			// per layer, sorted; two on a tick: the later one wins
			auto layers = splitHiSpeedLayers(score);
			for (size_t l = 0; l < layers.size(); ++l)
			{
				std::vector<HiSpeedChange> unique;
				for (const HiSpeedChange& c : layers[l])
				{
					if (!unique.empty() && unique.back().tick == c.tick)
					{
						unique.back() = c;
						++changes;
						report.log.push_back("two hi-speed changes shared tick " + std::to_string(c.tick) + ": kept the later one");
					}
					else
						unique.push_back(c);
				}
				layers[l] = unique;
			}

			if (aggressive)
			{
				for (auto& layer : layers)
				{
					for (HiSpeedChange& c : layer)
						if (c.speed < 0.0f)
						{
							c.speed = -c.speed;
							++changes;
						}
					for (size_t k = 0; k < layer.size(); ++k)
					{
						const long long to = k + 1 < layer.size() ? layer[k + 1].tick : static_cast<long long>(layer[k].tick) + 1920;
						// an extreme speed that runs a measure or more is a typo (100 for 1.0); a short burst is a pop-in on purpose
						if (std::abs(layer[k].speed) >= 10.0f && to - layer[k].tick >= 1920)
						{
							const float old = layer[k].speed;
							float fixed = old;
							while (fixed > 8.0f)
								fixed /= 10.0f;
							layer[k].speed = fixed;
							++changes;
							report.log.push_back("hi-speed " + IO::formatFixedFloatTrimmed(old) + "x at tick " + std::to_string(layer[k].tick) + " ran for a measure or more: now " + IO::formatFixedFloatTrimmed(fixed) + "x");
						}
					}
				}
			}
			flatten(score, layers);

			if (aggressive)
				changes += fixStackedNotes(score, report);
			return changes;
		}

		// ---- skills and FEVER -------------------------------------------------------------------------

		int fixSkillFever(Score& score, AutoFixReport& report)
		{
			if (score.notes.empty() || score.tempoChanges.empty())
				return 0;
			int firstTick = INT_MAX, lastTick = 0;
			for (const auto& [id, note] : score.notes)
			{
				firstTick = std::min(firstTick, note.tick);
				lastTick = std::max(lastTick, note.tick);
			}

			const SkillFever::Markers generated = SkillFever::generateOfficialStyle(score);
			SkillFever::Markers markers = SkillFever::readMarkers(score);
			const SkillFever::Markers original = markers;
			int changes = 0;

			auto timeAt = [&](int tick) { return static_cast<double>(accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges)); };
			auto nextMeasure = [&](int tick) {
				return measureToTicks(accumulateMeasures(std::max(0, tick), TICKS_PER_BEAT, score.timeSignatures) + 1, TICKS_PER_BEAT, score.timeSignatures);
			};

			if (markers.skills.empty() && !generated.skills.empty())
			{
				markers.skills = generated.skills;
				report.log.push_back("added the six skill triggers in the official positions");
				++changes;
			}
			else
			{
				std::vector<int> skills = markers.skills;
				for (int& tick : skills)
					tick = std::clamp(tick, firstTick, lastTick);
				std::sort(skills.begin(), skills.end());
				skills.erase(std::unique(skills.begin(), skills.end()), skills.end());
				// a skill lasts about 5 s: push the next one to the measure after that
				for (size_t i = 1; i < skills.size();)
				{
					int tick = skills[i];
					while (tick <= lastTick && timeAt(tick) - timeAt(skills[i - 1]) < 5.0)
						tick = nextMeasure(tick);
					if (tick > lastTick)
					{
						skills.erase(skills.begin() + i);
						continue;
					}
					skills[i] = tick;
					++i;
				}
				if (skills != markers.skills)
				{
					markers.skills = skills;
					report.log.push_back("moved skill triggers so they lie inside the chart, are not stacked and do not overlap");
					++changes;
				}
			}

			if (markers.feverChance < 0 && markers.feverStart < 0 && generated.feverChance >= 0)
			{
				markers.feverChance = generated.feverChance;
				markers.feverStart = generated.feverStart;
				report.log.push_back("placed FEVER chance and FEVER start in the official positions");
				++changes;
			}
			else
			{
				if (markers.feverChance < 0 && generated.feverChance >= 0)
				{
					markers.feverChance = generated.feverChance;
					report.log.push_back("placed the missing FEVER chance");
					++changes;
				}
				if (markers.feverStart < 0 && generated.feverStart >= 0)
				{
					markers.feverStart = std::max(generated.feverStart, nextMeasure(markers.feverChance));
					report.log.push_back("placed the missing FEVER start");
					++changes;
				}
				for (int* tick : { &markers.feverChance, &markers.feverStart })
					if (*tick >= 0 && (*tick < firstTick || *tick > lastTick))
					{
						*tick = std::clamp(*tick, firstTick, lastTick);
						++changes;
						report.log.push_back("moved a FEVER marker inside the chart");
					}
				if (markers.feverChance >= 0 && markers.feverStart >= 0 && markers.feverStart <= markers.feverChance)
				{
					markers.feverStart = nextMeasure(markers.feverChance);
					++changes;
					report.log.push_back("FEVER start was not after FEVER chance: moved it one measure past");
				}
			}

			if (!(markers == original))
				SkillFever::applyMarkers(score, markers);
			return changes;
		}

		struct Counter
		{
			int errors{}, warnings{};
		};

		Counter countProblems(const Score& score, const AutoFixOptions& options)
		{
			Issues::ScanOptions scan;
			scan.musicDurationSeconds = options.musicDurationSeconds;
			const auto summary = Issues::summarize(Issues::scanChart(score, scan));
			return { summary.errors, summary.warnings };
		}
	}

	int groupOfCode(const std::string& code)
	{
		static const std::map<std::string, int> groups{
			{ "duplicate-note", FixOverlaps }, { "notes-same-key", FixOverlaps }, { "visible-overlap", FixOverlaps },
			{ "hold-node-collision", FixOverlaps }, { "hidden-step-visible", FixOverlaps },

			{ "negative-tick", FixHolds }, { "bad-width", FixHolds }, { "lane-out-of-range", FixHolds }, { "orphan-hold-point", FixHolds },
			{ "broken-hold", FixHolds }, { "hold-zero-length", FixHolds }, { "step-outside-hold", FixHolds }, { "step-on-endpoint", FixHolds },
			{ "hold-points-same-tick", FixHolds },

			{ "hispeed-negative-tick", FixHiSpeed }, { "bad-layer", FixHiSpeed }, { "hispeed-not-finite", FixHiSpeed }, { "hispeed-duplicate", FixHiSpeed },
			{ "hispeed-negative", FixHiSpeed }, { "hispeed-flash", FixHiSpeed }, { "hispeed-stacked", FixHiSpeed }, { "hispeed-overtake", FixHiSpeed },
			{ "bad-speed-ratio", FixHiSpeed }, { "extreme-speed-ratio", FixHiSpeed },

			{ "skill-missing", FixSkillFever }, { "skill-duplicate", FixSkillFever }, { "skill-too-close", FixSkillFever }, { "skill-outside-notes", FixSkillFever },
			{ "fever-chance-missing", FixSkillFever }, { "fever-start-missing", FixSkillFever }, { "fever-order", FixSkillFever }, { "fever-outside-notes", FixSkillFever },

			{ "no-bpm", FixTiming }, { "bad-bpm", FixTiming }, { "bpm-duplicate", FixTiming }, { "no-bpm-at-start", FixTiming }, { "bad-time-signature", FixTiming },
		};
		const auto it = groups.find(code);
		return it == groups.end() ? 0 : it->second;
	}

	AutoFixPreview previewAutoFix(const Score& score, double musicDurationSeconds)
	{
		Issues::ScanOptions scan;
		scan.musicDurationSeconds = musicDurationSeconds;
		AutoFixPreview preview;
		for (const Issues::Issue& issue : Issues::scanChart(score, scan))
		{
			switch (issue.severity)
			{
			case Issues::Severity::Error: ++preview.errors; break;
			case Issues::Severity::Warning: ++preview.warnings; break;
			default: ++preview.infos; break;
			}
			const int group = groupOfCode(issue.code);
			for (int bit = 0; bit < 5; ++bit)
				if (group == (1 << bit))
					++preview.perGroup[bit];
		}
		return preview;
	}

	AutoFixReport autoFix(Score& score, const AutoFixOptions& options, const std::function<void(const std::string&)>& stage)
	{
		AutoFixReport report;
		auto announce = [&](const char* text) { if (stage) stage(text); };

		announce("Scanning");
		const Counter beforeCount = countProblems(score, options);
		report.before = beforeCount.errors + beforeCount.warnings;

		// timing first: everything else measures ticks against it
		if (options.groups & FixTiming)
		{
			announce("Timing");
			report.timing = fixTiming(score, report);
		}
		if (options.groups & FixHolds)
		{
			announce("Holds");
			report.holds = fixHolds(score, report);
		}
		if (options.groups & FixOverlaps)
		{
			announce("Overlaps");
			const Report overlaps = fixOverlaps(score);
			report.overlaps = overlaps.merged + overlaps.moved;
			report.log.insert(report.log.end(), overlaps.log.begin(), overlaps.log.end());
			if (overlaps.left)
				report.log.push_back(plural(overlaps.left, "overlap is", "overlaps are") + " left");
		}
		if (options.groups & FixHiSpeed)
		{
			announce("Hi-speed");
			report.hiSpeed = fixHiSpeed(score, report, options.aggressiveHiSpeed);
		}
		if (options.groups & FixSkillFever)
		{
			announce("Skills & FEVER");
			report.skillFever = fixSkillFever(score, report);
		}

		announce("Checking the result");
		const Counter after = countProblems(score, options);
		report.after = after.errors + after.warnings;
		return report;
	}
}
