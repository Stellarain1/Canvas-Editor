#pragma once
#include "MathUtils.h"

namespace MikuMikuWorld
{
	// the non-const ones follow the interface style (Canvas Neo tints them with the accent)
	inline ImU32 measureColor	= ImGui::ColorConvertFloat4ToU32(ImVec4(0.80f, 0.80f, 0.80f, 0.65f));
	const ImU32 measureTxtColor	= ImGui::ColorConvertFloat4ToU32(ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
	inline ImU32 divColor1		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.65f, 0.65f, 0.65f));
	inline ImU32 divColor2		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.65f, 0.65f, 0.30f));
	inline ImU32 cursorColor		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.87f, 0.23f, 0.23f, 1.00f));
	const ImU32 tempoColor		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.52f, 0.87f, 0.35f, 1.00f));
	const ImU32 timeColor		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.85f, 0.71f, 0.05f, 1.00f));
	const ImU32 speedColor		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.84f, 0.25f, 0.16f, 1.00f));
	const ImU32 seVolumeColor	= ImGui::ColorConvertFloat4ToU32(ImVec4(0.34f, 0.78f, 0.86f, 1.00f));
	const ImU32 skillColor		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.58f, 1.00f, 1.00f));
	const ImU32 feverColor		= ImGui::ColorConvertFloat4ToU32(ImVec4(0.90f, 0.54f, 0.28f, 1.00f));
	const ImU32 selectionColor1 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.40f, 0.40f, 0.40f, 0.45f));
	inline ImU32 selectionColor2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.46f, 1.00f, 0.50f));
	const ImU32 selectionShadow = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.20f, 0.20f, 0.65f));
	const ImU32 warningColor	= ImGui::ColorConvertFloat4ToU32(ImVec4(0.96f, 0.26f, 0.21f, 0.50f));
	const ImU32 bgFallbackColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.10f, 0.10f, 1.00f));

	// Swatch shown in menus / outlines for each GuideColor (same order as the enum)
	static ImVec4 guideColorSwatch(GuideColor color)
	{
		static const ImVec4 swatches[] =
		{
			{ 0.89f, 0.89f, 0.94f, 1.0f },	// neutral
			{ 1.00f, 0.32f, 0.43f, 1.0f },	// red
			{ 0.24f, 0.91f, 0.59f, 1.0f },	// green
			{ 0.31f, 0.63f, 1.00f, 1.0f },	// blue
			{ 0.97f, 0.75f, 0.19f, 1.0f },	// yellow
			{ 0.73f, 0.43f, 1.00f, 1.0f },	// purple
			{ 0.27f, 0.90f, 0.94f, 1.0f },	// cyan
			{ 0.19f, 0.19f, 0.24f, 1.0f },	// black
		};
		const int index = static_cast<int>(color);
		return index >= 0 && index < static_cast<int>(GuideColor::GuideColorCount) ? swatches[index] : swatches[2];
	}

	const Color noteTint{ 1.0f, 1.0f, 1.0f, 1.0f };
	const Color otherLayerTint{ 1.0f, 1.0f, 1.0f, 0.30f };
	const Color hoverTint{ 1.0f, 1.0f, 1.0f, 0.70f };

	static ImVec4 generateDarkColor(const ImVec4& color)
	{
		return ImVec4(
			std::max(0.0f, color.x - 0.10f),
			std::max(0.0f, color.y - 0.10f),
			std::max(0.0f, color.z - 0.10f),
			1.0f);
	}

	static ImVec4 generateHighlightColor(const ImVec4& color)
	{
		return ImVec4(
			std::min(1.00f, color.x + 0.10f),
			std::min(1.00f, color.y + 0.10f),
			std::min(1.00f, color.z + 0.10f),
			1.0f);
	}
}
