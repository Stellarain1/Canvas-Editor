#include "SkillFever.h"
#include "../../Constants.h"
#include "../../Tempo.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <limits>

using namespace MikuMikuWorld;

namespace Canvas::SkillFever
{
	namespace
	{
		// Python's round(): nearest, ties to even
		long long pyRound(double value)
		{
			const int previous = std::fegetround();
			std::fesetround(FE_TONEAREST);
			const long long result = std::llrint(value);
			std::fesetround(previous);
			return result;
		}

		struct MeasureGrid
		{
			std::vector<int> starts; // tick of each measure start, strictly increasing

			MeasureGrid(const Score& score, int lastTick)
			{
				int measure = 0;
				int tick = 0;
				while (true)
				{
					starts.push_back(tick);
					if (tick > lastTick + TICKS_PER_BEAT * 64)
						break;

					const int tsMeasure = findTimeSignature(measure, score.timeSignatures);
					const auto tsIt = score.timeSignatures.find(tsMeasure);
					const float beats = tsIt == score.timeSignatures.end() ? 4.0f : beatsPerMeasure(tsIt->second);
					tick += std::max(1, static_cast<int>(std::lround(beats * TICKS_PER_BEAT)));
					++measure;
				}
			}

			// measure start nearest to tick (ties go to the even measure index, like round())
			int nearest(int tick) const
			{
				auto it = std::upper_bound(starts.begin(), starts.end(), tick);
				if (it == starts.begin())
					return starts.front();
				const size_t index = static_cast<size_t>(std::distance(starts.begin(), it)) - 1;
				if (index + 1 >= starts.size())
					return starts[index];

				const int before = starts[index], after = starts[index + 1];
				const int distBefore = tick - before, distAfter = after - tick;
				if (distBefore < distAfter)
					return before;
				if (distAfter < distBefore)
					return after;
				return (index % 2 == 0) ? before : after;
			}

			int lengthAt(int measureStart) const
			{
				auto it = std::upper_bound(starts.begin(), starts.end(), measureStart);
				if (it == starts.end() || it == starts.begin())
					return TICKS_PER_BEAT * 4;
				return *it - *(it - 1);
			}
		};
	}

	bool hasMarkers(const Score& score)
	{
		return !score.skills.empty() || score.fever.chanceTick >= 0 || score.fever.startTick >= 0;
	}

	Markers readMarkers(const Score& score)
	{
		Markers markers;
		for (const auto& skill : score.skills)
			markers.skills.push_back(skill.tick);
		std::sort(markers.skills.begin(), markers.skills.end());
		markers.feverChance = score.fever.chanceTick;
		markers.feverStart = score.fever.startTick;
		return markers;
	}

	void applyMarkers(Score& score, const Markers& markers)
	{
		score.skills.clear();
		std::vector<int> ticks = markers.skills;
		std::sort(ticks.begin(), ticks.end());
		for (int tick : ticks)
			score.skills.push_back({ nextSkillID++, tick });
		score.fever.chanceTick = markers.feverChance;
		score.fever.startTick = markers.feverStart;
	}

	Markers generateOfficialStyle(const Score& score)
	{
		Markers markers;
		if (score.notes.empty())
			return markers;

		int firstTick = std::numeric_limits<int>::max();
		int lastTick = std::numeric_limits<int>::min();
		for (const auto& [id, note] : score.notes)
		{
			firstTick = std::min(firstTick, note.tick);
			lastTick = std::max(lastTick, note.tick);
		}

		const int span = lastTick - firstTick;
		if (span <= 0)
			return markers;

		const MeasureGrid grid(score, lastTick);
		auto at = [&](double fraction) { return grid.nearest(firstTick + static_cast<int>(pyRound(span * fraction))); };

		for (double fraction : DEFAULT_SKILLS)
		{
			int tick = at(fraction);
			while (std::find(markers.skills.begin(), markers.skills.end(), tick) != markers.skills.end())
				tick += TICKS_PER_BEAT; // very short chart: two skills in one measure
			markers.skills.push_back(tick);
		}
		std::sort(markers.skills.begin(), markers.skills.end());

		markers.feverChance = at(DEFAULT_FEVER_CHANCE);
		markers.feverStart = at(DEFAULT_FEVER_START);
		if (markers.feverStart <= markers.feverChance)
			markers.feverStart = markers.feverChance + grid.lengthAt(markers.feverChance);

		return markers;
	}

	nlohmann::json toJson(const Markers& markers)
	{
		if (markers.empty())
			return nullptr;

		std::vector<int> skills = markers.skills;
		std::sort(skills.begin(), skills.end());

		nlohmann::ordered_json value;
		value["skills"] = skills;
		value["feverChance"] = markers.feverChance >= 0 ? nlohmann::ordered_json(markers.feverChance) : nlohmann::ordered_json(nullptr);
		value["feverStart"] = markers.feverStart >= 0 ? nlohmann::ordered_json(markers.feverStart) : nlohmann::ordered_json(nullptr);
		return nlohmann::json::parse(value.dump());
	}

	Markers fromJson(const nlohmann::json& value)
	{
		Markers markers;
		if (!value.is_object())
			return markers;

		auto toTick = [](const nlohmann::json& v, int fallback) -> int
		{
			if (v.is_number_integer())
				return v.get<int>();
			if (v.is_number())
				return static_cast<int>(std::lround(v.get<double>()));
			if (v.is_string())
			{
				try { return std::stoi(v.get<std::string>()); }
				catch (...) { return fallback; }
			}
			return fallback;
		};

		const auto skills = value.find("skills");
		if (skills != value.end() && skills->is_array())
		{
			for (const auto& tick : *skills)
			{
				const int t = toTick(tick, -1);
				if (t >= 0)
					markers.skills.push_back(t);
			}
		}
		std::sort(markers.skills.begin(), markers.skills.end());

		const auto chance = value.find("feverChance");
		if (chance != value.end() && !chance->is_null())
			markers.feverChance = toTick(*chance, -1);

		const auto start = value.find("feverStart");
		if (start != value.end() && !start->is_null())
			markers.feverStart = toTick(*start, -1);

		return markers;
	}

	std::vector<std::string> ensureSong(std::map<std::string, Score*>& charts)
	{
		std::vector<std::string> missing;
		for (const auto& [difficulty, score] : charts)
			if (score && !hasMarkers(*score))
				missing.push_back(difficulty);

		if (missing.empty())
			return {};

		std::vector<std::string> order;
		for (const auto& [difficulty, score] : charts)
			order.push_back(difficulty);

		const auto& ranking = hardestFirst();
		auto rank = [&ranking](const std::string& difficulty)
		{
			auto it = std::find(ranking.begin(), ranking.end(), difficulty);
			return it == ranking.end() ? 99 : static_cast<int>(it - ranking.begin());
		};
		std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });

		Markers markers;
		auto donor = std::find_if(order.begin(), order.end(), [&](const std::string& d) { return charts[d] && hasMarkers(*charts[d]); });
		if (donor != order.end())
			markers = readMarkers(*charts[*donor]);
		else
			markers = generateOfficialStyle(*charts[order.front()]);

		if (markers.empty())
			return {};

		for (const auto& difficulty : missing)
			applyMarkers(*charts[difficulty], markers);
		return missing;
	}
}
