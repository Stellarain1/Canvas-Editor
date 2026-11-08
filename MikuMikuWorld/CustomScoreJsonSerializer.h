#pragma once

#include "ScoreSerializer.h"

namespace MikuMikuWorld
{
	// Project SEKAI MusicScoreData (the in-game Chart Maker's JSON). Reads plain, gzip and
	// base64(gzip) files; skills and fever travel in the "CanvasSkillFever" key.
	class CustomScoreJsonSerializer : public ScoreSerializer
	{
	public:
		explicit CustomScoreJsonSerializer(bool base64Gzip = false) : base64Gzip{ base64Gzip } {}

		void serialize(const Score& score, std::string filename) override;
		Score deserialize(std::string filename) override;
		Score parseBytes(std::vector<uint8_t> bytes);

	private:
		bool base64Gzip{ false };
	};
}
