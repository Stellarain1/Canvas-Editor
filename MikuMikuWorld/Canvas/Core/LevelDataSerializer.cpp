#include "LevelDataSerializer.h"
#include "ChartTransforms.h"
#include "../../Constants.h"
#include "../../File.h"
#include "../../IO.h"
#include "../../MathUtils.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace MikuMikuWorld
{
	using json = nlohmann::json;
	using ordered_json = nlohmann::ordered_json;

	namespace
	{
		constexpr double EPSILON = 1e-6;

		double tickToBeat(int tick) { return tick / static_cast<double>(TICKS_PER_BEAT); }
		int beatToTick(double beat) { return static_cast<int>(std::lround(beat * TICKS_PER_BEAT)); }
		double toUscLane(int lane, int width) { return lane - 6 + width / 2.0; }
		double toUscSize(int width) { return width / 2.0; }

		// ------------------------------------------------------------------ export helpers

		// An entity whose data keeps insertion order, like the Python exporter's dicts.
		struct Entity
		{
			std::string name;
			std::string archetype;
			std::vector<std::pair<std::string, ordered_json>> data; // value, or {"$ref": name}

			void set(const std::string& key, ordered_json value)
			{
				for (auto& [k, v] : data)
				{
					if (k == key)
					{
						v = std::move(value);
						return;
					}
				}
				data.emplace_back(key, std::move(value));
			}

			double num(const std::string& key) const
			{
				for (const auto& [k, v] : data)
					if (k == key && v.is_number())
						return v.get<double>();
				return 0.0;
			}
		};

		ordered_json ref(const Entity& entity) { return ordered_json{ { "$ref", entity.name } }; }

		class EntityList
		{
		public:
			std::vector<Entity> entities;

			size_t add(const std::string& archetype)
			{
				Entity entity;
				entity.archetype = archetype;
				entity.name = toBase36(nextName++);
				entities.push_back(std::move(entity));
				return entities.size() - 1;
			}

			Entity& operator[](size_t index) { return entities[index]; }

			ordered_json exportJson() const
			{
				ordered_json out = ordered_json::array();
				for (const auto& entity : entities)
				{
					ordered_json data = ordered_json::array();
					for (const auto& [key, value] : entity.data)
					{
						if (value.is_object() && value.contains("$ref"))
							data.push_back({ { "name", key }, { "ref", value["$ref"] } });
						else
							data.push_back({ { "name", key }, { "value", value } });
					}
					out.push_back({ { "name", entity.name }, { "archetype", entity.archetype }, { "data", data } });
				}
				return out;
			}

		private:
			long long nextName = 0;

			static std::string toBase36(long long n)
			{
				static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
				std::string out;
				do
				{
					out.insert(out.begin(), digits[n % 36]);
					n /= 36;
				} while (n > 0);
				return out;
			}
		};

		int connectorEase(EaseType ease)
		{
			switch (ease)
			{
			case EaseType::EaseIn: return 2;
			case EaseType::EaseOut: return 3;
			case EaseType::EaseInOut: return 4;
			case EaseType::EaseOutIn: return 5;
			default: return 1;
			}
		}

		EaseType fromConnectorEase(int value)
		{
			switch (value)
			{
			case 2: return EaseType::EaseIn;
			case 3: return EaseType::EaseOut;
			case 4: return EaseType::EaseInOut;
			case 5: return EaseType::EaseOutIn;
			default: return EaseType::Linear;
			}
		}

		int directionValue(FlickType flick)
		{
			switch (flick)
			{
			case FlickType::Left: return 1;
			case FlickType::Right: return 2;
			default: return 0;
			}
		}

		FlickType flickFromDirectionValue(int value)
		{
			switch (value)
			{
			case 1: return FlickType::Left;
			case 2: return FlickType::Right;
			default: return FlickType::Default;
			}
		}

		// ------------------------------------------------------------------ import helpers

		struct RawEntity
		{
			std::string name;
			std::string archetype;
			std::unordered_map<std::string, json> data;	// value or ref (as string)
			std::unordered_set<std::string> refKeys;

			double num(const std::string& key, double fallback = 0.0) const
			{
				auto it = data.find(key);
				return it != data.end() && it->second.is_number() ? it->second.get<double>() : fallback;
			}

			std::string refOf(const std::string& key) const
			{
				auto it = data.find(key);
				if (it == data.end())
					return {};
				if (it->second.is_string())
					return it->second.get<std::string>();
				return {};
			}

			bool has(const std::string& key) const { return data.find(key) != data.end(); }
		};

		std::vector<RawEntity> readEntities(const json& levelData)
		{
			std::vector<RawEntity> entities;
			if (!levelData.contains("entities") || !levelData["entities"].is_array())
				return entities;

			for (const auto& e : levelData["entities"])
			{
				RawEntity raw;
				raw.name = e.contains("name") && e["name"].is_string() ? e["name"].get<std::string>() : "";
				raw.archetype = e.contains("archetype") && e["archetype"].is_string() ? e["archetype"].get<std::string>() : "";
				if (e.contains("data") && e["data"].is_array())
				{
					for (const auto& item : e["data"])
					{
						if (!item.contains("name"))
							continue;
						const std::string key = item["name"].get<std::string>();
						if (item.contains("ref"))
						{
							raw.data[key] = item["ref"];
							raw.refKeys.insert(key);
						}
						else if (item.contains("value"))
						{
							raw.data[key] = item["value"];
						}
					}
				}
				else if (e.contains("data") && e["data"].is_object())
				{
					for (const auto& [key, value] : e["data"].items())
						raw.data[key] = value;
				}
				entities.push_back(std::move(raw));
			}
			return entities;
		}

		bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }
		bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

		struct LaneFix
		{
			int adjusted{};

			// countAsAdjusted=false for positions we computed ourselves (interpolated hidden
			// steps), which are fractional by nature and not a loss of information.
			void apply(Note& note, double lane, double size, bool countAsAdjusted = true)
			{
				const double width = size * 2.0;
				const double left = lane + 6.0 - size;
				int w = std::clamp(static_cast<int>(std::lround(width)), MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
				int l = std::clamp(static_cast<int>(std::lround(left)), MIN_LANE, MAX_LANE - w + 1);
				if (countAsAdjusted && (std::abs(w - width) > 1e-4 || std::abs(l - left) > 1e-4))
					++adjusted;
				note.lane = l;
				note.width = w;
			}
		};
	}

	// =====================================================================================
	// Export (Next Sekai)
	// =====================================================================================

	json LevelDataSerializer::exportNextSekai(const Score& input)
	{
		warnings.clear();
		Score score = input;
		Canvas::normalizeEvents(score);

		EntityList list;
		list.add("Initialization");

		for (const auto& tempo : score.tempoChanges)
		{
			size_t e = list.add("#BPM_CHANGE");
			list[e].set("#BEAT", tickToBeat(tempo.tick));
			list[e].set("#BPM", tempo.bpm);
		}

		// Timescale groups: one per layer, plus derived groups for notes with a per-note speed
		// ratio (the engine has no per-note speed, but a group whose speeds are all scaled by
		// the ratio scrolls the note exactly the same way).
		std::vector<std::vector<HiSpeedChange>> groups = splitHiSpeedLayers(score);
		std::map<std::pair<int, float>, int> ratioGroups;
		auto groupFor = [&](int layer, float ratio) -> int
		{
			layer = std::clamp(layer, 0, static_cast<int>(groups.size()) - 1);
			if (std::abs(ratio - 1.0f) <= 0.0001f || !std::isfinite(ratio) || ratio <= 0.0f)
				return layer;

			auto key = std::make_pair(layer, ratio);
			auto it = ratioGroups.find(key);
			if (it != ratioGroups.end())
				return it->second;

			std::vector<HiSpeedChange> scaled = groups[layer];
			if (scaled.empty() || scaled.front().tick != 0)
				scaled.insert(scaled.begin(), HiSpeedChange{ 0, 1.0f, layer });
			for (auto& change : scaled)
				change.speed *= ratio;

			// groups is only appended to here, index stays valid
			groups.push_back(scaled);
			const int index = static_cast<int>(groups.size()) - 1;
			ratioGroups[key] = index;
			return index;
		};

		// resolve every note's group before creating the group entities
		std::unordered_map<int, int> noteGroup;
		for (const auto& [id, note] : score.notes)
		{
			const float ratio = getEffectiveSpeedRatio(note, score);
			noteGroup[id] = groupFor(note.layer, ratio);
		}

		std::vector<size_t> groupEntities;
		for (auto& changes : groups)
		{
			if (changes.empty())
				changes.push_back(HiSpeedChange{ 0, 1.0f, 0 });

			const size_t group = list.add("#TIMESCALE_GROUP");
			groupEntities.push_back(group);
			size_t previous = SIZE_MAX;
			for (const auto& change : changes)
			{
				const size_t e = list.add("#TIMESCALE_CHANGE");
				list[e].set("#BEAT", tickToBeat(change.tick));
				list[e].set("#TIMESCALE", change.speed);
				list[e].set("#TIMESCALE_SKIP", 0);
				list[e].set("#TIMESCALE_GROUP", ref(list[group]));
				list[e].set("#TIMESCALE_EASE", 0);
				if (previous == SIZE_MAX)
					list[group].set("first", ref(list[e]));
				else
					list[previous].set("next", ref(list[e]));
				previous = e;
			}
		}

		if (!ratioGroups.empty())
			warnings.push_back(IO::formatString("Per-note speed ratios became %d extra timescale group(s) (LevelData has no per-note speed).", static_cast<int>(ratioGroups.size())));

		for (const auto& skill : score.skills)
		{
			const size_t e = list.add("Skill");
			list[e].set("#BEAT", tickToBeat(skill.tick));
		}
		if (score.fever.chanceTick >= 0)
		{
			const size_t e = list.add("FeverChance");
			list[e].set("#BEAT", tickToBeat(score.fever.chanceTick));
		}
		if (score.fever.startTick >= 0)
		{
			const size_t e = list.add("FeverStart");
			list[e].set("#BEAT", tickToBeat(score.fever.startTick));
		}

		std::vector<size_t> simEligible;

		auto noteData = [&](Entity& e, const Note& note, int group)
		{
			e.set("#BEAT", tickToBeat(note.tick));
			e.set("#TIMESCALE_GROUP", ref(list[groupEntities[group]]));
			e.set("lane", toUscLane(note.lane, note.width));
			e.set("size", toUscSize(note.width));
		};

		// ---- singles
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap)
				continue;

			std::string archetype = note.critical ? "Critical" : "Normal";
			if (note.isFlick())
				archetype += note.friction ? "TraceFlick" : "Flick";
			else
				archetype += note.friction ? "Trace" : "Tap";
			archetype += "Note";

			const size_t e = list.add(archetype);
			noteData(list[e], note, noteGroup[id]);
			list[e].set("direction", directionValue(note.flick));
			list[e].set("isAttached", 0);
			list[e].set("connectorEase", 1);
			list[e].set("isSeparator", 0);
			list[e].set("segmentKind", note.critical ? 2 : 1);
			list[e].set("segmentAlpha", 1);
			simEligible.push_back(e);
		}

		// ---- slides
		for (const auto& [id, hold] : score.holdNotes)
		{
			if (hold.isGuide())
				continue;

			const Note& start = score.notes.at(hold.start.ID);
			const bool critical = start.critical;

			size_t head = SIZE_MAX, prevJoint = SIZE_MAX, prevNote = SIZE_MAX;
			std::vector<size_t> queuedAttach, connectors;
			double nextHiddenTickBeat = std::floor(tickToBeat(start.tick) * 2 + 1) / 2;

			struct Point { const Note* note; int kind; EaseType ease; HoldStepType stepType; };	// kind 0 start, 1 step, 2 end
			std::vector<Point> points;
			points.push_back({ &start, 0, hold.start.ease, HoldStepType::Normal });
			for (const auto& step : hold.steps)
				points.push_back({ &score.notes.at(step.ID), 1, step.ease, step.type });
			points.push_back({ &score.notes.at(hold.end), 2, EaseType::Linear, HoldStepType::Normal });

			for (const auto& point : points)
			{
				const Note& note = *point.note;
				std::string archetype;
				bool simLine = false, attached = false;
				FlickType direction = FlickType::Default;

				if (point.kind == 0)
				{
					if (hold.startType != HoldNoteType::Normal)
						archetype = "Anchor";
					else
					{
						archetype = std::string(critical ? "Critical" : "Normal") + "Head" + (note.friction ? "Trace" : "Tap");
						simLine = true;
					}
				}
				else if (point.kind == 2)
				{
					if (hold.endType != HoldNoteType::Normal)
						archetype = "Anchor";
					else
					{
						archetype = std::string(note.critical ? "Critical" : "Normal") + "Tail";
						if (note.isFlick())
						{
							archetype += note.friction ? "TraceFlick" : "Flick";
							direction = note.flick;
						}
						else
						{
							archetype += note.friction ? "Trace" : "Release";
						}
						simLine = true;
					}
				}
				else if (point.stepType == HoldStepType::Skip)
				{
					attached = true;
					archetype = std::string(critical ? "Critical" : "Normal") + "Tick";
				}
				else if (point.stepType == HoldStepType::Hidden)
				{
					archetype = "Anchor";
				}
				else
				{
					archetype = std::string(critical ? "Critical" : "Normal") + "Tick";
				}
				archetype += "Note";

				const size_t e = list.add(archetype);
				noteData(list[e], note, noteGroup[note.ID]);
				list[e].set("direction", directionValue(direction));
				list[e].set("isAttached", attached ? 1 : 0);
				list[e].set("connectorEase", connectorEase(point.ease));
				list[e].set("isSeparator", 0);
				list[e].set("segmentKind", critical ? 2 : 1);
				list[e].set("segmentAlpha", 1);
				list[e].set("segmentLayer", 0);

				if (simLine)
					simEligible.push_back(e);
				if (head == SIZE_MAX)
					head = e;
				list[e].set("activeHead", ref(list[head]));

				if (attached)
				{
					queuedAttach.push_back(e);
				}
				else
				{
					if (prevJoint != SIZE_MAX)
					{
						for (size_t attach : queuedAttach)
						{
							list[attach].set("attachHead", ref(list[prevJoint]));
							list[attach].set("attachTail", ref(list[e]));
						}
						queuedAttach.clear();

						// the engine's half-beat combo ticks
						while (nextHiddenTickBeat + EPSILON < list[e].num("#BEAT"))
						{
							const size_t t = list.add("TransientHiddenTickNote");
							list[t].set("#BEAT", std::round(nextHiddenTickBeat * 1e9) / 1e9);
							list[t].set("#TIMESCALE_GROUP", ref(list[groupEntities[0]]));
							list[t].set("lane", list[e].num("lane"));
							list[t].set("size", list[e].num("size"));
							list[t].set("direction", 0);
							list[t].set("isAttached", 1);
							list[t].set("connectorEase", 1);
							list[t].set("isSeparator", 0);
							list[t].set("segmentKind", 1);
							list[t].set("segmentAlpha", 0);
							list[t].set("activeHead", ref(list[head]));
							list[t].set("attachHead", ref(list[prevJoint]));
							list[t].set("attachTail", ref(list[e]));
							nextHiddenTickBeat += 0.5;
						}

						const size_t c = list.add("Connector");
						list[c].set("head", ref(list[prevJoint]));
						list[c].set("tail", ref(list[e]));
						connectors.push_back(c);
					}
					prevJoint = e;
				}

				if (prevNote != SIZE_MAX)
					list[prevNote].set("next", ref(list[e]));
				prevNote = e;
			}

			for (size_t c : connectors)
			{
				list[c].set("segmentHead", ref(list[head]));
				list[c].set("segmentTail", ref(list[prevJoint]));
				list[c].set("activeHead", ref(list[head]));
				list[c].set("activeTail", ref(list[prevJoint]));
			}
		}

		// ---- guides
		static const int GUIDE_KINDS[] = { 101, 102, 103, 104, 105, 106, 107, 108 };
		for (const auto& [id, hold] : score.holdNotes)
		{
			if (!hold.isGuide())
				continue;

			std::vector<std::pair<const Note*, EaseType>> points;
			points.push_back({ &score.notes.at(hold.start.ID), hold.start.ease });
			for (const auto& step : hold.steps)
				points.push_back({ &score.notes.at(step.ID), step.ease });
			points.push_back({ &score.notes.at(hold.end), EaseType::Linear });

			size_t head = SIZE_MAX, prev = SIZE_MAX;
			std::vector<size_t> connectors;
			for (const auto& [note, ease] : points)
			{
				const size_t e = list.add("AnchorNote");
				noteData(list[e], *note, noteGroup[note->ID]);
				list[e].set("direction", 0);
				list[e].set("isAttached", 0);
				list[e].set("connectorEase", connectorEase(ease));
				list[e].set("isSeparator", 0);
				list[e].set("segmentKind", GUIDE_KINDS[static_cast<int>(hold.guideColor)]);
				list[e].set("segmentAlpha", 1);
				list[e].set("segmentLayer", 0);

				if (head == SIZE_MAX)
					head = e;
				if (prev != SIZE_MAX)
				{
					const size_t c = list.add("Connector");
					list[c].set("head", ref(list[prev]));
					list[c].set("tail", ref(list[e]));
					connectors.push_back(c);
					list[prev].set("next", ref(list[e]));
				}
				prev = e;
			}

			for (size_t c : connectors)
			{
				list[c].set("segmentHead", ref(list[head]));
				list[c].set("segmentTail", ref(list[prev]));
				list[c].set("activeHead", ref(list[head]));
				list[c].set("activeTail", ref(list[prev]));
			}

			if (hold.fadeType == FadeType::In)
				list[head].set("segmentAlpha", 0);
			else if (hold.fadeType == FadeType::Out)
				list[prev].set("segmentAlpha", 0);
		}

		// ---- sim lines between notes on (almost) the same beat
		std::sort(simEligible.begin(), simEligible.end(), [&](size_t a, size_t b)
		{
			const double ba = list[a].num("#BEAT"), bb = list[b].num("#BEAT");
			return ba != bb ? ba < bb : list[a].num("lane") < list[b].num("lane");
		});
		std::vector<std::vector<size_t>> simGroups;
		for (size_t e : simEligible)
		{
			if (simGroups.empty() || std::abs(list[e].num("#BEAT") - list[simGroups.back().front()].num("#BEAT")) >= 1e-2)
				simGroups.push_back({ e });
			else
				simGroups.back().push_back(e);
		}
		for (const auto& group : simGroups)
		{
			for (size_t i = 1; i < group.size(); ++i)
			{
				const size_t s = list.add("SimLine");
				list[s].set("left", ref(list[group[i - 1]]));
				list[s].set("right", ref(list[group[i]]));
			}
		}

		if (!score.seVolumeChanges.empty())
			warnings.push_back(IO::formatString("%d SE volume change(s) were dropped: LevelData has no SE volume.", static_cast<int>(score.seVolumeChanges.size())));

		ordered_json level;
		level["bgmOffset"] = score.metadata.musicOffset == 0 ? 0.0 : -score.metadata.musicOffset / 1000.0;
		level["entities"] = list.exportJson();
		return json::parse(level.dump());
	}

	// =====================================================================================
	// Import
	// =====================================================================================

	LevelDataSerializer::Variant LevelDataSerializer::detect(const json& levelData)
	{
		if (!levelData.is_object() || !levelData.contains("bgmOffset") || !levelData.contains("entities") || !levelData["entities"].is_array())
			return Variant::Unknown;

		static const std::set<std::string> chcyOnly =
		{
			"NormalSlideStartNote", "CriticalSlideStartNote", "NormalSlideEndNote", "CriticalSlideEndNote",
			"NormalSlideEndFlickNote", "CriticalSlideEndFlickNote", "IgnoredSlideTickNote", "NormalSlideTickNote",
			"CriticalSlideTickNote", "HiddenSlideTickNote", "NormalAttachedSlideTickNote", "CriticalAttachedSlideTickNote",
			"NormalSlideConnector", "CriticalSlideConnector", "NormalTraceSlideStartNote", "CriticalTraceSlideStartNote",
			"NormalTraceSlideEndNote", "CriticalTraceSlideEndNote", "NonDirectionalTraceFlickNote", "HiddenSlideStartNote",
			"TimeScaleGroup", "TimeScaleChange", "Guide", "InputManager"
		};

		for (const auto& e : levelData["entities"])
		{
			const std::string archetype = e.contains("archetype") && e["archetype"].is_string() ? e["archetype"].get<std::string>() : "";
			if (chcyOnly.count(archetype))
				return Variant::ChartCyanvas;
		}
		// everything else with bgmOffset + entities reads as the Next Sekai family, like the library's detector
		return Variant::NextSekai;
	}

	Score LevelDataSerializer::importLevelData(const json& levelData)
	{
		warnings.clear();
		switch (detect(levelData))
		{
		case Variant::ChartCyanvas:
			return importChartCyanvas(levelData);
		case Variant::NextSekai:
			return importNextSekai(levelData);
		default:
			throw std::runtime_error("Not a Sonolus LevelData file.");
		}
	}

	Score LevelDataSerializer::importNextSekai(const json& levelData)
	{
		Score score;
		score.tempoChanges.clear();
		score.layers.clear();

		const double bgmOffset = levelData.contains("bgmOffset") && levelData["bgmOffset"].is_number() ? levelData["bgmOffset"].get<double>() : 0.0;
		score.metadata.musicOffset = static_cast<float>(-bgmOffset * 1000.0);

		std::vector<RawEntity> entities = readEntities(levelData);
		std::unordered_map<std::string, size_t> byName;
		for (size_t i = 0; i < entities.size(); ++i)
			if (!entities[i].name.empty())
				byName[entities[i].name] = i;

		auto find = [&](const std::string& name) -> const RawEntity*
		{
			auto it = byName.find(name);
			return it == byName.end() ? nullptr : &entities[it->second];
		};

		// timescale groups in order of appearance (their index is the layer)
		std::unordered_map<std::string, int> groupIndex;
		int anonymousGroups = 0;
		for (const auto& e : entities)
		{
			if (e.archetype != "#TIMESCALE_GROUP")
				continue;

			const int layer = static_cast<int>(score.layers.size());
			groupIndex[e.name.empty() ? "_anon" + std::to_string(anonymousGroups++) : e.name] = layer;
			score.layers.push_back({ layer == 0 ? "default" : "Layer " + std::to_string(layer) });

			const RawEntity* change = find(e.refOf("first"));
			int guard = 0;
			while (change && guard++ < 100000)
			{
				score.hiSpeedChanges.push_back({ beatToTick(change->num("#BEAT")), static_cast<float>(change->num("#TIMESCALE", 1.0)), layer });
				change = find(change->refOf("next"));
			}
		}
		if (score.layers.empty())
			score.layers.push_back({ "default" });

		auto layerOf = [&](const RawEntity& e)
		{
			auto it = groupIndex.find(e.refOf("#TIMESCALE_GROUP"));
			return it == groupIndex.end() ? 0 : it->second;
		};

		LaneFix lanes;
		int damage = 0, fake = 0;

		for (const auto& e : entities)
		{
			if (e.archetype == "#BPM_CHANGE")
				score.tempoChanges.push_back({ beatToTick(e.num("#BEAT")), static_cast<float>(e.num("#BPM", 120.0)) });
			else if (e.archetype == "Skill")
				score.skills.push_back({ nextSkillID++, beatToTick(e.num("#BEAT")) });
			else if (e.archetype == "FeverChance")
				score.fever.chanceTick = beatToTick(e.num("#BEAT"));
			else if (e.archetype == "FeverStart")
				score.fever.startTick = beatToTick(e.num("#BEAT"));
		}

		// notes referenced as someone's `next` are never the start of a chain
		std::unordered_set<std::string> referenced;
		for (const auto& e : entities)
		{
			const std::string next = e.refOf("next");
			if (!next.empty() && e.archetype != "#TIMESCALE_CHANGE")
				referenced.insert(next);
		}

		auto makeNote = [&](NoteType type, const RawEntity& e) -> Note
		{
			Note note(type);
			note.ID = nextID++;
			note.tick = beatToTick(e.num("#BEAT"));
			lanes.apply(note, e.num("lane"), e.num("size", 1.5));
			note.layer = layerOf(e);
			return note;
		};

		// ---- singles: note archetypes that are not part of a chain
		for (const auto& e : entities)
		{
			const std::string& a = e.archetype;
			if (!contains(a, "Note") || a == "AnchorNote" || a == "TransientHiddenTickNote" || a == "FakeAnchorNote" || a == "FakeTransientHiddenTickNote")
				continue;
			if (e.has("activeHead") || contains(a, "Head") || contains(a, "Tail") || contains(a, "Tick"))
				continue;
			if (referenced.count(e.name) || !e.refOf("next").empty())
				continue;

			if (startsWith(a, "Fake"))
			{
				++fake;
				continue;
			}
			if (startsWith(a, "Damage"))
			{
				++damage;
				continue;
			}

			Note note = makeNote(NoteType::Tap, e);
			note.critical = startsWith(a, "Critical");
			note.friction = contains(a, "Trace");
			if (contains(a, "Flick"))
				note.flick = flickFromDirectionValue(static_cast<int>(e.num("direction")));
			score.notes[note.ID] = note;
		}

		// ---- chains (slides and guides): entities with a `next` nobody points to
		for (const auto& e : entities)
		{
			if (e.name.empty() || referenced.count(e.name) || e.refOf("next").empty() || e.archetype == "#TIMESCALE_CHANGE" || e.archetype == "#TIMESCALE_GROUP")
				continue;

			std::vector<const RawEntity*> chain;
			const RawEntity* current = &e;
			int guard = 0;
			while (current && guard++ < 100000)
			{
				chain.push_back(current);
				current = find(current->refOf("next"));
			}
			if (chain.size() < 2)
				continue;

			const bool hasKind = e.has("segmentKind");
			const int kind = static_cast<int>(e.num("segmentKind", 1));
			const bool isFake = (kind >= 50 && kind < 100) || startsWith(e.archetype, "Fake");
			if (isFake)
			{
				++fake;
				continue;
			}

			const bool allAnchors = std::all_of(chain.begin(), chain.end(), [](const RawEntity* n) { return n->archetype == "AnchorNote"; });
			// guides are anchor chains with a guide colour (101-108); files without segmentKind
			// tell them apart by the missing activeHead
			const bool guide = allAnchors && (hasKind ? kind >= 100 : !e.has("activeHead"));

			if (guide)
			{
				HoldNote hold;
				hold.startType = hold.endType = HoldNoteType::Guide;
				hold.guideColor = kind >= 101 && kind <= 108 ? static_cast<GuideColor>(kind - 101) : GuideColor::Green;
				const double headAlpha = chain.front()->num("segmentAlpha", 1), tailAlpha = chain.back()->num("segmentAlpha", 1);
				hold.fadeType = headAlpha < tailAlpha ? FadeType::In : tailAlpha < headAlpha ? FadeType::Out : FadeType::None;

				int startId = -1;
				for (size_t i = 0; i < chain.size(); ++i)
				{
					const bool first = i == 0, last = i + 1 == chain.size();
					Note note = makeNote(first ? NoteType::Hold : last ? NoteType::HoldEnd : NoteType::HoldMid, *chain[i]);
					note.parentID = first ? -1 : startId;
					score.notes[note.ID] = note;
					const EaseType ease = fromConnectorEase(static_cast<int>(chain[i]->num("connectorEase", 1)));
					if (first)
					{
						startId = note.ID;
						hold.start = { note.ID, HoldStepType::Normal, ease };
					}
					else if (last)
						hold.end = note.ID;
					else
						hold.steps.push_back({ note.ID, HoldStepType::Hidden, ease });
				}
				syncGuideCritical(score, hold);
				score.holdNotes[startId] = hold;
				continue;
			}

			// slide
			const bool critical = (kind % 50) == 2;
			HoldNote hold;
			int startId = -1;
			for (size_t i = 0; i < chain.size(); ++i)
			{
				const RawEntity& n = *chain[i];
				std::string a = startsWith(n.archetype, "Fake") ? n.archetype.substr(4) : n.archetype;
				const bool first = i == 0, last = i + 1 == chain.size();
				const EaseType ease = fromConnectorEase(static_cast<int>(n.num("connectorEase", 1)));

				if (first)
				{
					Note note = makeNote(NoteType::Hold, n);
					note.critical = critical;
					note.friction = contains(a, "Trace");
					hold.startType = startsWith(a, "Anchor") ? HoldNoteType::Hidden : HoldNoteType::Normal;
					hold.start = { note.ID, HoldStepType::Normal, ease };
					startId = note.ID;
					score.notes[note.ID] = note;
				}
				else if (last)
				{
					Note note = makeNote(NoteType::HoldEnd, n);
					note.parentID = startId;
					hold.endType = startsWith(a, "Anchor") ? HoldNoteType::Hidden : HoldNoteType::Normal;
					note.friction = contains(a, "Trace");
					note.critical = critical || startsWith(a, "Critical");
					if (contains(a, "Flick"))
						note.flick = flickFromDirectionValue(static_cast<int>(n.num("direction")));
					hold.end = note.ID;
					score.notes[note.ID] = note;
				}
				else
				{
					const bool attached = static_cast<int>(n.num("isAttached")) == 1;
					if (attached && startsWith(a, "TransientHiddenTick"))
						continue;	// invisible attach: no shape, no combo, nothing to keep

					Note note = makeNote(NoteType::HoldMid, n);
					note.parentID = startId;
					note.critical = critical;
					HoldStepType type = HoldStepType::Normal;
					if (attached)
						type = HoldStepType::Skip;
					else if (startsWith(a, "Anchor") || startsWith(a, "TransientHiddenTick"))
						type = HoldStepType::Hidden;
					hold.steps.push_back({ note.ID, type, ease });
					score.notes[note.ID] = note;
				}
			}

			if (startId < 0 || hold.end == 0)
				continue;
			sortHoldSteps(score, hold);
			score.holdNotes[startId] = hold;
		}

		if (score.tempoChanges.empty())
			score.tempoChanges.push_back({ 0, 160.0f });
		if (lanes.adjusted)
			warnings.push_back(IO::formatString("%d note(s) used fractional or extended lanes and were snapped into the 12 lanes.", lanes.adjusted));
		if (damage)
			warnings.push_back(IO::formatString("%d damage note(s) were dropped (not supported by Project SEKAI).", damage));
		if (fake)
			warnings.push_back(IO::formatString("%d fake note(s)/slide(s) were dropped.", fake));

		Canvas::normalizeEvents(score);
		return score;
	}

	Score LevelDataSerializer::importChartCyanvas(const json& levelData)
	{
		Score score;
		score.tempoChanges.clear();
		score.layers.clear();

		const double bgmOffset = levelData.contains("bgmOffset") && levelData["bgmOffset"].is_number() ? levelData["bgmOffset"].get<double>() : 0.0;
		score.metadata.musicOffset = static_cast<float>(-bgmOffset * 1000.0);

		std::vector<RawEntity> entities = readEntities(levelData);
		std::unordered_map<std::string, size_t> byName;
		for (size_t i = 0; i < entities.size(); ++i)
			if (!entities[i].name.empty())
				byName[entities[i].name] = i;
		auto find = [&](const std::string& name) -> const RawEntity*
		{
			auto it = byName.find(name);
			return it == byName.end() ? nullptr : &entities[it->second];
		};

		// "tsg:<i>" groups -> layers
		std::map<int, const RawEntity*> groupsByIndex;
		for (const auto& e : entities)
		{
			if (e.archetype == "TimeScaleGroup" && startsWith(e.name, "tsg:"))
				groupsByIndex[std::atoi(e.name.substr(4).c_str())] = &e;
		}
		std::unordered_map<std::string, int> layerByGroup;
		for (const auto& [index, group] : groupsByIndex)
		{
			const int layer = static_cast<int>(score.layers.size());
			layerByGroup[group->name] = layer;
			score.layers.push_back({ layer == 0 ? "default" : "Layer " + std::to_string(layer) });

			const RawEntity* change = find(group->refOf("first"));
			int guard = 0;
			const int length = static_cast<int>(group->num("length", 100000));
			while (change && change->archetype == "TimeScaleChange" && guard++ < std::max(1, length))
			{
				score.hiSpeedChanges.push_back({ beatToTick(change->num("#BEAT")), static_cast<float>(change->num("timeScale", 1.0)), layer });
				change = find(change->refOf("next"));
			}
		}
		if (score.layers.empty())
			score.layers.push_back({ "default" });

		auto layerOf = [&](const RawEntity& e, const std::string& key = "timeScaleGroup")
		{
			auto it = layerByGroup.find(e.refOf(key));
			return it == layerByGroup.end() ? 0 : it->second;
		};

		static const std::map<int, EaseType> eases = { { -2, EaseType::EaseOutIn }, { -1, EaseType::EaseOut }, { 0, EaseType::Linear }, { 1, EaseType::EaseIn }, { 2, EaseType::EaseInOut } };
		auto chcyEase = [](double value)
		{
			auto it = eases.find(static_cast<int>(std::lround(value)));
			return it == eases.end() ? EaseType::Linear : it->second;
		};
		auto chcyFlick = [](double value)
		{
			const int v = static_cast<int>(std::lround(value));
			return v < 0 ? FlickType::Left : v > 0 ? FlickType::Right : FlickType::Default;
		};

		LaneFix lanes;
		int damage = 0, nonDirectional = 0;

		for (const auto& e : entities)
		{
			const std::string& a = e.archetype;
			if (a == "#BPM_CHANGE")
			{
				score.tempoChanges.push_back({ beatToTick(e.num("#BEAT")), static_cast<float>(e.num("#BPM", 120.0)) });
				continue;
			}

			if (a == "DamageNote")
			{
				++damage;
				continue;
			}

			const bool single = (contains(a, "TapNote") || contains(a, "FlickNote") || contains(a, "TraceNote")) && !contains(a, "Slide");
			if (!single)
				continue;

			Note note(NoteType::Tap);
			note.ID = nextID++;
			note.tick = beatToTick(e.num("#BEAT"));
			lanes.apply(note, e.num("lane"), e.num("size", 1.5));
			note.layer = layerOf(e);
			note.critical = startsWith(a, "Critical");
			note.friction = contains(a, "Trace");
			if (a == "NonDirectionalTraceFlickNote")
			{
				note.flick = FlickType::Default;
				++nonDirectional;
			}
			else if (contains(a, "Flick"))
			{
				note.flick = chcyFlick(e.num("direction"));
			}
			score.notes[note.ID] = note;
		}

		// ---- slides, rebuilt from their connectors
		std::map<std::string, std::vector<const RawEntity*>> connectorsBySlide;
		for (const auto& e : entities)
			if (e.archetype == "NormalSlideConnector" || e.archetype == "CriticalSlideConnector")
				connectorsBySlide[e.refOf("start")].push_back(&e);

		std::unordered_map<std::string, int> slideStartId;	// connector start ref -> hold id
		std::unordered_map<std::string, std::pair<const RawEntity*, int>> connectorInfo;	// connector name -> (connector, hold id)

		for (auto& [startName, connectors] : connectorsBySlide)
		{
			std::sort(connectors.begin(), connectors.end(), [&](const RawEntity* x, const RawEntity* y)
			{
				const RawEntity* hx = find(x->refOf("head"));
				const RawEntity* hy = find(y->refOf("head"));
				return (hx ? hx->num("#BEAT") : 0) < (hy ? hy->num("#BEAT") : 0);
			});

			const RawEntity* startEntity = find(startName);
			const RawEntity* endEntity = find(connectors.front()->refOf("end"));
			if (!startEntity || !endEntity)
				continue;

			const bool critical = connectors.front()->archetype == "CriticalSlideConnector";
			HoldNote hold;

			Note start(NoteType::Hold);
			start.ID = nextID++;
			start.tick = beatToTick(startEntity->num("#BEAT"));
			lanes.apply(start, startEntity->num("lane"), startEntity->num("size", 1.5));
			start.layer = layerOf(*startEntity);
			start.critical = critical;
			start.friction = contains(startEntity->archetype, "Trace");
			hold.startType = startEntity->archetype == "HiddenSlideStartNote" ? HoldNoteType::Hidden : HoldNoteType::Normal;
			hold.start = { start.ID, HoldStepType::Normal, chcyEase(connectors.front()->num("ease")) };
			score.notes[start.ID] = start;

			// joints between the start and the end: every connector tail but the last
			for (size_t i = 0; i + 1 < connectors.size(); ++i)
			{
				const RawEntity* tail = find(connectors[i]->refOf("tail"));
				if (!tail)
					continue;

				Note mid(NoteType::HoldMid);
				mid.ID = nextID++;
				mid.parentID = start.ID;
				mid.tick = beatToTick(tail->num("#BEAT"));
				lanes.apply(mid, tail->num("lane"), tail->num("size", 1.5));
				mid.layer = layerOf(*tail);
				mid.critical = critical;
				const HoldStepType type = tail->archetype == "HiddenSlideTickNote" ? HoldStepType::Hidden : HoldStepType::Normal;
				hold.steps.push_back({ mid.ID, type, chcyEase(connectors[i + 1]->num("ease")) });
				score.notes[mid.ID] = mid;
			}

			Note end(NoteType::HoldEnd);
			end.ID = nextID++;
			end.parentID = start.ID;
			end.tick = beatToTick(endEntity->num("#BEAT"));
			lanes.apply(end, endEntity->num("lane"), endEntity->num("size", 1.5));
			end.layer = layerOf(*endEntity);
			const std::string& ea = endEntity->archetype;
			hold.endType = ea == "HiddenSlideTickNote" ? HoldNoteType::Hidden : HoldNoteType::Normal;
			end.friction = contains(ea, "Trace");
			end.critical = critical || startsWith(ea, "Critical");
			if (contains(ea, "Flick"))
				end.flick = chcyFlick(endEntity->num("direction"));
			hold.end = end.ID;
			score.notes[end.ID] = end;

			sortHoldSteps(score, hold);
			score.holdNotes[start.ID] = hold;
			slideStartId[startName] = start.ID;
			for (const RawEntity* c : connectors)
				connectorInfo[c->name] = { c, start.ID };
		}

		// ---- attached ticks (skip steps); their position lies on the connector's curve
		for (const auto& e : entities)
		{
			if (e.archetype != "NormalAttachedSlideTickNote" && e.archetype != "CriticalAttachedSlideTickNote")
				continue;

			auto it = connectorInfo.find(e.refOf("attach"));
			if (it == connectorInfo.end())
				continue;

			const RawEntity* connector = it->second.first;
			const RawEntity* head = find(connector->refOf("head"));
			const RawEntity* tail = find(connector->refOf("tail"));
			if (!head || !tail)
				continue;

			const double beat = e.num("#BEAT");
			const double span = tail->num("#BEAT") - head->num("#BEAT");
			const float t = span > 0 ? static_cast<float>((beat - head->num("#BEAT")) / span) : 0.0f;
			auto ease = getEaseFunction(chcyEase(connector->num("ease")));
			const float lane = ease(static_cast<float>(head->num("lane")), static_cast<float>(tail->num("lane")), t);
			const float size = ease(static_cast<float>(head->num("size", 1.5)), static_cast<float>(tail->num("size", 1.5)), t);

			HoldNote& hold = score.holdNotes.at(it->second.second);
			Note mid(NoteType::HoldMid);
			mid.ID = nextID++;
			mid.parentID = hold.start.ID;
			mid.tick = beatToTick(beat);
			lanes.apply(mid, lane, size, false);
			mid.layer = layerOf(e);
			mid.critical = score.notes.at(hold.start.ID).critical;
			hold.steps.push_back({ mid.ID, HoldStepType::Skip, EaseType::Linear });
			score.notes[mid.ID] = mid;
			sortHoldSteps(score, hold);
		}

		// ---- guides: one "Guide" entity per segment
		struct GuideKey
		{
			double startBeat, startLane, startSize, endBeat, endLane, endSize;
			int color, fade;
			bool operator<(const GuideKey& o) const
			{
				return std::tie(startBeat, startLane, startSize, endBeat, endLane, endSize, color, fade) <
					std::tie(o.startBeat, o.startLane, o.startSize, o.endBeat, o.endLane, o.endSize, o.color, o.fade);
			}
		};
		std::map<GuideKey, std::vector<const RawEntity*>> guides;
		for (const auto& e : entities)
		{
			if (e.archetype != "Guide")
				continue;
			GuideKey key{ e.num("startBeat"), e.num("startLane"), e.num("startSize"), e.num("endBeat"), e.num("endLane"), e.num("endSize"),
				static_cast<int>(e.num("color", 2)), static_cast<int>(e.num("fade", 0)) };
			guides[key].push_back(&e);
		}

		for (auto& [key, segments] : guides)
		{
			std::sort(segments.begin(), segments.end(), [](const RawEntity* x, const RawEntity* y) { return x->num("headBeat") < y->num("headBeat"); });

			HoldNote hold;
			hold.startType = hold.endType = HoldNoteType::Guide;
			hold.guideColor = static_cast<GuideColor>(std::clamp(key.color, 0, static_cast<int>(GuideColor::GuideColorCount) - 1));
			hold.fadeType = key.fade == 2 ? FadeType::In : key.fade == 1 ? FadeType::None : FadeType::Out;

			int startId = -1;
			auto addPoint = [&](const RawEntity& seg, const char* prefix, NoteType type, EaseType ease)
			{
				const std::string p = prefix;
				Note note(type);
				note.ID = nextID++;
				note.parentID = startId;
				note.tick = beatToTick(seg.num(p + "Beat"));
				lanes.apply(note, seg.num(p + "Lane"), seg.num(p + "Size", 1.5));
				note.layer = layerOf(seg, p + "TimeScaleGroup");
				score.notes[note.ID] = note;

				if (type == NoteType::Hold)
				{
					startId = note.ID;
					score.notes[note.ID].parentID = -1;
					hold.start = { note.ID, HoldStepType::Normal, ease };
				}
				else if (type == NoteType::HoldEnd)
					hold.end = note.ID;
				else
					hold.steps.push_back({ note.ID, HoldStepType::Hidden, ease });
			};

			for (size_t i = 0; i < segments.size(); ++i)
				addPoint(*segments[i], "head", i == 0 ? NoteType::Hold : NoteType::HoldMid, chcyEase(segments[i]->num("ease")));
			addPoint(*segments.back(), "tail", NoteType::HoldEnd, EaseType::Linear);

			syncGuideCritical(score, hold);
			score.holdNotes[startId] = hold;
		}

		if (score.tempoChanges.empty())
			score.tempoChanges.push_back({ 0, 160.0f });

		warnings.push_back("Chart Cyanvas LevelData has no skill or FEVER markers; use Chart > Auto-place Skills & FEVER if the chart needs them.");
		if (lanes.adjusted)
			warnings.push_back(IO::formatString("%d note(s) used fractional or extended lanes and were snapped into the 12 lanes.", lanes.adjusted));
		if (damage)
			warnings.push_back(IO::formatString("%d damage note(s) were dropped (not supported by Project SEKAI).", damage));
		if (nonDirectional)
			warnings.push_back(IO::formatString("%d non-directional trace flick(s) became trace flicks pointing up.", nonDirectional));

		Canvas::normalizeEvents(score);
		return score;
	}

	// =====================================================================================
	// File entry points
	// =====================================================================================

	void LevelDataSerializer::serialize(const Score& score, std::string filename)
	{
		const std::string text = exportNextSekai(score).dump();
		std::vector<uint8_t> bytes(text.begin(), text.end());
		if (gzipOutput)
			bytes = IO::deflateGzip(bytes);

		IO::File file(filename, IO::FileMode::WriteBinary);
		file.writeAllBytes(bytes);
		file.flush();
		file.close();
	}

	Score LevelDataSerializer::deserialize(std::string filename)
	{
		IO::File file(filename, IO::FileMode::ReadBinary);
		std::vector<uint8_t> bytes = file.readAllBytes();
		file.close();
		if (IO::isGzipCompressed(bytes))
			bytes = IO::inflateGzip(bytes);

		const json level = json::parse(bytes.begin(), bytes.end(), nullptr, false);
		if (level.is_discarded())
			throw std::runtime_error("The LevelData file is not valid JSON.");
		return importLevelData(level);
	}
}
