#include "ChartJson.h"
#include "../../Tempo.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

using namespace MikuMikuWorld;

namespace Canvas::ChartJson
{
	namespace
	{
		constexpr int TPB = 480;

		const char* flickName(FlickType flick)
		{
			switch (flick)
			{
			case FlickType::Default: return "up";
			case FlickType::Left: return "left";
			case FlickType::Right: return "right";
			default: return "none";
			}
		}

		FlickType flickFrom(const nlohmann::json& value)
		{
			if (value.is_boolean())
				return value.get<bool>() ? FlickType::Default : FlickType::None;
			const std::string name = value.is_string() ? value.get<std::string>() : "none";
			if (name == "up" || name == "default")
				return FlickType::Default;
			if (name == "left")
				return FlickType::Left;
			if (name == "right")
				return FlickType::Right;
			if (name == "none" || name.empty())
				return FlickType::None;
			throw std::runtime_error("unknown flick \"" + name + "\" (none, up, left, right)");
		}

		EaseType easeFrom(const nlohmann::json& value)
		{
			const std::string name = value.is_string() ? value.get<std::string>() : "linear";
			for (int i = 0; i < static_cast<int>(EaseType::EaseTypeCount); ++i)
				if (name == easeTypes[i])
					return static_cast<EaseType>(i);
			if (name == "in")
				return EaseType::EaseIn;
			if (name == "out")
				return EaseType::EaseOut;
			throw std::runtime_error("unknown ease \"" + name + "\" (linear, ease_in, ease_out, ease_in_out, ease_out_in)");
		}

		HoldStepType visibilityFrom(const nlohmann::json& value)
		{
			const std::string name = value.is_string() ? value.get<std::string>() : "visible";
			if (name == "visible" || name == "normal")
				return HoldStepType::Normal;
			if (name == "hidden")
				return HoldStepType::Hidden;
			if (name == "skip")
				return HoldStepType::Skip;
			throw std::runtime_error("unknown step visibility \"" + name + "\" (visible, hidden, skip)");
		}

		GuideColor colorFrom(const nlohmann::json& value)
		{
			const std::string name = value.is_string() ? value.get<std::string>() : "green";
			for (int i = 0; i < static_cast<int>(GuideColor::GuideColorCount); ++i)
				if (name == guideColors[i])
					return static_cast<GuideColor>(i);
			throw std::runtime_error("unknown guide colour \"" + name + "\"");
		}

		void checkLanes(int lane, int width)
		{
			if (width < MIN_NOTE_WIDTH || width > MAX_NOTE_WIDTH)
				throw std::runtime_error("width " + std::to_string(width) + " is outside 1-12");
			if (lane < MIN_LANE || lane + width > NUM_LANES)
				throw std::runtime_error("lane " + std::to_string(lane) + " with width " + std::to_string(width) + " does not fit lanes 0-11");
		}

		nlohmann::ordered_json position(const Score& score, int tick)
		{
			const int measure = accumulateMeasures(tick, TPB, score.timeSignatures);
			const int measureTick = measureToTicks(measure, TPB, score.timeSignatures);
			const double time = accumulateDuration(tick, TPB, score.tempoChanges);
			nlohmann::ordered_json j;
			j["tick"] = tick;
			j["measure"] = measure;
			j["beat"] = std::round((tick - measureTick) / double(TPB) * 1000.0) / 1000.0;
			j["time"] = std::round(time * 1000.0) / 1000.0;
			j["audioTime"] = std::round((time - score.metadata.musicOffset / 1000.0) * 1000.0) / 1000.0;
			return j;
		}

		int holdIdOf(const Score& score, int id)
		{
			auto it = score.notes.find(id);
			if (it == score.notes.end())
				return -1;
			if (it->second.getType() == NoteType::Tap)
				return -1;
			return it->second.getType() == NoteType::Hold ? id : it->second.parentID;
		}

		void removeHold(Score& score, int holdId)
		{
			auto hold = score.holdNotes.find(holdId);
			if (hold == score.holdNotes.end())
				return;
			score.notes.erase(hold->second.start.ID);
			for (const auto& step : hold->second.steps)
				score.notes.erase(step.ID);
			score.notes.erase(hold->second.end);
			score.holdNotes.erase(hold);
		}

