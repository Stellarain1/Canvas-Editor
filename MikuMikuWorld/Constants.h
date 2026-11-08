#pragma once

namespace MikuMikuWorld
{
	constexpr int TICKS_PER_BEAT = 480;

	constexpr float MEASURE_WIDTH	= 30.0f;
	constexpr int MIN_LANE_WIDTH	= 24;
	constexpr int MAX_LANE_WIDTH	= 72;
	constexpr int MIN_NOTES_HEIGHT	= 24;
	constexpr int MAX_NOTES_HEIGHT	= 42;

	constexpr int MIN_TIME_SIGNATURE				= 1;
	constexpr int MAX_TIME_SIGNATURE_NUMERATOR		= 32;
	constexpr int MAX_TIME_SIGNATURE_DENOMINATOR	= 64;
	constexpr float MIN_BPM							= 10;
	constexpr float MAX_BPM							= 10000;

	constexpr const char* SUS_EXTENSION			= ".sus";
	constexpr const char* MMWS_EXTENSION		= ".mmws";
	// Canvas Editor's own charts: the same layout as .mmws (MikuMikuWorld opens them renamed)
	constexpr const char* CE_EXTENSION			= ".ce";
	constexpr const char* JSON_EXTENSION		= ".json";
	constexpr const char* GZ_JSON_EXTENSION		= ".json.gz";
}