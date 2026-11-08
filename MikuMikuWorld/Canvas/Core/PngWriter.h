#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Tiny PNG encoder on top of zlib (already linked for the formats). Used for the --screenshot
// option, the icon picker and the score UI skin exporter.
namespace Canvas
{
	// Writes 8-bit RGBA pixels, rows top to bottom. `flipRows` takes OpenGL's bottom-up rows.
	bool writePng(const std::string& path, int width, int height, const uint8_t* rgba, bool flipRows = false);

	// The same file, in memory.
	std::vector<uint8_t> encodePng(int width, int height, const uint8_t* rgba, bool flipRows = false);
}
