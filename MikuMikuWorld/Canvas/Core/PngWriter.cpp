#include "PngWriter.h"
#include <zlib.h>
#include <filesystem>
#include <fstream>

namespace Canvas
{
	namespace
	{
		void put32(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v >> 24));
			out.push_back(static_cast<uint8_t>(v >> 16));
			out.push_back(static_cast<uint8_t>(v >> 8));
			out.push_back(static_cast<uint8_t>(v));
		}

		void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
		{
			put32(out, static_cast<uint32_t>(data.size()));
			const size_t start = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			const uLong crc = crc32(0L, out.data() + start, static_cast<uInt>(out.size() - start));
			put32(out, static_cast<uint32_t>(crc));
		}
	}

	std::vector<uint8_t> encodePng(int width, int height, const uint8_t* rgba, bool flipRows)
	{
		std::vector<uint8_t> out;
		if (width <= 0 || height <= 0 || !rgba)
			return out;

		// every row gets filter byte 0 (none); zlib does the rest
		const size_t stride = static_cast<size_t>(width) * 4;
		std::vector<uint8_t> raw((stride + 1) * height);
		for (int y = 0; y < height; ++y)
		{
			const int src = flipRows ? height - 1 - y : y;
			raw[y * (stride + 1)] = 0;
			std::copy(rgba + src * stride, rgba + (src + 1) * stride, raw.begin() + y * (stride + 1) + 1);
		}

		uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
		std::vector<uint8_t> packed(packedSize);
		if (compress2(packed.data(), &packedSize, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK)
			return {};
		packed.resize(packedSize);

		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
		out.insert(out.end(), signature, signature + 8);

		std::vector<uint8_t> header;
		put32(header, static_cast<uint32_t>(width));
		put32(header, static_cast<uint32_t>(height));
		header.push_back(8);	// bits per channel
		header.push_back(6);	// RGBA
		header.push_back(0);
		header.push_back(0);
		header.push_back(0);
		chunk(out, "IHDR", header);
		chunk(out, "IDAT", packed);
		chunk(out, "IEND", {});
		return out;
	}

	bool writePng(const std::string& path, int width, int height, const uint8_t* rgba, bool flipRows)
	{
		const std::vector<uint8_t> bytes = encodePng(width, height, rgba, flipRows);
		if (bytes.empty())
			return false;
		std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return static_cast<bool>(file);
	}
}
