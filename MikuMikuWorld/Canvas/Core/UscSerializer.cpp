#include "UscSerializer.h"
#include "ChartTransforms.h"
#include "../../Constants.h"
#include "../../File.h"
#include "../../IO.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace MikuMikuWorld
{
	using json = nlohmann::json;
	using ordered_json = nlohmann::ordered_json;

	namespace
	{
		constexpr const char* USC_EASES[] = { "linear", "in", "out", "inout", "outin" };

		double tickToBeat(int tick) { return std::round(tick / static_cast<double>(TICKS_PER_BEAT) * 1e6) / 1e6; }
		int beatToTick(double beat) { return static_cast<int>(std::lround(beat * TICKS_PER_BEAT)); }

		double toUscLane(int lane, int width) { return lane - 6 + width / 2.0; }
		double toUscSize(int width) { return width / 2.0; }

		EaseType easeFromString(const std::string& ease)
		{
			for (int i = 0; i < static_cast<int>(EaseType::EaseTypeCount); ++i)
				if (ease == USC_EASES[i])
					return static_cast<EaseType>(i);
			return EaseType::Linear;
		}

		FlickType flickFromDirection(const json& obj)
		{
			const auto it = obj.find("direction");
			if (it == obj.end() || !it->is_string())
				return FlickType::None;
			const std::string direction = it->get<std::string>();
			if (direction == "left") return FlickType::Left;
			if (direction == "right") return FlickType::Right;
			if (direction == "up") return FlickType::Default;
			return FlickType::None;
		}

		const char* directionString(FlickType flick)
		{
			switch (flick)
			{
			case FlickType::Left: return "left";
			case FlickType::Right: return "right";
			default: return "up";
			}
		}

		double number(const json& obj, const char* key, double fallback = 0.0)
		{
			const auto it = obj.find(key);
			return it != obj.end() && it->is_number() ? it->get<double>() : fallback;
		}

		bool boolean(const json& obj, const char* key, bool fallback = false)
		{
			const auto it = obj.find(key);
			return it != obj.end() && it->is_boolean() ? it->get<bool>() : fallback;
		}

		std::string string(const json& obj, const char* key, const std::string& fallback = "")
		{
			const auto it = obj.find(key);
			return it != obj.end() && it->is_string() ? it->get<std::string>() : fallback;
		}

		struct LaneReader
		{
			int adjusted{};

			// USC lanes are centre-based floats; Project SEKAI has 12 whole lanes
			void apply(Note& note, const json& point)
			{
				const double size = number(point, "size", 1.5);
				const double lane = number(point, "lane", 0.0);
				const double width = size * 2.0;
				const double left = lane + 6.0 - size;

				int w = std::clamp(static_cast<int>(std::lround(width)), MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
				int l = std::clamp(static_cast<int>(std::lround(left)), MIN_LANE, MAX_LANE - w + 1);
				if (std::abs(w - width) > 1e-4 || std::abs(l - left) > 1e-4)
					++adjusted;
				note.lane = l;
				note.width = w;
			}
		};

		ordered_json pointBase(const Note& note, int layer)
		{
			ordered_json point;
			point["beat"] = tickToBeat(note.tick);
			point["lane"] = toUscLane(note.lane, note.width);
			point["size"] = toUscSize(note.width);
			point["timeScaleGroup"] = layer;
			return point;
		}
	}

	json UscSerializer::toJson(const Score& input)
	{
		warnings.clear();
		Score score = input;
		Canvas::normalizeEvents(score);

		ordered_json objects = ordered_json::array();

		for (const auto& tempo : score.tempoChanges)
			objects.push_back({ { "type", "bpm" }, { "beat", tickToBeat(tempo.tick) }, { "bpm", tempo.bpm } });

		const auto layers = splitHiSpeedLayers(score);
		for (const auto& layer : layers)
		{
			ordered_json changes = ordered_json::array();
			for (const auto& change : layer)
				changes.push_back({ { "beat", tickToBeat(change.tick) }, { "timeScale", change.speed } });
			if (changes.empty())
				changes.push_back({ { "beat", 0.0 }, { "timeScale", 1.0 } });
			objects.push_back({ { "type", "timeScaleGroup" }, { "changes", changes } });
		}

		for (const auto& skill : score.skills)
			objects.push_back({ { "type", "skill" }, { "beat", tickToBeat(skill.tick) } });
		if (score.fever.chanceTick >= 0)
			objects.push_back({ { "type", "feverChance" }, { "beat", tickToBeat(score.fever.chanceTick) } });
		if (score.fever.startTick >= 0)
			objects.push_back({ { "type", "feverStart" }, { "beat", tickToBeat(score.fever.startTick) } });

		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap)
				continue;

			ordered_json single;
			single["type"] = "single";
			single["beat"] = tickToBeat(note.tick);
			single["critical"] = note.critical;
			single["lane"] = toUscLane(note.lane, note.width);
			single["size"] = toUscSize(note.width);
			single["timeScaleGroup"] = note.layer;
			single["speedRatio"] = note.speedRatio;
			single["trace"] = note.friction;
			if (note.isFlick())
				single["direction"] = directionString(note.flick);
			objects.push_back(single);
		}

		for (const auto& [id, hold] : score.holdNotes)
		{
			const Note& start = score.notes.at(hold.start.ID);
			const Note& end = score.notes.at(hold.end);

			if (hold.isGuide())
			{
				ordered_json midpoints = ordered_json::array();
				auto addPoint = [&](const Note& note, EaseType ease)
				{
					ordered_json point = pointBase(note, note.layer);
					point["ease"] = USC_EASES[static_cast<int>(ease)];
					point["speedRatio"] = start.speedRatio;
					midpoints.push_back(point);
				};

				addPoint(start, hold.start.ease);
				for (const auto& step : hold.steps)
					addPoint(score.notes.at(step.ID), step.ease);
				addPoint(end, EaseType::Linear);

				objects.push_back({ { "type", "guide" }, { "color", guideColors[static_cast<int>(hold.guideColor)] },
					{ "fade", hold.fadeType == FadeType::In ? "in" : hold.fadeType == FadeType::None ? "none" : "out" }, { "midpoints", midpoints } });
				continue;
			}

			ordered_json connections = ordered_json::array();

			ordered_json startPoint = pointBase(start, start.layer);
			startPoint["type"] = "start";
			startPoint["critical"] = start.critical;
			startPoint["ease"] = USC_EASES[static_cast<int>(hold.start.ease)];
			startPoint["judgeType"] = hold.startType == HoldNoteType::Hidden ? "none" : start.friction ? "trace" : "normal";
			startPoint["speedRatio"] = start.speedRatio;
			connections.push_back(startPoint);

			for (const auto& step : hold.steps)
			{
				const Note& mid = score.notes.at(step.ID);
				ordered_json point = pointBase(mid, mid.layer);
				point["type"] = step.type == HoldStepType::Skip ? "attach" : "tick";
				point["ease"] = USC_EASES[static_cast<int>(step.ease)];
				if (step.type != HoldStepType::Hidden)
					point["critical"] = start.critical;
				point["speedRatio"] = start.speedRatio;
				connections.push_back(point);
			}

			ordered_json endPoint = pointBase(end, end.layer);
			endPoint["type"] = "end";
			endPoint["critical"] = end.critical;
			endPoint["judgeType"] = hold.endType == HoldNoteType::Hidden ? "none" : end.friction ? "trace" : "normal";
			endPoint["speedRatio"] = start.speedRatio;
			if (end.isFlick() && hold.endType == HoldNoteType::Normal)
				endPoint["direction"] = directionString(end.flick);
			connections.push_back(endPoint);

			objects.push_back({ { "type", "slide" }, { "critical", start.critical }, { "connections", connections } });
		}

		if (!score.seVolumeChanges.empty())
			warnings.push_back(IO::formatString("%d SE volume change(s) were dropped: USC has no SE volume.", static_cast<int>(score.seVolumeChanges.size())));

		ordered_json root;
		root["usc"] = { { "objects", objects }, { "offset", score.metadata.musicOffset == 0 ? 0.0 : -score.metadata.musicOffset / 1000.0 } };
		root["version"] = 2;
		return json::parse(root.dump());
	}

	Score UscSerializer::fromJson(const json& root)
	{
		warnings.clear();
		if (!root.is_object() || !root.contains("usc") || !root["usc"].is_object())
			throw std::runtime_error("Not a USC file.");

		const json& usc = root["usc"];
		Score score;
		score.tempoChanges.clear();
		score.layers.clear();
		score.metadata.musicOffset = static_cast<float>(-number(usc, "offset", 0.0) * 1000.0);

		LaneReader lanes;
		int damageNotes = 0, fakeNotes = 0, unknown = 0;

		const json objects = usc.contains("objects") && usc["objects"].is_array() ? usc["objects"] : json::array();

		// layers first so notes can refer to them by index
		for (const auto& obj : objects)
		{
			if (string(obj, "type") != "timeScaleGroup")
				continue;

			const int layer = static_cast<int>(score.layers.size());
			score.layers.push_back({ layer == 0 ? "default" : "Layer " + std::to_string(layer) });
			if (obj.contains("changes") && obj["changes"].is_array())
				for (const auto& change : obj["changes"])
					score.hiSpeedChanges.push_back({ beatToTick(number(change, "beat")), static_cast<float>(number(change, "timeScale", 1.0)), layer });
		}
		if (score.layers.empty())
			score.layers.push_back({ "default" });

		const int layerCount = static_cast<int>(score.layers.size());
		auto layerOf = [layerCount](const json& point)
		{
			const auto it = point.find("timeScaleGroup");
			const int layer = it != point.end() && it->is_number_integer() ? it->get<int>() : 0;
			return std::clamp(layer, 0, layerCount - 1);
		};

		for (const auto& obj : objects)
		{
			const std::string type = string(obj, "type");
			if (type == "bpm")
			{
				score.tempoChanges.push_back({ beatToTick(number(obj, "beat")), static_cast<float>(number(obj, "bpm", 120.0)) });
			}
			else if (type == "timeScaleGroup")
			{
				continue;
			}
			else if (type == "skill")
			{
				score.skills.push_back({ nextSkillID++, beatToTick(number(obj, "beat")) });
			}
			else if (type == "feverChance")
			{
				score.fever.chanceTick = beatToTick(number(obj, "beat"));
			}
			else if (type == "feverStart")
			{
				score.fever.startTick = beatToTick(number(obj, "beat"));
			}
			else if (type == "damage")
			{
				++damageNotes;
			}
			else if (type == "single")
			{
				if (boolean(obj, "fake"))
				{
					++fakeNotes;
					continue;
				}

				Note note(NoteType::Tap);
				note.ID = nextID++;
				note.tick = beatToTick(number(obj, "beat"));
				lanes.apply(note, obj);
				note.critical = boolean(obj, "critical");
				note.friction = boolean(obj, "trace");
				note.flick = flickFromDirection(obj);
				note.layer = layerOf(obj);
				note.speedRatio = static_cast<float>(number(obj, "speedRatio", 1.0));
				score.notes[note.ID] = note;
			}
			else if (type == "slide")
			{
				if (boolean(obj, "fake"))
				{
					++fakeNotes;
					continue;
				}

				std::vector<json> points;
				if (obj.contains("connections") && obj["connections"].is_array())
					points.assign(obj["connections"].begin(), obj["connections"].end());
				std::stable_sort(points.begin(), points.end(), [](const json& a, const json& b) { return number(a, "beat") < number(b, "beat"); });

				auto startIt = std::find_if(points.begin(), points.end(), [](const json& p) { return string(p, "type") == "start"; });
				auto endIt = std::find_if(points.begin(), points.end(), [](const json& p) { return string(p, "type") == "end"; });
				if (startIt == points.end() || endIt == points.end() || points.size() < 2)
				{
					++unknown;
					continue;
				}

				HoldNote hold;
				Note start(NoteType::Hold);
				start.ID = nextID++;
				start.tick = beatToTick(number(*startIt, "beat"));
				lanes.apply(start, *startIt);
				start.critical = boolean(*startIt, "critical", boolean(obj, "critical"));
				start.layer = layerOf(*startIt);
				start.speedRatio = static_cast<float>(number(*startIt, "speedRatio", 1.0));
				const std::string startJudge = string(*startIt, "judgeType", "normal");
				start.friction = startJudge == "trace";
				hold.startType = startJudge == "none" ? HoldNoteType::Hidden : HoldNoteType::Normal;
				hold.start = { start.ID, HoldStepType::Normal, easeFromString(string(*startIt, "ease", "linear")) };
				score.notes[start.ID] = start;

				for (const auto& point : points)
				{
					const std::string pointType = string(point, "type");
					if (pointType != "tick" && pointType != "attach")
						continue;

					Note mid(NoteType::HoldMid);
					mid.ID = nextID++;
					mid.parentID = start.ID;
					mid.tick = beatToTick(number(point, "beat"));
					lanes.apply(mid, point);
					mid.critical = start.critical;
					mid.layer = layerOf(point);
					mid.speedRatio = start.speedRatio;

					HoldStepType stepType = HoldStepType::Normal;
					const auto critical = point.find("critical");
					const bool hidden = critical == point.end() || critical->is_null();
					if (pointType == "attach")
						stepType = HoldStepType::Skip;
					else if (hidden)
						stepType = HoldStepType::Hidden;

					score.notes[mid.ID] = mid;
					hold.steps.push_back({ mid.ID, stepType, easeFromString(string(point, "ease", "linear")) });
				}

				Note end(NoteType::HoldEnd);
				end.ID = nextID++;
				end.parentID = start.ID;
				end.tick = beatToTick(number(*endIt, "beat"));
				lanes.apply(end, *endIt);
				end.layer = layerOf(*endIt);
				end.speedRatio = start.speedRatio;
				const std::string endJudge = string(*endIt, "judgeType", "normal");
				hold.endType = endJudge == "none" ? HoldNoteType::Hidden : HoldNoteType::Normal;
				end.friction = endJudge == "trace";
				end.flick = hold.endType == HoldNoteType::Normal ? flickFromDirection(*endIt) : FlickType::None;
				end.critical = start.critical || boolean(*endIt, "critical");
				hold.end = end.ID;
				score.notes[end.ID] = end;

				sortHoldSteps(score, hold);
				score.holdNotes[start.ID] = hold;
			}
			else if (type == "guide")
			{
				std::vector<json> points;
				if (obj.contains("midpoints") && obj["midpoints"].is_array())
					points.assign(obj["midpoints"].begin(), obj["midpoints"].end());
				if (points.size() < 2)
				{
					++unknown;
					continue;
				}
				std::stable_sort(points.begin(), points.end(), [](const json& a, const json& b) { return number(a, "beat") < number(b, "beat"); });

				HoldNote hold;
				hold.startType = hold.endType = HoldNoteType::Guide;
				const std::string color = string(obj, "color", "green");
				for (int c = 0; c < static_cast<int>(GuideColor::GuideColorCount); ++c)
					if (color == guideColors[c])
						hold.guideColor = static_cast<GuideColor>(c);
				const std::string fade = string(obj, "fade", "out");
				hold.fadeType = fade == "in" ? FadeType::In : fade == "none" ? FadeType::None : FadeType::Out;

				int startId = -1;
				for (size_t i = 0; i < points.size(); ++i)
				{
					const bool first = i == 0, last = i + 1 == points.size();
					Note note(first ? NoteType::Hold : last ? NoteType::HoldEnd : NoteType::HoldMid);
					note.ID = nextID++;
					note.parentID = first ? -1 : startId;
					note.tick = beatToTick(number(points[i], "beat"));
					lanes.apply(note, points[i]);
					note.layer = layerOf(points[i]);
					note.speedRatio = static_cast<float>(number(points[0], "speedRatio", 1.0));
					score.notes[note.ID] = note;

					const EaseType ease = easeFromString(string(points[i], "ease", "linear"));
					if (first)
					{
						startId = note.ID;
						hold.start = { note.ID, HoldStepType::Normal, ease };
					}
					else if (last)
					{
						hold.end = note.ID;
					}
					else
					{
						hold.steps.push_back({ note.ID, HoldStepType::Hidden, ease });
					}
				}

				syncGuideCritical(score, hold);
				score.holdNotes[startId] = hold;
			}
			else
			{
				++unknown;
			}
		}

		if (score.tempoChanges.empty())
			score.tempoChanges.push_back({ 0, 160.0f });

		if (lanes.adjusted)
			warnings.push_back(IO::formatString("%d point(s) used fractional or extended lanes and were snapped into the 12 lanes.", lanes.adjusted));
		if (damageNotes)
			warnings.push_back(IO::formatString("%d damage note(s) were dropped (not supported by Project SEKAI).", damageNotes));
		if (fakeNotes)
			warnings.push_back(IO::formatString("%d fake note(s) were dropped.", fakeNotes));
		if (unknown)
			warnings.push_back(IO::formatString("%d USC object(s) were not understood and skipped.", unknown));

		Canvas::normalizeEvents(score);
		return score;
	}

	void UscSerializer::serialize(const Score& score, std::string filename)
	{
		const json root = toJson(score);
		IO::File file(filename, IO::FileMode::Write);
		file.write(root.dump());
		file.flush();
		file.close();
	}

	Score UscSerializer::deserialize(std::string filename)
	{
		IO::File file(filename, IO::FileMode::ReadBinary);
		std::vector<uint8_t> bytes = file.readAllBytes();
		file.close();
		if (IO::isGzipCompressed(bytes))
			bytes = IO::inflateGzip(bytes);

		const json root = json::parse(bytes.begin(), bytes.end(), nullptr, false);
		if (root.is_discarded())
			throw std::runtime_error("The USC file is not valid JSON.");
		return fromJson(root);
	}
}
