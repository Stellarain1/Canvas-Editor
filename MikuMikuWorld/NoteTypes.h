#pragma once
#include <stdint.h>

namespace MikuMikuWorld
{
	enum class NoteType : uint8_t
	{
		Tap,
		Hold,
		HoldMid,
		HoldEnd,
	};

	enum class FlickType : uint8_t
	{
		None,
		Default,
		Left,
		Right,
		FlickTypeCount
	};

	constexpr const char* flickTypes[]
	{
		"none",
		"default",
		"left",
		"right"
	};

	enum class HoldStepType : uint8_t
	{
		Normal,
		Hidden,
		Skip,
		HoldStepTypeCount
	};

	constexpr const char* stepTypes[]
	{
		"normal",
		"hidden",
		"skip"
	};

	// Values match MikuMikuWorld4CC / CCMMWS so files round-trip without remapping.
	enum class EaseType : uint8_t
	{
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut,
		EaseOutIn,
		EaseTypeCount
	};

	constexpr const char* easeTypes[]
	{
		"linear",
		"ease_in",
		"ease_out",
		"ease_in_out",
		"ease_out_in"
	};

	// Project SEKAI itself (.sus / MusicScoreData) only knows in and out.
	constexpr bool isExtendedEase(EaseType ease) { return ease == EaseType::EaseInOut || ease == EaseType::EaseOutIn; }

	enum class HoldNoteType : uint8_t
	{
		Normal,
		Hidden,
		Guide
	};

	constexpr const char* holdTypes[]
	{
		"normal",
		"hidden",
		"guide"
	};

	// Guide colours. Values match MikuMikuWorld4CC (CCMMWS), USC and Chart Cyanvas LevelData.
	enum class GuideColor : uint8_t
	{
		Neutral,
		Red,
		Green,
		Blue,
		Yellow,
		Purple,
		Cyan,
		Black,
		GuideColorCount
	};

	constexpr const char* guideColors[]
	{
		"neutral",
		"red",
		"green",
		"blue",
		"yellow",
		"purple",
		"cyan",
		"black"
	};

	// Localisation keys for the guide colours (menus, tooltips).
	constexpr const char* guideColorNames[]
	{
		"guide_neutral",
		"guide_red",
		"guide_green",
		"guide_blue",
		"guide_yellow",
		"guide_purple",
		"guide_cyan",
		"guide_black"
	};

	// Project SEKAI only has green and yellow (critical) guides. This is the same
	// mapping sonolus-level-converters uses when it has to drop the extra colours.
	constexpr bool isYellowFamilyGuide(GuideColor color)
	{
		return color == GuideColor::Yellow || color == GuideColor::Red || color == GuideColor::Purple;
	}

	constexpr bool isExtendedGuideColor(GuideColor color)
	{
		return color != GuideColor::Green && color != GuideColor::Yellow;
	}

	// Values match MikuMikuWorld4CC (CCMMWS).
	enum class FadeType : uint8_t
	{
		Out,
		None,
		In,
		FadeTypeCount
	};

	constexpr const char* fadeTypes[]
	{
		"fade_out",
		"fade_none",
		"fade_in"
	};
}