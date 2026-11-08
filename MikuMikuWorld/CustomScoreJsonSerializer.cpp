#include "CustomScoreJsonSerializer.h"

#include "Constants.h"
#include "File.h"
#include "IO.h"
#include "JsonIO.h"
#include "Note.h"
#include "Canvas/Core/ChartTransforms.h"
#include "Canvas/Core/SkillFever.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace MikuMikuWorld
{
	using json = nlohmann::json;
	using ordered_json = nlohmann::ordered_json;

	namespace custom_score_json
	{
		struct RawNote
		{
			int id{};
			int ticks{};
			int laneStart{};
			int laneEnd{};
			int category{};
			int noteBaseType{};
			int previousConnectionId{ -1 };
			int nextConnectionId{ -1 };
			int direction{};
			int noteLineType{};
			float speedRatio{ 1.0f };
			bool critical{};
			bool isSkip{};
		};

		struct ExportEvent
		{
			int id{};
			int eventType{};
			int ticks{};
			json changeValue{};
		};

		struct ExportNote
		{
			int id{};
			int ticks{};
			int laneStart{};
			int laneEnd{};
			int category{};
			int type{};
			float speedRatio{ 1.0f };
			int noteLineType{};
			int noteBaseType{};
			int previousConnectionId{ -1 };
			int nextConnectionId{ -1 };
			int direction{};
			bool isSkip{};
			bool isSingle{};
			bool isConnectedFirst{};
			bool isConnectedLast{};
		};

		enum class SlideKind
		{
			Start,
			End,
			Relay,
			Invisible,
		};

		int toInt(const json& js, const char* key, int fallback = 0)
		{
			const auto it = js.find(key);
			if (it == js.end() || it->is_null())
				return fallback;

			if (it->is_number_integer())
				return it->get<int>();

			if (it->is_number())
				return static_cast<int>(std::round(it->get<double>()));

			if (it->is_string())
				return std::atoi(it->get_ref<const std::string&>().c_str());

			return fallback;
		}

		float toFloat(const json& js, const char* key, float fallback = 0.0f)
		{
			const auto it = js.find(key);
			if (it == js.end() || it->is_null())
				return fallback;

			if (it->is_number())
				return it->get<float>();

			if (it->is_string())
				return std::strtof(it->get_ref<const std::string&>().c_str(), nullptr);

			return fallback;
		}

		bool toBool(const json& js, const char* key, bool fallback = false)
		{
			const auto it = js.find(key);
			if (it == js.end() || it->is_null())
				return fallback;

			if (it->is_boolean())
				return it->get<bool>();

			if (it->is_number())
				return it->get<int>() != 0;

			return fallback;
		}

		bool looksLikeCustomScoreJson(const json& js)
		{
			if (!js.is_object())
				return false;

			return jsonIO::arrayHasData(js, "MusicScoreEventDataList")
				|| jsonIO::arrayHasData(js, "NoteList")
				|| (jsonIO::keyExists(js, "MusicScoreEventDataList") && js["MusicScoreEventDataList"].is_array())
				|| (jsonIO::keyExists(js, "NoteList") && js["NoteList"].is_array());
		}

		int lane(const RawNote& note)
		{
			return std::clamp(note.laneStart, MIN_LANE, MAX_LANE);
		}

		int width(const RawNote& note)
		{
			return std::clamp(note.laneEnd - note.laneStart + 1, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
		}

		bool isVisibleRelaySlideNote(const RawNote& note)
		{
			return note.noteBaseType == 5 || note.category == 2;
		}

		bool shouldVisibleRelayAffectPath(const RawNote& note)
		{
			return isVisibleRelaySlideNote(note) && !note.isSkip;
		}

		bool isVisibleRelayAttachment(const RawNote& note)
		{
			return isVisibleRelaySlideNote(note) && !shouldVisibleRelayAffectPath(note);
		}

		bool isDecorationSlideNote(const RawNote& note)
		{
			return note.category == 9 || note.noteBaseType == 10 || note.noteBaseType == 13;
		}

		bool isDecorationSlideChain(const std::vector<const RawNote*>& chain)
		{
			return std::any_of(chain.begin(), chain.end(), [](const RawNote* note)
			{
				return note != nullptr && isDecorationSlideNote(*note);
			});
		}

		SlideKind getSlideKind(const RawNote& note, bool isLast)
		{
			if (isLast)
				return SlideKind::End;

			const int base = note.noteBaseType;
			if (base == 2 || base == 8 || base == 9 || base == 10 || note.category == 6)
				return SlideKind::Start;

			if (base == 1 || base == 3 || base == 11 || base == 12 || base == 13)
				return SlideKind::End;

			if (base == 6 || base == 14 || note.category == 11)
				return SlideKind::Invisible;

			return SlideKind::Relay;
		}

		bool isCancelNote(const RawNote& note)
		{
			return note.noteBaseType == 9 || note.noteBaseType == 12 || note.noteBaseType == 10 || note.noteBaseType == 13;
		}

		bool isTraceNote(const RawNote& note)
		{
			return note.noteBaseType == 4 || note.noteBaseType == 8 || note.noteBaseType == 11
				|| note.category == 4 || note.category == 6 || note.category == 8;
		}

		bool isTraceFlickNote(const RawNote& note)
		{
			return note.noteBaseType == 4 || note.category == 8;
		}

		bool isFlickNote(const RawNote& note)
		{
			return note.noteBaseType == 3 || note.category == 3;
		}

		FlickType flickTypeFromDirection(int direction)
		{
			if (direction == 1)
				return FlickType::Left;

			if (direction == 2)
				return FlickType::Right;

			return FlickType::Default;
		}

		EaseType easeFromLineType(int noteLineType)
		{
			if (noteLineType == 2)
				return EaseType::EaseIn;

			if (noteLineType == 1)
				return EaseType::EaseOut;

			return EaseType::Linear;
		}

		int lineTypeFromEase(EaseType ease)
		{
			if (ease == EaseType::EaseOut)
				return 1;

			if (ease == EaseType::EaseIn)
				return 2;

			return 0;
		}

		int directionFromFlick(FlickType flick)
		{
			if (flick == FlickType::Left)
				return 1;

			if (flick == FlickType::Right)
				return 2;

			return 0;
		}

		int laneEndFrom(const Note& note)
		{
			return std::clamp(note.lane + note.width - 1, MIN_LANE, MAX_LANE);
		}

		float sanitizeSpeedRatio(float speedRatio)
		{
			if (!std::isfinite(speedRatio) || speedRatio <= 0.0f)
				return 1.0f;

			return speedRatio;
		}

		ExportNote makeBaseExportNote(const Note& note, int id, float speedRatio)
		{
			return ExportNote{
				id,
				note.tick,
				std::clamp(note.lane, MIN_LANE, MAX_LANE),
				laneEndFrom(note),
				0,
				note.critical ? 1 : 0,
				sanitizeSpeedRatio(speedRatio),
				0,
				1,
				-1,
				-1,
				directionFromFlick(note.flick),
				false,
				true,
				false,
				false
			};
		}

		void applyTapLikeCategory(ExportNote& raw, const Note& note)
		{
			if (note.friction && note.flick != FlickType::None)
			{
				raw.category = 8;
				raw.noteBaseType = 4;
			}
			else if (note.friction)
			{
				raw.category = 4;
				raw.noteBaseType = 11;
			}
			else if (note.flick != FlickType::None)
			{
				raw.category = 3;
				raw.noteBaseType = 3;
			}
			else
			{
				raw.category = 0;
				raw.noteBaseType = 1;
			}
		}

		ExportNote makeTapExportNote(const Note& note, int id)
		{
			ExportNote raw = makeBaseExportNote(note, id, note.speedRatio);
			applyTapLikeCategory(raw, note);
			return raw;
		}

		ExportNote makeChainEndpointExportNote(
			const Note& note,
			HoldNoteType endpointType,
			bool isStart,
			int id,
			int previousId,
			int nextId,
			EaseType ease,
			float speedRatio)
		{
			ExportNote raw = makeBaseExportNote(note, id, speedRatio);
			raw.previousConnectionId = previousId;
			raw.nextConnectionId = nextId;
			raw.isSingle = false;
			raw.isConnectedFirst = isStart;
			raw.isConnectedLast = !isStart && nextId == -1;
			raw.noteLineType = lineTypeFromEase(ease);

			if (endpointType == HoldNoteType::Guide)
			{
				raw.category = 9;
				raw.noteBaseType = isStart ? 10 : 13;
				raw.direction = 0;
				return raw;
			}

			if (endpointType == HoldNoteType::Hidden)
			{
				raw.category = isStart ? 7 : 5;
				raw.noteBaseType = isStart ? 9 : 12;
				raw.direction = 0;
				return raw;
			}

			if (isStart)
			{
				if (note.friction)
				{
					raw.category = 6;
					raw.noteBaseType = 8;
					raw.direction = 0;
					return raw;
				}

				raw.category = 1;
				raw.noteBaseType = 2;
				return raw;
			}

			if (note.flick == FlickType::None && !note.friction)
			{
				raw.category = 1;
				raw.noteBaseType = 1;
				return raw;
			}

			applyTapLikeCategory(raw, note);
			return raw;
		}

		ExportNote makeChainMidExportNote(
			const Note& note,
			const HoldStep& step,
			bool isGuide,
			int id,
			int previousId,
			int nextId,
			float speedRatio)
		{
			ExportNote raw = makeBaseExportNote(note, id, speedRatio);
			raw.previousConnectionId = previousId;
			raw.nextConnectionId = nextId;
			raw.noteLineType = lineTypeFromEase(step.ease);
			raw.isSingle = false;

			if (isGuide || step.type == HoldStepType::Hidden)
			{
				raw.category = isGuide ? 11 : 13;
				raw.noteBaseType = isGuide ? 14 : 6;
				raw.direction = 0;
				return raw;
			}

			raw.category = 2;
			raw.noteBaseType = 5;
			raw.isSkip = step.type == HoldStepType::Skip;
			return raw;
		}

		ordered_json toEventJson(const ExportEvent& event, int jsonId)
		{
			ordered_json value;
			value["$id"] = std::to_string(jsonId);
			value["id"] = event.id;
			value["eventType"] = event.eventType;
			value["ticks"] = event.ticks;
			value["changeValue"] = event.changeValue;
			return value;
		}

		ordered_json toNoteJson(const ExportNote& note, int jsonId)
		{
			ordered_json value;
			value["$id"] = std::to_string(jsonId);
			value["id"] = note.id;
			value["ticks"] = note.ticks;
			value["laneStart"] = note.laneStart;
			value["laneEnd"] = note.laneEnd;
			value["category"] = note.category;
			value["type"] = note.type;
			value["speedRatio"] = note.speedRatio;
			value["noteLineType"] = note.noteLineType;
			value["noteBaseType"] = note.noteBaseType;
			value["previousConnectionId"] = note.previousConnectionId;
			value["nextConnectionId"] = note.nextConnectionId;
			value["direction"] = note.direction;
			value["isSkip"] = note.isSkip;
			value["IsSingle"] = note.isSingle;
			value["IsConnectedFirst"] = note.isConnectedFirst;
			value["IsConnectedLast"] = note.isConnectedLast;
			return value;
		}

		double roundFloatForJson(float value)
		{
			return std::strtod(IO::formatFixedFloatTrimmed(value, 6).c_str(), nullptr);
		}

		RawNote readNote(const json& js)
		{
			return RawNote{
				toInt(js, "id"),
				toInt(js, "ticks"),
				toInt(js, "laneStart"),
				toInt(js, "laneEnd"),
				toInt(js, "category"),
				toInt(js, "noteBaseType"),
				toInt(js, "previousConnectionId", -1),
				toInt(js, "nextConnectionId", -1),
				toInt(js, "direction"),
				toInt(js, "noteLineType"),
				sanitizeSpeedRatio(toFloat(js, "speedRatio", 1.0f)),
				toBool(js, "type"),
				toBool(js, "isSkip"),
			};
		}

		void addTap(Score& score, const RawNote& raw, bool forceCritical = false)
		{
			if (isCancelNote(raw))
				return;

			Note note(NoteType::Tap, raw.ticks, lane(raw), width(raw));
			note.ID = nextID++;
			note.speedRatio = raw.speedRatio;
			note.critical = forceCritical || raw.critical;
			note.friction = isTraceNote(raw);
			if (isFlickNote(raw) || isTraceFlickNote(raw) || raw.direction == 1 || raw.direction == 2)
				note.flick = flickTypeFromDirection(raw.direction);

			score.notes[note.ID] = note;
		}

		HoldStepType stepTypeFromRaw(const RawNote& raw, SlideKind kind)
		{
			if (kind == SlideKind::Invisible)
				return HoldStepType::Hidden;

			if (isVisibleRelayAttachment(raw))
				return HoldStepType::Skip;

			return HoldStepType::Normal;
		}

		HoldNoteType endpointTypeFromRaw(const RawNote& raw, bool decoration)
		{
			if (decoration)
				return HoldNoteType::Guide;

			return isCancelNote(raw) ? HoldNoteType::Hidden : HoldNoteType::Normal;
		}

		std::vector<const RawNote*> removeAdjacentVisibleRelayDuplicates(const std::vector<const RawNote*>& chain)
		{
			std::vector<const RawNote*> filtered;
			filtered.reserve(chain.size());
			for (size_t index = 0; index < chain.size(); ++index)
			{
				const RawNote* note = chain[index];
				const RawNote* next = index + 1 < chain.size() ? chain[index + 1] : nullptr;
				if (note != nullptr && next != nullptr
					&& isVisibleRelaySlideNote(*note)
					&& isVisibleRelaySlideNote(*next)
					&& std::abs(next->ticks - note->ticks) <= 1)
				{
					continue;
				}

				filtered.push_back(note);
			}

			return filtered;
		}

		std::vector<std::vector<const RawNote*>> buildChains(
			const std::vector<RawNote>& notes,
			const std::unordered_map<int, const RawNote*>& byId,
			std::unordered_set<int>& connectedIds)
		{
			std::vector<std::vector<const RawNote*>> chains;

			for (const auto& note : notes)
			{
				if (connectedIds.find(note.id) != connectedIds.end())
					continue;

				if (note.nextConnectionId == -1 && note.previousConnectionId == -1)
					continue;

				if (note.previousConnectionId != -1)
					continue;

				std::vector<const RawNote*> chain;
				const RawNote* current = &note;
				while (current != nullptr && connectedIds.find(current->id) == connectedIds.end())
				{
					chain.push_back(current);
					connectedIds.insert(current->id);
					if (current->nextConnectionId == -1)
					{
						current = nullptr;
					}
					else
					{
						const auto it = byId.find(current->nextConnectionId);
						current = it == byId.end() ? nullptr : it->second;
					}
				}

				if (!chain.empty())
					chains.push_back(chain);
			}

			for (const auto& note : notes)
			{
				if (connectedIds.find(note.id) == connectedIds.end()
					&& (note.nextConnectionId != -1 || note.previousConnectionId != -1))
				{
					chains.push_back({ &note });
					connectedIds.insert(note.id);
				}
			}

			return chains;
		}

		void addChain(Score& score, const std::vector<const RawNote*>& rawChain)
		{
			const std::vector<const RawNote*> chain = removeAdjacentVisibleRelayDuplicates(rawChain);
			if (chain.size() < 2 || chain.front() == nullptr || chain.back() == nullptr)
				return;

			const bool decoration = isDecorationSlideChain(chain);
			const int startID = nextID++;
			HoldNote hold{};

			for (size_t index = 0; index < chain.size(); ++index)
			{
				const RawNote& raw = *chain[index];
				const bool isFirst = index == 0;
				const bool isLast = index + 1 == chain.size();
				const SlideKind kind = getSlideKind(raw, isLast);
				const NoteType noteType = isFirst ? NoteType::Hold : (isLast ? NoteType::HoldEnd : NoteType::HoldMid);

				Note note(noteType, raw.ticks, lane(raw), width(raw));
				note.ID = isFirst ? startID : nextID++;
				note.parentID = isFirst ? -1 : startID;
				note.speedRatio = raw.speedRatio;
				note.critical = raw.critical;
				note.friction = isTraceNote(raw);
				if (isFlickNote(raw) || isTraceFlickNote(raw) || raw.direction == 1 || raw.direction == 2)
					note.flick = flickTypeFromDirection(raw.direction);

				if (isFirst)
				{
					hold.start = HoldStep{ note.ID, HoldStepType::Normal, easeFromLineType(raw.noteLineType) };
					hold.startType = endpointTypeFromRaw(raw, decoration);
				}
				else if (isLast)
				{
					hold.end = note.ID;
					hold.endType = endpointTypeFromRaw(raw, decoration);
				}
				else
				{
					hold.steps.push_back(HoldStep{ note.ID, stepTypeFromRaw(raw, kind), easeFromLineType(raw.noteLineType) });
				}

				score.notes[note.ID] = note;

				if (!decoration && endpointTypeFromRaw(raw, false) == HoldNoteType::Hidden && raw.critical)
					addTap(score, raw, true);
			}

			if (hold.start.ID == 0 || hold.end == 0)
				return;

			sortHoldSteps(score, hold);
			if (hold.isGuide())
			{
				hold.guideColor = score.notes.at(startID).critical ? GuideColor::Yellow : GuideColor::Green;
				syncGuideCritical(score, hold);
			}
			score.holdNotes[startID] = hold;
		}

		Score parse(const std::string& text, float normalizedOffsetMs)
		{
			const json root = json::parse(text);
			if (!looksLikeCustomScoreJson(root))
				throw std::runtime_error("Not a supported ChartMaker JSON file.");

			Score score{};
			score.metadata.musicOffset = normalizedOffsetMs;
			score.metadata.musicId = toInt(root, "MusicId");
			score.tempoChanges.clear();
			score.hiSpeedChanges.clear();
			score.seVolumeChanges.clear();

			std::vector<std::pair<int, std::string>> timeSignatureEvents;
			const auto eventList = root.find("MusicScoreEventDataList");
			if (eventList != root.end() && eventList->is_array())
			{
				for (const auto& event : *eventList)
				{
					const int type = toInt(event, "eventType", -1);
					const int tick = toInt(event, "ticks");
					if (type == 0)
						score.tempoChanges.push_back({ tick, toFloat(event, "changeValue", 120.0f) });
					else if (type == 1)
						score.hiSpeedChanges.push_back({ tick, toFloat(event, "changeValue", 1.0f) });
					else if (type == 2)
						score.seVolumeChanges.push_back({ tick, toFloat(event, "changeValue", 1.0f) });
					else if (type == 3)
					{
						const auto value = event.find("changeValue");
						if (value != event.end() && value->is_string())
							timeSignatureEvents.push_back({ tick, value->get<std::string>() });
					}
				}
			}

			// time signatures are stored as "n/d" at a tick; turn ticks into measures in order,
			// since each change moves where the next measures start
			std::stable_sort(timeSignatureEvents.begin(), timeSignatureEvents.end(),
				[](const auto& a, const auto& b) { return a.first < b.first; });
			for (const auto& [tick, value] : timeSignatureEvents)
			{
				const size_t slash = value.find('/');
				if (slash == std::string::npos)
					continue;

				const int numerator = std::atoi(value.substr(0, slash).c_str());
				const int denominator = std::atoi(value.substr(slash + 1).c_str());
				if (numerator < MIN_TIME_SIGNATURE || denominator < MIN_TIME_SIGNATURE)
					continue;

				const int measure = accumulateMeasures(tick, TICKS_PER_BEAT, score.timeSignatures);
				score.timeSignatures[measure] = { measure, numerator, denominator };
			}

			// skills + fever ride along in our extension key (MusicScoreData itself has no event for them)
			const auto skillFever = root.find(Canvas::SkillFever::JSON_KEY);
			if (skillFever != root.end())
				Canvas::SkillFever::applyMarkers(score, Canvas::SkillFever::fromJson(*skillFever));

			if (score.tempoChanges.empty())
				score.tempoChanges.push_back({ 0, 120.0f });

			std::stable_sort(score.tempoChanges.begin(), score.tempoChanges.end(),
				[](const Tempo& left, const Tempo& right) { return left.tick < right.tick; });
			std::stable_sort(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(),
				[](const HiSpeedChange& left, const HiSpeedChange& right) { return left.tick < right.tick; });
			std::stable_sort(score.seVolumeChanges.begin(), score.seVolumeChanges.end(),
				[](const SEVolumeChange& left, const SEVolumeChange& right) { return left.tick < right.tick; });

			std::vector<RawNote> notes;
			const auto noteList = root.find("NoteList");
			if (noteList != root.end() && noteList->is_array())
			{
				notes.reserve(noteList->size());
				for (const auto& noteJson : *noteList)
					notes.push_back(readNote(noteJson));
			}

			std::stable_sort(notes.begin(), notes.end(), [](const RawNote& left, const RawNote& right)
			{
				if (left.ticks != right.ticks)
					return left.ticks < right.ticks;

				if (left.laneStart != right.laneStart)
					return left.laneStart < right.laneStart;

				return left.id < right.id;
			});

			std::unordered_map<int, const RawNote*> byId;
			byId.reserve(notes.size());
			for (const auto& note : notes)
				byId[note.id] = &note;

			std::unordered_set<int> connectedIds;
			connectedIds.reserve(notes.size());
			const auto chains = buildChains(notes, byId, connectedIds);
			for (const auto& chain : chains)
				addChain(score, chain);

			for (const auto& note : notes)
			{
				if (connectedIds.find(note.id) == connectedIds.end())
					addTap(score, note);
			}

			return score;
		}
	}

	void CustomScoreJsonSerializer::serialize(const Score& inputScore, std::string filename)
	{
		using namespace custom_score_json;

		warnings.clear();
		Score score = inputScore;
		Canvas::normalizeEvents(score);
		if (int n = Canvas::lowerExtendedEases(score))
			warnings.push_back(IO::formatString("%d ease-in-out / ease-out-in segment(s) were split into ease-in + ease-out halves (MusicScoreData only has in and out).", n));
		if (Canvas::usesLayers(score))
		{
			const auto report = Canvas::mergeLayersToDefault(score);
			warnings.push_back(IO::formatString("MusicScoreData has a single hi-speed timeline: %d note(s) moved to the default layer, %d hi-speed change(s) of other layers dropped.",
				report.notesMoved, report.hiSpeedsDropped));
		}
		if (int n = Canvas::countExtendedGuides(score))
			warnings.push_back(IO::formatString("%d guide(s) use a colour other than green/yellow or a fade other than fade-out; MusicScoreData stores them as green or yellow.", n));

		std::vector<ExportEvent> events;
		events.reserve(2 + score.timeSignatures.size() + score.tempoChanges.size() + score.hiSpeedChanges.size() + score.seVolumeChanges.size());

		int nextEventId = 1;
		if (score.hiSpeedChanges.empty() || score.hiSpeedChanges.front().tick != 0)
		{
			events.push_back({ nextEventId++, 1, 0, 1.0f });
		}

		for (const auto& hiSpeed : score.hiSpeedChanges)
		{
			events.push_back({ nextEventId++, 1, hiSpeed.tick, roundFloatForJson(hiSpeed.speed) });
		}

		if (score.timeSignatures.empty())
		{
			events.push_back({ nextEventId++, 3, 0, "4/4" });
		}
		else
		{
			for (const auto& [measure, signature] : score.timeSignatures)
			{
				const int ticks = measureToTicks(measure, TICKS_PER_BEAT, score.timeSignatures);
				events.push_back({
					nextEventId++,
					3,
					ticks,
					IO::formatString("%d/%d", signature.numerator, signature.denominator)
				});
			}
		}

		if (score.tempoChanges.empty())
		{
			events.push_back({ nextEventId++, 0, 0, 160.0f });
		}
		else
		{
			for (const auto& tempo : score.tempoChanges)
				events.push_back({ nextEventId++, 0, tempo.tick, roundFloatForJson(tempo.bpm) });
		}

		if (score.seVolumeChanges.empty() || score.seVolumeChanges.front().tick != 0)
		{
			events.push_back({ nextEventId++, 2, 0, 1.0f });
		}

		for (const auto& seVolume : score.seVolumeChanges)
		{
			events.push_back({ nextEventId++, 2, seVolume.tick, roundFloatForJson(seVolume.volume) });
		}

		std::vector<ExportNote> notes;
		notes.reserve(score.notes.size());
		int nextNoteId = 1;

		for (const auto& [_, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap)
				continue;

			notes.push_back(makeTapExportNote(note, nextNoteId++));
		}

		for (const auto& [_, hold] : score.holdNotes)
		{
			const Note& start = score.notes.at(hold.start.ID);
			const Note& end = score.notes.at(hold.end);
			const float holdSpeedRatio = sanitizeSpeedRatio(start.speedRatio);
			const bool isGuide = hold.isGuide();
			const size_t chainSize = hold.steps.size() + 2;
			std::vector<int> ids(chainSize);
			for (int& id : ids)
				id = nextNoteId++;

			notes.push_back(makeChainEndpointExportNote(
				start,
				hold.startType,
				true,
				ids.front(),
				-1,
				chainSize > 1 ? ids[1] : -1,
				hold.start.ease,
				holdSpeedRatio
			));

			for (size_t index = 0; index < hold.steps.size(); ++index)
			{
				const HoldStep& step = hold.steps[index];
				const Note& stepNote = score.notes.at(step.ID);
				notes.push_back(makeChainMidExportNote(
					stepNote,
					step,
					isGuide,
					ids[index + 1],
					ids[index],
					ids[index + 2],
					holdSpeedRatio
				));
			}

			notes.push_back(makeChainEndpointExportNote(
				end,
				hold.endType,
				false,
				ids.back(),
				ids[ids.size() - 2],
				-1,
				EaseType::Linear,
				holdSpeedRatio
			));
		}

		std::stable_sort(notes.begin(), notes.end(), [](const ExportNote& left, const ExportNote& right)
		{
			if (left.ticks != right.ticks)
				return left.ticks < right.ticks;

			if (left.laneStart != right.laneStart)
				return left.laneStart < right.laneStart;

			return left.id < right.id;
		});

		int maxTick = 0;
		for (const auto& event : events)
			maxTick = std::max(maxTick, event.ticks);
		for (const auto& note : notes)
			maxTick = std::max(maxTick, note.ticks);

		ordered_json root{
			{ "$id", "1" },
			{ "VersionCode", 10000 },
			{ "MusicScoreEventDataList", ordered_json::array() },
			{ "EventArray", ordered_json::array() },
			{ "NoteList", ordered_json::array() },
			{ "MusicScoreTicksMax", maxTick },
			{ "MusicId", score.metadata.musicId },
			{ "FullComboDataHash", nullptr }
		};

		int nextJsonId = 2;
		for (const auto& event : events)
			root["MusicScoreEventDataList"].push_back(toEventJson(event, nextJsonId++));
		for (const auto& note : notes)
			root["NoteList"].push_back(toNoteJson(note, nextJsonId++));

		// The game's reader ignores unknown keys; ours brings skills and fever back on import.
		const json skillFever = Canvas::SkillFever::toJson(Canvas::SkillFever::readMarkers(score));
		if (!skillFever.is_null())
			root[Canvas::SkillFever::JSON_KEY] = ordered_json::parse(skillFever.dump());

		if (base64Gzip)
		{
			// what the game's own chart maker / server uses: base64(gzip(json))
			const std::string text = root.dump();
			const std::vector<uint8_t> packed = IO::deflateGzip(std::vector<uint8_t>(text.begin(), text.end()));
			IO::File file(filename, IO::FileMode::WriteBinary);
			file.write(IO::base64Encode(packed));
			file.flush();
			file.close();
			return;
		}

		IO::File file(filename, IO::FileMode::Write);
		file.write(root.dump(2));
		file.flush();
		file.close();
	}

	Score CustomScoreJsonSerializer::deserialize(std::string filename)
	{
		if (!IO::File::exists(filename))
			throw std::runtime_error("Score JSON file not found.");

		IO::File scoreFile(filename, IO::FileMode::ReadBinary);
		std::vector<uint8_t> bytes = scoreFile.readAllBytes();
		scoreFile.close();

		warnings.clear();
		return parseBytes(bytes);
	}

	Score CustomScoreJsonSerializer::parseBytes(std::vector<uint8_t> bytes)
	{
		// plain json, raw gzip(json) or base64(gzip(json)), like the game and its chart maker
		if (IO::isGzipCompressed(bytes))
		{
			bytes = IO::inflateGzip(bytes);
		}
		else if (!bytes.empty() && bytes.front() != '{' && bytes.front() != '[')
		{
			std::vector<uint8_t> decoded;
			if (IO::base64Decode(std::string(bytes.begin(), bytes.end()), decoded) && IO::isGzipCompressed(decoded))
				bytes = IO::inflateGzip(decoded);
		}

		// strip a UTF-8 byte order mark
		if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
			bytes.erase(bytes.begin(), bytes.begin() + 3);

		return custom_score_json::parse(std::string(bytes.begin(), bytes.end()), 0.0f);
	}
}