		void removeAny(Score& score, int id)
		{
			auto it = score.notes.find(id);
			if (it == score.notes.end())
				throw std::runtime_error("no note with id " + std::to_string(id));
			if (it->second.getType() == NoteType::Tap)
			{
				score.notes.erase(it);
				return;
			}
			if (it->second.getType() == NoteType::HoldMid)
			{
				// only the step
				auto hold = score.holdNotes.find(it->second.parentID);
				if (hold != score.holdNotes.end())
				{
					auto& steps = hold->second.steps;
					steps.erase(std::remove_if(steps.begin(), steps.end(), [&](const HoldStep& s) { return s.ID == id; }), steps.end());
				}
				score.notes.erase(it);
				return;
			}
			removeHold(score, holdIdOf(score, id));
		}

		std::vector<int> holdNoteIds(const HoldNote& hold)
		{
			std::vector<int> ids{ hold.start.ID };
			for (const auto& step : hold.steps)
				ids.push_back(step.ID);
			ids.push_back(hold.end);
			return ids;
		}

		void mirrorNote(Note& note)
		{
			note.lane = NUM_LANES - note.lane - note.width;
			if (note.flick == FlickType::Left)
				note.flick = FlickType::Right;
			else if (note.flick == FlickType::Right)
				note.flick = FlickType::Left;
		}
	}

	int tickOf(const Score& score, const nlohmann::json& p)
	{
		if (p.contains("tick") && p["tick"].is_number())
			return static_cast<int>(std::lround(p["tick"].get<double>()));
		if (p.contains("measure") && p["measure"].is_number())
		{
			const int measure = p["measure"].get<int>();
			const double beat = p.contains("beat") && p["beat"].is_number() ? p["beat"].get<double>() : 0.0;
			if (measure < 0)
				throw std::runtime_error("measure must be 0 or more");
			return measureToTicks(measure, TPB, score.timeSignatures) + static_cast<int>(std::lround(beat * TPB));
		}
		if (p.contains("time") && p["time"].is_number())
			return accumulateTicks(p["time"].get<float>(), TPB, score.tempoChanges);
		if (p.contains("audioTime") && p["audioTime"].is_number())
			return accumulateTicks(static_cast<float>(p["audioTime"].get<double>() + score.metadata.musicOffset / 1000.0), TPB, score.tempoChanges);
		throw std::runtime_error("a position needs \"tick\", \"measure\" (+ \"beat\"), \"time\" or \"audioTime\"");
	}

