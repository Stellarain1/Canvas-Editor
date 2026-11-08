#pragma once
#include <stdint.h>

namespace MikuMikuWorld
{
	enum class TimelineMode : uint8_t
	{
		Select,
		InsertTap,
		InsertLong,
		InsertLongMid,
		InsertFlick,
		MakeCritical,
		MakeFriction,
		InsertGuide,
		InsertBPM,
		InsertTimeSign,
		InsertHiSpeed,
		InsertSEVolume,
		TimelineModeMax
	};

	constexpr const char* timelineModes[]
	{
		"select",
		"tap",
		"hold",
		"hold_step",
		"flick",
		"critical",
		"trace",
		"guide",
		"bpm",
		"time_signature",
		"hi_speed",
		"se_volume"
	};

	constexpr int divisions[]
	{
		4, 8, 12, 16, 20, 24, 32, 48, 64, 96, 128, 192, 240, 384, 480, 768, 960, 1920
	};

	constexpr int timeSignatureDenominators[]
	{
		2, 4, 8, 16, 32, 64
	};

	enum class EventType : uint8_t
	{
		None,
		Bpm,
		TimeSignature,
		HiSpeed,
		SEVolume,
		Skill,
		Fever,
		EventTypeMax
	};

	constexpr const char* eventTypes[]
	{
		"none",
		"bpm",
		"time_signature",
		"hi_speed",
		"se_volume",
		"skill",
		"fever",
		"event"
	};

	enum class Direction : uint8_t
	{
		Down,
		Up,
		DirectionMax
	};
}
