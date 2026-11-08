#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Reads files out of a .zip (stored or deflated entries, zip64 included, archives of any size).
// Enough for a folder of charts zipped by Windows' "Send to > Compressed folder", 7-Zip, etc.
namespace Canvas
{
	class ZipReader
	{
	public:
		struct Entry
		{
			std::string name;			// path inside the archive, '/' separated
			uint64_t compressedSize{}, size{}, localHeader{};
			uint16_t method{};
			bool directory() const { return !name.empty() && name.back() == '/'; }
		};

		ZipReader() = default;
		~ZipReader();
		ZipReader(const ZipReader&) = delete;
		ZipReader& operator=(const ZipReader&) = delete;

		// Opens the archive and reads its directory; false (with error()) when it is not a zip.
		bool open(const std::string& path);
		const std::vector<Entry>& entries() const { return list; }
		// The entry's bytes; empty with error() set when it cannot be read.
		std::vector<uint8_t> read(const Entry& entry);
		const std::string& error() const { return message; }

	private:
		FILE* file{};
		uint64_t fileSize{};
		std::vector<Entry> list;
		std::string message;

		bool seek(uint64_t offset);
		bool readBytes(void* out, size_t count);
	};
}
