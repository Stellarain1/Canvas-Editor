#include "SusSerializer.h"
#include "SusExporter.h"
#include "SusParser.h"
#include "SUS.h"
#include "Canvas/Core/ChartTransforms.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace MikuMikuWorld
{	
	void SusSerializer::serialize(const Score& score, std::string filename)
	{
		warnings.clear();
		Score exportScore = score;
		Canvas::normalizeEvents(exportScore);

		if (int n = Canvas::lowerExtendedEases(exportScore))
			warnings.push_back(IO::formatString("%d ease-in-out / ease-out-in segment(s) were split into ease-in + ease-out halves (.sus only has in and out).", n));

		if (mergeLayers && Canvas::usesLayers(exportScore))
		{
			const auto report = Canvas::mergeLayersToDefault(exportScore);
			warnings.push_back(IO::formatString("Hi-speed layers were merged into #TIL00 (the only one the game reads): %d note(s) moved to the default layer, %d hi-speed change(s) of other layers dropped.",
				report.notesMoved, report.hiSpeedsDropped));
		}

		int extendedGuides = 0;
		for (const auto& [id, hold] : exportScore.holdNotes)
			extendedGuides += hold.isGuide() && (isExtendedGuideColor(hold.guideColor) || hold.fadeType != FadeType::Out);
		if (extendedGuides)
			warnings.push_back(IO::formatString("%d guide(s) use a colour other than green/yellow or a fade other than fade-out; .sus stores them as green or yellow fading out.", extendedGuides));

		if (!includeNoteSpeed && Canvas::countNotesWithSpeedRatio(exportScore))
			warnings.push_back("Per-note speed ratios were left out (chosen export mode has no note speed).");

		SUS sus = scoreToSus(exportScore);
		SusExporter().dump(sus, filename, exportComment, includeNoteSpeed);
	}

	Score SusSerializer::deserialize(std::string filename)
	{
		warnings.clear();
		SUS sus = SusParser().parse(filename);
		return susToScore(sus);
	}

	std::pair<int, int> SusSerializer::barLengthToFraction(float length, float fractionDenom)
	{
		int factor = 1;
		for (int i = 2; i < 10; ++i)
		{
			if (fmodf(factor * length, 1) == 0)
				return std::make_pair<int, int>(factor * length, std::pow(2, i));

			factor *= 2;
		}

		return std::pair<int, int>(4, 4);
	}

	std::string SusSerializer::noteKey(const SUSNote& note)
	{
		return IO::formatString("%d-%d", note.tick, note.lane);
	}

	std::string SusSerializer::noteKey(const Note& note)
	{
		return IO::formatString("%d-%d", note.tick, note.lane);
	}

	Score SusSerializer::susToScore(const SUS& sus)
	{
		ScoreMetadata metadata
		{
			sus.metadata.data.at("title"),
			sus.metadata.data.at("artist"),
			sus.metadata.data.at("designer"),
			"",
			"",
			sus.metadata.waveOffset * 1000 // seconds -> milliseconds
		};

		std::unordered_map<std::string, FlickType> flicks;
		std::unordered_set<std::string> criticals;
		std::unordered_set<std::string> stepIgnore;
		std::unordered_set<std::string> easeIns;
		std::unordered_set<std::string> easeOuts;
		std::unordered_set<std::string> slideKeys;
		std::unordered_set<std::string> frictions;
		std::unordered_set<std::string> hiddenHolds;
		std::unordered_map<std::string, float> noteSpeedRatios;

		auto rememberNoteSpeed = [&](const SUSNote& note)
		{
			if (std::isfinite(note.speedRatio) && note.speedRatio > 0.0f && std::abs(note.speedRatio - 1.0f) > 0.0001f)
				noteSpeedRatios.insert_or_assign(noteKey(note), note.speedRatio);
		};

		for (const auto& tap : sus.taps)
			rememberNoteSpeed(tap);

		for (const auto& dir : sus.directionals)
			rememberNoteSpeed(dir);

		for (const auto& slide : sus.slides)
		{
			for (const auto& note : slide)
			{
				rememberNoteSpeed(note);
				switch (note.type)
				{
				case 1:
				case 2:
				case 3:
				case 5:
					slideKeys.insert(noteKey(note));
				}
			}
		}

		for (const auto& guide : sus.guides)
		{
			for (const auto& note : guide)
				rememberNoteSpeed(note);
		}

		auto getNoteSpeed = [&](const SUSNote& note)
		{
			const auto it = noteSpeedRatios.find(noteKey(note));
			return it == noteSpeedRatios.end() ? 1.0f : it->second;
		};

		for (const auto& dir : sus.directionals)
		{
			const std::string key = noteKey(dir);
			switch (dir.type)
			{
			case 1:
				flicks.insert_or_assign(key, FlickType::Default);
				break;
			case 3:
				flicks.insert_or_assign(key, FlickType::Left);
				break;
			case 4:
				flicks.insert_or_assign(key, FlickType::Right);
				break;
			case 2:
				easeIns.insert(key);
				break;
			case 5:
			case 6:
				easeOuts.insert(key);
				break;
			default:
				break;
			}
		}

		for (const auto& tap : sus.taps)
		{
			const std::string key = noteKey(tap);
			switch (tap.type)
			{
			case 2:
				criticals.insert(key);
				break;
			case 3:
				stepIgnore.insert(key);
				break;
			case 5:
				frictions.insert(key);
				break;
			case 6:
				criticals.insert(key);
				frictions.insert(key);
				break;
			case 7:
				hiddenHolds.insert(key);
				break;
			case 8:
				hiddenHolds.insert(key);
				criticals.insert(key);
				break;
			default:
				break;
			}
		}

		std::map<int, Note> notes;
		std::map<int, HoldNote> holds;
		std::vector<SkillTrigger> skills;
		Fever fever{ -1, -1 };

		int ignoredTypeFour = 0;
		int mergedDuplicates = 0;
		std::unordered_set<std::string> createdTapKeys;
		for (const auto& note : sus.taps)
		{
			// skill trigger: type 4 on lane 0 (#mmm10:41). Type 4 inside the playfield is a
			// Chunithm damage note, not a skill.
			if (note.type == 4)
			{
				if (note.lane < 2 || note.lane > 13)
					skills.push_back(SkillTrigger{ nextSkillID++, note.tick });
				else
					++ignoredTypeFour;
				continue;
			}

			if (note.lane == 15 && note.width == 1)
			{
				if (note.type == 1)
					fever.chanceTick = note.tick;
				else if (note.type == 2)
					fever.startTick = note.tick;
				continue;
			}

			// 3 = skip-step marker, 7/8 = invisible slide point marker: they only modify the
			// slide point at the same position and are never notes on their own
			if (note.type == 3 || note.type == 7 || note.type == 8)
				continue;

			if (note.lane - 2 < MIN_LANE || note.lane - 2 > MAX_LANE)
				continue;

			const std::string key = noteKey(note);

			// Conflict with skip slide steps and hidden holds
			if (slideKeys.find(key) != slideKeys.end())
				continue;

			// the game identifies notes by tick + lane: a second tap there is the same note
			if (!createdTapKeys.insert(key).second)
			{
				++mergedDuplicates;
				continue;
			}

			Note n(NoteType::Tap, note.tick, note.lane - 2, note.width);
			n.critical = criticals.find(key) != criticals.end();
			n.friction = frictions.find(key) != frictions.end();
			n.flick = flicks.find(key) != flicks.end() ? flicks[key] : FlickType::None;
			n.speedRatio = getNoteSpeed(note);
			n.layer = note.hiSpeedGroup;
			n.ID = nextID++;

			notes[n.ID] = n;
		}

		// Not the best way to handle this but it will do the job
		std::unordered_set<std::string> slideSignatures;
		auto slideFillFunc = [&](const std::vector<std::vector<SUSNote>>& slides, bool isGuide)
			{
				for (const auto& slide : slides)
				{
					const std::string key = noteKey(slide[0]);

					// identical slides written on two channels are one slide in-game
					std::string signature = isGuide ? "g" : "s";
					for (const auto& point : slide)
						signature += IO::formatString("|%d,%d,%d,%d", point.tick, point.lane, point.width, point.type);
					if (!slideSignatures.insert(signature).second)
					{
						++mergedDuplicates;
						continue;
					}

					auto start = std::find_if(slide.begin(), slide.end(),
						[](const SUSNote& a) { return a.type == 1 || a.type == 2; });

					if (start == slide.end() || slide.size() < 2)
						continue;

					bool critical = criticals.find(key) != criticals.end();

					HoldNote hold;
					int startID = nextID++;
					hold.steps.reserve(slide.size() - 2);

					for (const auto& note : slide)
					{
						const std::string key = noteKey(note);

						EaseType ease = EaseType::Linear;
						if (easeIns.find(key) != easeIns.end())
						{
							ease = EaseType::EaseIn;
						}
						else if (easeOuts.find(key) != easeOuts.end())
						{
							ease = EaseType::EaseOut;
						}

						switch (note.type)
						{
							// start
						case 1:
						{
							Note n(NoteType::Hold, note.tick, note.lane - 2, note.width);
							n.critical = critical;
							n.speedRatio = getNoteSpeed(note);
							n.layer = note.hiSpeedGroup;
							n.ID = startID;

							if (isGuide)
							{
								hold.startType = HoldNoteType::Guide;
							}
							else
							{
								n.friction = frictions.find(noteKey(note)) != frictions.end();
								hold.startType = hiddenHolds.find(noteKey(note)) != hiddenHolds.end() ? HoldNoteType::Hidden : HoldNoteType::Normal;
							}

							notes[n.ID] = n;
							hold.start = HoldStep{ n.ID, HoldStepType::Normal, ease };
						}
						break;
						// end
						case 2:
						{
							Note n(NoteType::HoldEnd, note.tick, note.lane - 2, note.width);
							n.critical = (critical ? true : (criticals.find(key) != criticals.end()));
							n.speedRatio = getNoteSpeed(note);
							n.layer = note.hiSpeedGroup;
							n.ID = nextID++;
							n.parentID = startID;

							if (isGuide)
							{
								hold.endType = HoldNoteType::Guide;
							}
							else
							{
								n.flick = flicks.find(key) != flicks.end() ? flicks[key] : FlickType::None;
								n.friction = frictions.find(noteKey(note)) != frictions.end();
								hold.endType = hiddenHolds.find(noteKey(note)) != hiddenHolds.end() ? HoldNoteType::Hidden : HoldNoteType::Normal;
							}

							notes[n.ID] = n;
							hold.end = n.ID;
						}
						break;
						// mid
						case 3:
						case 5:
						{
							Note n(NoteType::HoldMid, note.tick, note.lane - 2, note.width);
							n.critical = critical;
							n.friction = false;
							n.speedRatio = getNoteSpeed(note);
							n.layer = note.hiSpeedGroup;
							n.ID = nextID++;
							n.parentID = startID;

							if (n.friction)
								printf("Note at %d-%d is friction", n.tick, n.lane);

							HoldStepType type = note.type == 3 ? HoldStepType::Normal : HoldStepType::Hidden;
							if (stepIgnore.find(key) != stepIgnore.end())
								type = HoldStepType::Skip;

							notes[n.ID] = n;
							hold.steps.push_back(HoldStep{ n.ID, type, ease });
						}
						break;
						default:
							break;
						}
					}

					if (hold.start.ID == 0 || hold.end == 0)
						throw std::runtime_error("Invalid hold note");

					if (isGuide)
						hold.guideColor = critical ? GuideColor::Yellow : GuideColor::Green;

					holds[startID] = hold;
				}
			};

		slideFillFunc(sus.slides, false);
		slideFillFunc(sus.guides, true);

		std::vector<Tempo> tempos;
		tempos.reserve(sus.bpms.size());
		for (const auto& tempo : sus.bpms)
			tempos.push_back(Tempo(tempo.tick, tempo.bpm));

		std::map<int, TimeSignature> timeSignatures;
		for (const auto& sign : sus.barlengths)
		{
			auto fraction = barLengthToFraction(sign.length, 4.0f);
			timeSignatures.insert(std::pair<int, TimeSignature>(sign.bar,
				TimeSignature{ sign.bar, fraction.first, fraction.second }));
		}

		std::vector<HiSpeedChange> hiSpeedChanges;
		std::vector<HiSpeedLayer> layers;
		if (sus.hiSpeedGroups.size() > 1)
		{
			for (size_t group = 0; group < sus.hiSpeedGroups.size(); ++group)
			{
				const std::string id = group < sus.hiSpeedGroupIds.size() ? sus.hiSpeedGroupIds[group] : std::to_string(group);
				layers.push_back({ group == 0 ? "default" : "TIL" + id });
				for (const auto& speed : sus.hiSpeedGroups[group])
					hiSpeedChanges.push_back({ speed.tick, speed.speed, static_cast<int>(group) });
			}
		}
		else
		{
			layers.push_back({ "default" });
			hiSpeedChanges.reserve(sus.hiSpeeds.size());
			for (const auto& speed : sus.hiSpeeds)
				hiSpeedChanges.push_back({ speed.tick, speed.speed });
		}

		std::sort(hiSpeedChanges.begin(), hiSpeedChanges.end(),
			[](const auto& a, const auto& b) { return a.tick < b.tick; });

		std::vector<SEVolumeChange> seVolumeChanges;
		seVolumeChanges.reserve(sus.seVolumes.size());
		for (const auto& volume : sus.seVolumes)
			seVolumeChanges.push_back({ volume.tick, volume.volume });

		std::sort(seVolumeChanges.begin(), seVolumeChanges.end(),
			[](const auto& a, const auto& b) { return a.tick < b.tick; });

		Score score;
		score.metadata = metadata;
		score.notes = notes;
		score.holdNotes = holds;
		score.tempoChanges = tempos;
		score.timeSignatures = timeSignatures;
		score.hiSpeedChanges = hiSpeedChanges;
		score.seVolumeChanges = seVolumeChanges;
		score.skills = skills;
		score.fever = fever;
		score.layers = layers;

		if (ignoredTypeFour)
			warnings.push_back(IO::formatString("%d type-4 tap(s) inside the playfield were ignored (not a Project SEKAI note).", ignoredTypeFour));
		if (mergedDuplicates)
			warnings.push_back(IO::formatString("%d duplicated note(s)/slide(s) at the same position were merged, as the game does.", mergedDuplicates));

		return score;
	}

	SUS SusSerializer::scoreToSus(const Score& score)
	{
		constexpr std::array<int, static_cast<int>(FlickType::FlickTypeCount)> flickToType{ 0, 1, 3, 4 };

		std::vector<SUSNote> taps, directionals;
		std::vector<std::vector<SUSNote>> slides;
		std::vector<std::vector<SUSNote>> guides;
		std::vector<BPM> bpms;
		std::vector<BarLength> barlengths;
		std::vector<HiSpeed> hiSpeeds;
		std::vector<SEVolume> seVolumes;

		std::unordered_set<std::string> criticalKeys;
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() == NoteType::Tap)
			{
				int type = note.friction ? 5 : 1;
				if (note.critical)
				{
					type++;
					criticalKeys.insert(noteKey(note));
				}
				taps.push_back(SUSNote{ note.tick, note.lane + 2, note.width, type, getEffectiveSpeedRatio(note, score) });

				if (note.isFlick())
					directionals.push_back(SUSNote{ note.tick, note.lane + 2, note.width, flickToType[static_cast<int>(note.flick)], getEffectiveSpeedRatio(note, score) });
			}
		}

		/*
			Generally guides can be placed on the same tick and lane (key) as other notes.
			If one of those notes or the guide is critical however, it will cause all the notes with the same key to be critical.
		*/
		std::vector<HoldNote> exportHolds;
		exportHolds.reserve(score.holdNotes.size());

		for (const auto& [_, hold] : score.holdNotes)
			exportHolds.emplace_back(hold);

		std::sort(exportHolds.begin(), exportHolds.end(), [&score](const HoldNote& a, const HoldNote& b)
			{
				const Note& n1 = score.notes.at(a.start.ID);
				const Note& n2 = score.notes.at(b.start.ID);
				return n1.tick == n2.tick ? n1.ID < n2.ID : n1.tick < n2.tick;
			});

		for (const auto& hold : exportHolds)
		{
			std::vector<SUSNote> slide;
			slide.reserve(hold.steps.size() + 2);

			const Note& start = score.notes.at(hold.start.ID);
			const float holdSpeedRatio = getEffectiveSpeedRatio(start, score);

			slide.push_back(SUSNote{ start.tick, start.lane + 2, start.width, 1, holdSpeedRatio });

			bool hasEase = hold.start.ease != EaseType::Linear;
			bool invisibleTapPoint =
				hold.startType == HoldNoteType::Hidden ||
				hold.startType != HoldNoteType::Normal && hasEase ||
				hold.startType != HoldNoteType::Normal && start.critical;

			if (hasEase)
				directionals.push_back(SUSNote{ start.tick, start.lane + 2, start.width, hold.start.ease == EaseType::EaseIn ? 2 : 6, holdSpeedRatio });

			bool guideAlreadyOnCritical = hold.isGuide() && criticalKeys.find(noteKey(start)) != criticalKeys.end();

			// We'll use type 1 to indicate it's a normal note
			int type = invisibleTapPoint ? 7 : start.friction ? 5 : 1;
			if (start.critical && criticalKeys.find(noteKey(start)) == criticalKeys.end())
			{
				type++;
				criticalKeys.insert(noteKey(start));
			}

			if (type > 1 && !((type == 7 || type == 8) && guideAlreadyOnCritical))
				taps.push_back({ start.tick, start.lane + 2, start.width, type, holdSpeedRatio });

			for (const auto& step : hold.steps)
			{
				const Note& midNote = score.notes.at(step.ID);

				slide.push_back(SUSNote{ midNote.tick, midNote.lane + 2, midNote.width, step.type == HoldStepType::Hidden ? 5 : 3, holdSpeedRatio });
				if (step.type == HoldStepType::Skip)
				{
					taps.push_back(SUSNote{ midNote.tick, midNote.lane + 2, midNote.width, 3, holdSpeedRatio });
				}
				else if (step.ease != EaseType::Linear)
				{
					int stepTapType = hold.isGuide() ? start.critical ? 8 : 7 : 1;
					taps.push_back(SUSNote{ midNote.tick, midNote.lane + 2, midNote.width, stepTapType, holdSpeedRatio });
					directionals.push_back(SUSNote{ midNote.tick, midNote.lane + 2, midNote.width, step.ease == EaseType::EaseIn ? 2 : 6, holdSpeedRatio });
				}
			}

			const Note& end = score.notes.at(hold.end);

			slide.push_back(SUSNote{ end.tick, end.lane + 2, end.width, 2, holdSpeedRatio });

			// Hidden and guide slides do not have flicks
			if (end.isFlick() && hold.endType == HoldNoteType::Normal)
			{
				directionals.push_back(SUSNote{ end.tick, end.lane + 2, end.width, flickToType[static_cast<int>(end.flick)], holdSpeedRatio });

				// Critical friction notes use type 6 not 2
				if (end.critical && !start.critical && !end.friction)
					taps.push_back(SUSNote{ end.tick, end.lane + 2, end.width, 2, holdSpeedRatio });
			}

			int endType = hold.endType == HoldNoteType::Hidden ? 7 : end.friction ? 5 : 1;
			if (end.critical)
			{
				endType++;
				criticalKeys.insert(noteKey(end));
			}

			if (endType != 1 && endType != 2)
				taps.push_back(SUSNote{ end.tick, end.lane + 2, end.width, endType, holdSpeedRatio });

			if (hold.isGuide())
			{
				guides.push_back(slide);
			}
			else
			{
				slides.push_back(slide);
			}
		}

		for (const auto& skill : score.skills)
			taps.push_back(SUSNote{ skill.tick, 0, 1, 4 });

		if (score.fever.chanceTick != -1)
			taps.push_back(SUSNote{ score.fever.chanceTick, 15, 1, 1 });

		if (score.fever.startTick != -1)
			taps.push_back(SUSNote{ score.fever.startTick, 15, 1, 2 });

		for (const auto& tempo : score.tempoChanges)
			bpms.push_back(BPM{ tempo.tick, tempo.bpm });

		if (!bpms.size())
			bpms.push_back(BPM{ 0, 120 });

		for (const auto& [measure, ts] : score.timeSignatures)
			barlengths.push_back(BarLength{ ts.measure, ((float)ts.numerator / (float)ts.denominator) * 4 });

		std::vector<std::vector<HiSpeed>> hiSpeedGroups;
		if (score.layers.size() > 1)
		{
			hiSpeedGroups.resize(score.layers.size());
			for (const auto& hiSpeed : score.hiSpeedChanges)
				if (hiSpeed.layer >= 0 && hiSpeed.layer < static_cast<int>(hiSpeedGroups.size()))
					hiSpeedGroups[hiSpeed.layer].push_back(HiSpeed{ hiSpeed.tick, hiSpeed.speed });
		}

		hiSpeeds.reserve(score.hiSpeedChanges.size());
		for (const auto& hiSpeed : score.hiSpeedChanges)
			if (hiSpeed.layer == 0)
				hiSpeeds.push_back(HiSpeed{ hiSpeed.tick, hiSpeed.speed });

		seVolumes.reserve(score.seVolumeChanges.size());
		for (const auto& seVolume : score.seVolumeChanges)
			seVolumes.push_back(SEVolume{ seVolume.tick, seVolume.volume });

		SUSMetadata metadata;
		metadata.data["title"] = score.metadata.title;
		metadata.data["artist"] = score.metadata.artist;
		metadata.data["designer"] = score.metadata.author;
		metadata.requests.push_back("ticks_per_beat 480");

		// milliseconds -> seconds
		metadata.waveOffset = score.metadata.musicOffset / 1000.0f;

		// every note-bearing cell remembers the layer of the note it belongs to
		if (score.layers.size() > 1)
		{
			std::map<std::pair<int, int>, int> layerByKey;
			for (const auto& [id, note] : score.notes)
				layerByKey[{ note.tick, note.lane + 2 }] = note.layer;
			auto assign = [&layerByKey](SUSNote& n)
			{
				auto it = layerByKey.find({ n.tick, n.lane });
				if (it != layerByKey.end())
					n.hiSpeedGroup = it->second;
			};
			for (auto& n : taps) assign(n);
			for (auto& n : directionals) assign(n);
			for (auto& slide : slides) for (auto& n : slide) assign(n);
			for (auto& guide : guides) for (auto& n : guide) assign(n);
		}

		SUS sus{ metadata, taps, directionals, slides, guides, bpms, barlengths, hiSpeeds, seVolumes };
		sus.hiSpeedGroups = hiSpeedGroups;
		return sus;
	}
}