	nlohmann::ordered_json toJson(const Score& score, int fromTick, int toTick)
	{
		auto inRange = [&](int tick) { return (fromTick < 0 || tick >= fromTick) && (toTick < 0 || tick <= toTick); };
		nlohmann::ordered_json j;
		j["title"] = score.metadata.title;
		j["artist"] = score.metadata.artist;
		j["designer"] = score.metadata.author;
		j["ticksPerBeat"] = TPB;
		j["offsetMs"] = score.metadata.musicOffset;
		j["tempo"] = nlohmann::ordered_json::array();
		for (const auto& t : score.tempoChanges)
			j["tempo"].push_back({ { "tick", t.tick }, { "bpm", t.bpm } });
		j["timeSignatures"] = nlohmann::ordered_json::array();
		for (const auto& [measure, ts] : score.timeSignatures)
			j["timeSignatures"].push_back({ { "measure", measure }, { "numerator", ts.numerator }, { "denominator", ts.denominator } });

		std::vector<const Note*> taps;
		for (const auto& [id, note] : score.notes)
			if (note.getType() == NoteType::Tap && inRange(note.tick))
				taps.push_back(&note);
		std::sort(taps.begin(), taps.end(), [](const Note* a, const Note* b) { return a->tick != b->tick ? a->tick < b->tick : a->lane < b->lane; });
		j["notes"] = nlohmann::ordered_json::array();
		for (const Note* note : taps)
		{
			nlohmann::ordered_json n;
			n["id"] = note->ID;
			n["type"] = note->friction ? "trace" : "tap";
			const auto where = position(score, note->tick);
			for (auto& [key, value] : where.items())
				n[key] = value;
			n["lane"] = note->lane;
			n["width"] = note->width;
			n["critical"] = note->critical;
			n["flick"] = flickName(note->flick);
			j["notes"].push_back(n);
		}

		std::vector<const HoldNote*> holds;
		for (const auto& [id, hold] : score.holdNotes)
		{
			auto start = score.notes.find(hold.start.ID);
			auto end = score.notes.find(hold.end);
			if (start == score.notes.end() || end == score.notes.end())
				continue;
			const bool overlaps = (toTick < 0 || start->second.tick <= toTick) && (fromTick < 0 || end->second.tick >= fromTick);
			if (overlaps)
				holds.push_back(&hold);
		}
		std::sort(holds.begin(), holds.end(), [&](const HoldNote* a, const HoldNote* b) {
			return score.notes.at(a->start.ID).tick < score.notes.at(b->start.ID).tick;
		});
		j["holds"] = nlohmann::ordered_json::array();
		for (const HoldNote* hold : holds)
		{
			const Note& start = score.notes.at(hold->start.ID);
			nlohmann::ordered_json h;
			h["id"] = hold->start.ID;
			h["kind"] = hold->isGuide() ? "guide" : "hold";
			h["critical"] = start.critical;
			if (hold->isGuide())
				h["guideColor"] = guideColors[static_cast<int>(hold->guideColor)];
			else
			{
				h["startVisible"] = hold->startType == HoldNoteType::Normal;
				h["endVisible"] = hold->endType == HoldNoteType::Normal;
			}
			h["points"] = nlohmann::ordered_json::array();
			auto point = [&](const Note& note, const char* role) {
				nlohmann::ordered_json p;
				p["id"] = note.ID;
				p["role"] = role;
				const auto where = position(score, note.tick);
				for (auto& [key, value] : where.items())
					p[key] = value;
				p["lane"] = note.lane;
				p["width"] = note.width;
				return p;
			};
			auto first = point(start, "start");
			first["ease"] = easeTypes[static_cast<int>(hold->start.ease)];
			h["points"].push_back(first);
			for (const auto& step : hold->steps)
			{
				auto it = score.notes.find(step.ID);
				if (it == score.notes.end())
					continue;
				auto p = point(it->second, "step");
				p["visibility"] = step.type == HoldStepType::Normal ? "visible" : step.type == HoldStepType::Hidden ? "hidden" : "skip";
				p["ease"] = easeTypes[static_cast<int>(step.ease)];
				h["points"].push_back(p);
			}
			const Note& end = score.notes.at(hold->end);
			auto last = point(end, "end");
			last["flick"] = flickName(end.flick);
			h["points"].push_back(last);
			j["holds"].push_back(h);
		}

		j["skills"] = nlohmann::ordered_json::array();
		for (const auto& skill : score.skills)
			j["skills"].push_back(skill.tick);
		j["fever"] = { { "chance", score.fever.chanceTick }, { "start", score.fever.startTick } };
		return j;
	}

