#pragma once

#include "ScoreSerializer.h"
#include "BinaryReader.h"
#include "BinaryWriter.h"

namespace MikuMikuWorld
{
	// .mmws family.
	//
	// Reads:  MMWS    - MikuMikuWorld and External-Unofficial-ChartMaker (versions 1-7)
	//         CCMMWS  - MikuMikuWorld4CC (Chart Cyanvas), cyanvas versions 1-6
	//         UCMMWS  - UntitledCharts' MMW (CCMMWS v6 layout + fake/dummy flags)
	//
	// Writes: CanvasMmws        - MMWS version 4, the layout the original MikuMikuWorld reads, plus a
	//                             trailing "CVEX" block holding what that format has no room for (hi-speed
	//                             layers, guide colours/fades, in-out eases, note speeds, SE volume, music
	//                             id). Every MikuMikuWorld version and fork reads the file normally and
	//                             simply never looks at the block; Canvas gets everything back from it.
	//         CanvasMmwsExtended - MMWS version 7 exactly as External-Unofficial-ChartMaker writes it (note
	//                             speed, SE volume and music id in the file itself) plus the same block.
	//                             The original MikuMikuWorld cannot open these; some forks need them.
	//         MikuMikuWorld4CC  - CCMMWS v4/6 for MikuMikuWorld4CC users (no per-note speed or SE volume)
	class NativeScoreSerializer : public ScoreSerializer
	{
	public:
		enum class Flavor
		{
			CanvasMmws,
			MikuMikuWorld4CC,
			CanvasMmwsExtended
		};

		static const int SCORE_VERSION;			// newest layout this build reads (7)
		static const int COMPATIBLE_VERSION;	// what plain Save writes (4)
		// Settings > Canvas > Export: plain Save writes version 7 instead of 4
		static bool saveExtendedByDefault;

		explicit NativeScoreSerializer(Flavor flavor = Flavor::CanvasMmws) : flavor{ flavor } {}

		void serialize(const Score& score, std::string filename) override;
		Score deserialize(std::string filename) override;

	private:
		Flavor flavor;

		void serializeCanvas(const Score& score, const std::string& filename, bool extended);
		void serializeCyanvas(const Score& score, const std::string& filename);
	};
}
