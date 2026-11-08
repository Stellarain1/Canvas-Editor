#pragma once
#include "../../ScoreSerializer.h"
#include <json.hpp>

namespace MikuMikuWorld
{
	// USC ("Universal Sekai Chart"), the JSON format of MikuMikuWorld4CC / Chart Cyanvas.
	// Port of sonolus-level-converters' usc loader/exporter (GPL-3.0), adapted to the MMW
	// note model:  lane_usc = lane_mmw - 6 + width / 2,  size = width / 2,  beat = tick / 480.
	class UscSerializer : public ScoreSerializer
	{
	public:
		void serialize(const Score& score, std::string filename) override;
		Score deserialize(std::string filename) override;

		nlohmann::json toJson(const Score& score);
		Score fromJson(const nlohmann::json& usc);
	};
}
