#include "Themes.h"
#include "../Core/ExportTargets.h"
#include "../../Application.h"
#include "../../ImGui/imgui.h"
#include <json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Canvas::Themes
{
	namespace
	{
		struct FloatSetting
		{
			const char* key;
			float ImGuiStyle::* member;
		};

		const FloatSetting floatSettings[] = {
			{ "window_rounding", &ImGuiStyle::WindowRounding },
			{ "child_rounding", &ImGuiStyle::ChildRounding },
			{ "frame_rounding", &ImGuiStyle::FrameRounding },
			{ "popup_rounding", &ImGuiStyle::PopupRounding },
			{ "scrollbar_rounding", &ImGuiStyle::ScrollbarRounding },
			{ "grab_rounding", &ImGuiStyle::GrabRounding },
			{ "tab_rounding", &ImGuiStyle::TabRounding },
			{ "window_border", &ImGuiStyle::WindowBorderSize },
			{ "frame_border", &ImGuiStyle::FrameBorderSize },
			{ "popup_border", &ImGuiStyle::PopupBorderSize },
		};

		fs::path pathOf(const std::string& name)
		{
			return fs::u8path(folder()) / fs::u8path(sanitizeFileName(name) + ".json");
		}
	}

	std::string folder()
	{
		return MikuMikuWorld::Application::getUserDataDir() + "themes";
	}

	std::vector<std::string> list()
	{
		std::vector<std::string> names;
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(fs::u8path(folder()), error))
			if (entry.is_regular_file() && entry.path().extension() == ".json")
				names.push_back(entry.path().stem().u8string());
		std::sort(names.begin(), names.end());
		return names;
	}

	bool apply(const std::string& name, ImGuiStyle& style)
	{
		std::ifstream in(pathOf(name));
		if (!in)
			return false;
		const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
		if (!json.is_object())
			return false;

		if (json.contains("colors") && json["colors"].is_object())
		{
			for (int i = 0; i < ImGuiCol_COUNT; ++i)
			{
				const char* colorName = ImGui::GetStyleColorName(i);
				if (!json["colors"].contains(colorName))
					continue;
				const auto& value = json["colors"][colorName];
				if (value.is_array() && value.size() == 4)
					style.Colors[i] = ImVec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
			}
		}

		for (const auto& setting : floatSettings)
			if (json.contains(setting.key) && json[setting.key].is_number())
				style.*setting.member = json[setting.key].get<float>();
		return true;
	}

	bool save(const std::string& name, const ImGuiStyle& style)
	{
		if (name.empty())
			return false;

		nlohmann::ordered_json json;
		json["format"] = "canvas-editor/theme";
		json["name"] = name;
		for (const auto& setting : floatSettings)
			json[setting.key] = style.*setting.member;
		for (int i = 0; i < ImGuiCol_COUNT; ++i)
		{
			const ImVec4& c = style.Colors[i];
			json["colors"][ImGui::GetStyleColorName(i)] = { c.x, c.y, c.z, c.w };
		}

		std::error_code error;
		fs::create_directories(fs::u8path(folder()), error);
		std::ofstream out(pathOf(name));
		out << json.dump(2);
		return static_cast<bool>(out);
	}

	bool remove(const std::string& name)
	{
		std::error_code error;
		return fs::remove(pathOf(name), error);
	}

	void applyDensity(ImGuiStyle& style, int density)
	{
		if (density == 1)
			return;
		const float factor = density <= 0 ? 0.7f : 1.35f;
		style.FramePadding.x *= factor;
		style.FramePadding.y *= factor;
		style.ItemSpacing.x *= factor;
		style.ItemSpacing.y *= factor;
		style.ItemInnerSpacing.x *= factor;
		style.ItemInnerSpacing.y *= factor;
		style.CellPadding.x *= factor;
		style.CellPadding.y *= factor;
		style.WindowPadding.x *= factor;
		style.WindowPadding.y *= factor;
	}
}
