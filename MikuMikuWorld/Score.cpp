#include "Score.h"
#include <algorithm>

namespace MikuMikuWorld
{
	Score::Score()
	{
		metadata.title = "";
		metadata.author = "";
		metadata.artist = "";
		metadata.musicOffset = 0;
		metadata.musicId = 0;

		tempoChanges.push_back(Tempo());
		timeSignatures[0] = { 0, 4, 4 };

		fever.chanceTick = fever.startTick = -1;
	}

	std::vector<HiSpeedChange> hiSpeedsOfLayer(const Score& score, int layer)
	{
		std::vector<HiSpeedChange> result;
		for (const auto& change : score.hiSpeedChanges)
			if (change.layer == layer)
				result.push_back(change);

		std::stable_sort(result.begin(), result.end(), [](const HiSpeedChange& a, const HiSpeedChange& b) { return a.tick < b.tick; });
		return result;
	}

	std::vector<std::vector<HiSpeedChange>> splitHiSpeedLayers(const Score& score)
	{
		std::vector<std::vector<HiSpeedChange>> result(std::max<size_t>(1, score.layers.size()));
		for (const auto& change : score.hiSpeedChanges)
		{
			const size_t layer = static_cast<size_t>(std::max(0, change.layer));
			if (layer >= result.size())
				result.resize(layer + 1);
			result[layer].push_back(change);
		}

		for (auto& layer : result)
			std::stable_sort(layer.begin(), layer.end(), [](const HiSpeedChange& a, const HiSpeedChange& b) { return a.tick < b.tick; });
		return result;
	}

	void syncGuideCritical(Score& score, HoldNote& hold)
	{
		if (!hold.isGuide())
			return;

		const bool critical = isYellowFamilyGuide(hold.guideColor);
		auto setCritical = [&](int id)
		{
			auto it = score.notes.find(id);
			if (it != score.notes.end())
				it->second.critical = critical;
		};

		setCritical(hold.start.ID);
		setCritical(hold.end);
		for (const auto& step : hold.steps)
			setCritical(step.ID);
	}
}
