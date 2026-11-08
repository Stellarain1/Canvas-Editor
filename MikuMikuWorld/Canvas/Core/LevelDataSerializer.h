#pragma once
#include "../../ScoreSerializer.h"
#include <json.hpp>

namespace MikuMikuWorld
{
	// Sonolus LevelData for the Project SEKAI engines.
	//
	//  - Next Sekai / UntitledCharts ("pysekai" archetypes: #TIMESCALE_GROUP, *HeadTapNote, AnchorNote, ...)
	//    Export is a port of sonolus-level-converters' next_sekai exporter (by the engine's author);
	//    import is the inverse (after skill-fever-format/pysekai_loader.py).
	//  - Chart Cyanvas (SlideConnector / Guide archetypes): import only, the inverse of the
	//    library's chart_cyanvas exporter.
	class LevelDataSerializer : public ScoreSerializer
	{
	public:
		enum class Variant
		{
			Unknown,
			NextSekai,
			ChartCyanvas
		};

		explicit LevelDataSerializer(bool gzipOutput = true) : gzipOutput{ gzipOutput } {}

		void serialize(const Score& score, std::string filename) override;
		Score deserialize(std::string filename) override;

		nlohmann::json exportNextSekai(const Score& score);
		Score importLevelData(const nlohmann::json& levelData);

		static Variant detect(const nlohmann::json& levelData);

	private:
		bool gzipOutput{ true };

		Score importNextSekai(const nlohmann::json& levelData);
		Score importChartCyanvas(const nlohmann::json& levelData);
	};
}
