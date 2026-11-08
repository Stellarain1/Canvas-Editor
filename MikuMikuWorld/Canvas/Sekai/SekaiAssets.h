#pragma once
#include <string>
#include <vector>

// SEKAI Mode's look comes from lezzthanthree/SEKAI-Stories (GPL-3.0). Its code is ported into
// Canvas, but its images, sounds and fonts are SEGA / Colorful Palette / Crypton property and
// are NOT shipped with Canvas. Point Settings > Canvas > SEKAI-Stories folder at a local copy
// (the repository root or its "public" folder, e.g. from tools/sekai-assets/fetch_sekai_assets.py)
// and SEKAI Mode picks them up. Without it, Canvas uses its own stand-ins.
namespace Canvas::Sekai
{
	class Assets
	{
	public:
		// Re-resolves when the configured folder changes. Cheap to call every frame.
		void refresh(const std::string& configuredDir);

		bool available() const { return !publicDir.empty(); }
		// a SEKAI-Stories folder the user set (the bundled pack has no story backgrounds)
		bool isStoriesFolder() const { return !publicDir.empty() && !bundled; }
		const std::string& root() const { return publicDir; }

		// Full path of public/sound/<name>.wav (or .mp3); empty when missing.
		std::string sound(const std::string& name) const;
		// Full path of public/img/<file>; empty when missing.
		std::string image(const std::string& file) const;
		// Full path of public/fonts/<file>; empty when missing.
		std::string font(const std::string& file) const;
		// Background images (public/background_compressed, else background_low_jpg), sorted.
		const std::vector<std::string>& backgrounds();

	private:
		std::string configured;
		std::string publicDir;
		std::vector<std::string> backgroundCache;
		bool backgroundsScanned{ false };
		bool bundled{ false };	// res/sekai, not a SEKAI-Stories folder
	};

	Assets& assets();
}