	EditResult applyEdits(Score& score, const nlohmann::json& operations)
	{
		EditResult result;
		if (!operations.is_array())
		{
			result.errors.push_back("operations must be an array");
			return result;
		}
		for (size_t index = 0; index < operations.size(); ++index)
		{
			const auto& op = operations[index];
			try
			{
				if (!op.is_object() || !op.contains("op"))
					throw std::runtime_error("each operation needs an \"op\"");
				const std::string kind = op["op"].get<std::string>();

				if (kind == "add_tap")
				{
					const int tick = tickOf(score, op);
					const int lane = op.at("lane").get<int>();
					const int width = op.value("width", 3);
					checkLanes(lane, width);
					if (tick < 0)
						throw std::runtime_error("tick must be 0 or more");
					Note note(NoteType::Tap, tick, lane, width);
					note.ID = nextID++;
					note.critical = op.value("critical", false);
					note.friction = op.value("trace", false);
					note.flick = op.contains("flick") ? flickFrom(op["flick"]) : FlickType::None;
					score.notes[note.ID] = note;
					result.createdIds.push_back(note.ID);
				}
				else if (kind == "add_hold")
				{
					const auto& points = op.at("points");
					if (!points.is_array() || points.size() < 2)
						throw std::runtime_error("a hold needs at least two points");
					const bool guide = op.value("guide", false);
					const bool critical = op.value("critical", false);
					int previousTick = -1;
					std::vector<Note> notes;
					std::vector<std::pair<HoldStepType, EaseType>> kinds;
					for (size_t i = 0; i < points.size(); ++i)
					{
						const auto& p = points[i];
						const int tick = tickOf(score, p);
						const int lane = p.at("lane").get<int>();
						const int width = p.value("width", 3);
						checkLanes(lane, width);
						if (tick <= previousTick)
							throw std::runtime_error("hold points must go forward in time");
						previousTick = tick;
						const NoteType type = i == 0 ? NoteType::Hold : i + 1 == points.size() ? NoteType::HoldEnd : NoteType::HoldMid;
						Note note(type, tick, lane, width);
						note.critical = critical;
						notes.push_back(note);
						kinds.push_back({ p.contains("visibility") ? visibilityFrom(p["visibility"]) : HoldStepType::Normal,
							p.contains("ease") ? easeFrom(p["ease"]) : EaseType::Linear });
					}
					if (notes.front().tick < 0)
						throw std::runtime_error("tick must be 0 or more");
					HoldNote hold;
					const int headId = nextID++;
					for (size_t i = 0; i < notes.size(); ++i)
					{
						Note& note = notes[i];
						note.ID = i == 0 ? headId : nextID++;
						note.parentID = i == 0 ? -1 : headId;
						if (i + 1 == notes.size() && op.contains("endFlick"))
							note.flick = flickFrom(op["endFlick"]);
						score.notes[note.ID] = note;
						if (i == 0)
							hold.start = { note.ID, HoldStepType::Normal, kinds[i].second };
						else if (i + 1 == notes.size())
							hold.end = note.ID;
						else
							hold.steps.push_back({ note.ID, kinds[i].first, kinds[i].second });
					}
					if (guide)
					{
						hold.startType = hold.endType = HoldNoteType::Guide;
						hold.guideColor = op.contains("guideColor") ? colorFrom(op["guideColor"]) : GuideColor::Green;
						hold.fadeType = FadeType::Out;
					}
					else
					{
						hold.startType = op.value("startVisible", true) ? HoldNoteType::Normal : HoldNoteType::Hidden;
						hold.endType = op.value("endVisible", true) ? HoldNoteType::Normal : HoldNoteType::Hidden;
					}
					score.holdNotes[headId] = hold;
					if (guide)
						syncGuideCritical(score, score.holdNotes[headId]);
					result.createdIds.push_back(headId);
				}
				else if (kind == "delete")
				{
					for (const auto& id : op.at("ids"))
						removeAny(score, id.get<int>());
				}
				else if (kind == "delete_range")
				{
					const int from = tickOf(score, op.at("from"));
					const int to = tickOf(score, op.at("to"));
					int firstLane = 0, lastLane = NUM_LANES - 1;
					if (op.contains("lanes") && op["lanes"].is_array() && op["lanes"].size() == 2)
					{
						firstLane = op["lanes"][0].get<int>();
						lastLane = op["lanes"][1].get<int>();
					}
					std::vector<int> taps, holds;
					for (const auto& [id, note] : score.notes)
					{
						if (note.tick < from || note.tick > to || note.lane > lastLane || note.lane + note.width - 1 < firstLane)
							continue;
						if (note.getType() == NoteType::Tap)
							taps.push_back(id);
						else if (note.getType() == NoteType::Hold)
							holds.push_back(id);
					}
					for (int id : taps)
						score.notes.erase(id);
					for (int id : holds)
						removeHold(score, id);
				}
				else if (kind == "move")
				{
					const int id = op.at("id").get<int>();
					auto it = score.notes.find(id);
					if (it == score.notes.end())
						throw std::runtime_error("no note with id " + std::to_string(id));
					const bool hasPosition = op.contains("tick") || op.contains("measure") || op.contains("time") || op.contains("audioTime");
					const int newTick = hasPosition ? tickOf(score, op) : it->second.tick;
					const int holdId = holdIdOf(score, id);
					if (holdId < 0)
					{
						Note& note = it->second;
						const int lane = op.value("lane", note.lane), width = op.value("width", note.width);
						checkLanes(lane, width);
						note.tick = newTick;
						note.lane = lane;
						note.width = width;
					}
					else
					{
						HoldNote& hold = score.holdNotes.at(holdId);
						const Note& head = score.notes.at(hold.start.ID);
						const int dt = newTick - head.tick;
						const int dl = op.contains("lane") ? op["lane"].get<int>() - head.lane : 0;
						for (int nid : holdNoteIds(hold))
						{
							const Note& n = score.notes.at(nid);
							checkLanes(n.lane + dl, op.value("width", n.width));
							if (n.tick + dt < 0)
								throw std::runtime_error("the hold would start before the chart");
						}
						for (int nid : holdNoteIds(hold))
						{
							Note& n = score.notes.at(nid);
							n.tick += dt;
							n.lane += dl;
							n.width = op.value("width", n.width);
						}
					}
				}
				else if (kind == "set")
				{
					const int id = op.at("id").get<int>();
					auto it = score.notes.find(id);
					if (it == score.notes.end())
						throw std::runtime_error("no note with id " + std::to_string(id));
					const int holdId = holdIdOf(score, id);
					if (op.contains("critical"))
					{
						const bool critical = op["critical"].get<bool>();
						if (holdId < 0)
							it->second.critical = critical;
						else
							for (int nid : holdNoteIds(score.holdNotes.at(holdId)))
								score.notes.at(nid).critical = critical;
					}
					if (op.contains("flick"))
					{
						if (holdId >= 0)
							score.notes.at(score.holdNotes.at(holdId).end).flick = flickFrom(op["flick"]);
						else
							it->second.flick = flickFrom(op["flick"]);
					}
					if (op.contains("trace"))
					{
						if (holdId >= 0)
							throw std::runtime_error("only taps can be traces");
						it->second.friction = op["trace"].get<bool>();
					}
				}
				else if (kind == "mirror" || kind == "shift")
				{
					const int from = tickOf(score, op.at("from"));
					const int to = tickOf(score, op.at("to"));
					const int byTicks = op.value("byTicks", 0);
					const int byLanes = op.value("byLanes", 0);
					std::vector<int> ids;
					for (const auto& [id, note] : score.notes)
						if (note.tick >= from && note.tick <= to && note.getType() == NoteType::Tap)
							ids.push_back(id);
					std::vector<int> holdIds;
					for (const auto& [id, hold] : score.holdNotes)
					{
						const int tick = score.notes.at(hold.start.ID).tick;
						if (tick >= from && tick <= to)
							holdIds.push_back(id);
					}
					for (int hid : holdIds)
						for (int nid : holdNoteIds(score.holdNotes.at(hid)))
							ids.push_back(nid);
					if (kind == "shift")
						for (int nid : ids)
						{
							const Note& n = score.notes.at(nid);
							checkLanes(n.lane + byLanes, n.width);
							if (n.tick + byTicks < 0)
								throw std::runtime_error("shift would move notes before the chart");
						}
					for (int nid : ids)
					{
						Note& n = score.notes.at(nid);
						if (kind == "mirror")
							mirrorNote(n);
						else
						{
							n.tick += byTicks;
							n.lane += byLanes;
						}
					}
				}
				else if (kind == "set_bpm")
				{
					const int tick = tickOf(score, op);
					const double bpm = op.at("bpm").get<double>();
					if (!(bpm > 0 && bpm < 10000))
						throw std::runtime_error("bpm must be above 0");
					auto& tempos = score.tempoChanges;
					auto it = std::find_if(tempos.begin(), tempos.end(), [&](const Tempo& t) { return t.tick == tick; });
					if (it != tempos.end())
						it->bpm = static_cast<float>(bpm);
					else
						tempos.push_back(Tempo(tick, static_cast<float>(bpm)));
					std::sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
				}
				else if (kind == "set_offset")
				{
					score.metadata.musicOffset = static_cast<float>(op.at("ms").get<double>());
				}
				else
				{
					throw std::runtime_error("unknown op \"" + kind + "\"");
				}
				result.applied++;
			}
			catch (const std::exception& ex)
			{
				result.errors.push_back("operation " + std::to_string(index) + ": " + ex.what());
			}
		}
		// keep hold steps in time order after moves
		for (auto& [id, hold] : score.holdNotes)
			sortHold(score, hold);
		return result;
	}
}
