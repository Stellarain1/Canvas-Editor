#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Writes a .zip: files from disk (streamed, so a 300 MB MV is fine) or bytes from memory, deflated
// or stored. Names are UTF-8 and '/' separated. Archives up to 4 GB (no zip64): plenty for charts.
//
//   Canvas::ZipWriter zip;
//   if (!zip.open("out.zip")) ... zip.error()
//   zip.addFile("track.mp3", "C:/song/music.mp3", false);	// already compressed: stored
//   zip.addBytes("ChartInfo.json", bytes);
//   zip.close();
namespace Canvas
{
	class ZipWriter
	{
	public:
		ZipWriter() = default;
		~ZipWriter();
		ZipWriter(const ZipWriter&) = delete;
		ZipWriter& operator=(const ZipWriter&) = delete;

		bool open(const std::string& path);
		// `compress` false stores the file as it is (mp3 / mp4 / png gain nothing from deflate)
		bool addFile(const std::string& name, const std::string& sourcePath, bool compress = true);
		bool addBytes(const std::string& name, const std::vector<uint8_t>& bytes, bool compress = true);
		bool addText(const std::string& name, const std::string& text, bool compress = true);
		// writes the central directory; false when anything failed along the way
		bool close();
		const std::string& error() const { return message; }

	private:
		struct Entry
		{
			std::string name;
			uint32_t crc{}, compressedSize{}, size{}, offset{};
			uint16_t method{};
		};

		// reads `source` chunk by chunk (from a FILE* or a buffer) into a local header + data
		bool addEntry(const std::string& name, FILE* source, const uint8_t* bytes, size_t length, bool compress);
		bool write(const void* data, size_t size);
		uint32_t position() const;

		FILE* file{};
		std::vector<Entry> entries;
		std::string message;
		bool failed{};
	};
}
