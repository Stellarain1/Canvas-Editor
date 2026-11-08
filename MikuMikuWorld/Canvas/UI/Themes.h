#pragma once
#include <string>
#include <vector>

struct ImGuiStyle;

// Custom themes: every ImGui colour plus rounding / borders, saved as JSON files in
// <user data>/themes/<name>.json. A custom theme is applied on top of the base theme (and
// SEKAI Mode), so a theme file only needs the colours it changes.
namespace Canvas::Themes
{
	std::string folder();
	std::vector<std::string> list();

	// Applies the named theme to `style`. Returns false when it does not exist or cannot be read.
	bool apply(const std::string& name, ImGuiStyle& style);
	// Saves every colour and the rounding / border settings of `style` under `name`.
	bool save(const std::string& name, const ImGuiStyle& style);
	bool remove(const std::string& name);

	// 0 compact, 1 normal, 2 roomy: scales paddings and spacing.
	void applyDensity(ImGuiStyle& style, int density);
}
