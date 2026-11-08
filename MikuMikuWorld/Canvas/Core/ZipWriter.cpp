#include "ZipWriter.h"
#include "../../IO.h"
#include <zlib.h>
#include <cstring>
#include <ctime>

namespace Canvas
{
	namespace
	{
		constexpr size_t CHUNK = 256 * 1024;

		void put16(std::vector<uint8_t>& out, uint16_t v)
		{
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
		}

		void put32(std::vector<uint8_t>& out, uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
				out.push_back(static_cast<uint8_t>(v >> (i * 8)));
		}

		// MS-DOS time and date (what every zip tool shows as "modified")
		void dosNow(uint16_t& time, uint16_t& date)
		{
			const std::time_t now = std::time(nullptr);
			std::tm local{};
#ifdef _WIN32
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			time = static_cast<uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
			date = static_cast<uint16_t>(((local.tm_year - 80) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
		}

		FILE* openFile(const std::string& path, const char* mode)
		{
#ifdef _WIN32
			return _wfopen(IO::mbToWideStr(path).c_str(), IO::mbToWideStr(mode).c_str());
#else
			return std::fopen(path.c_str(), mode);
#endif
		}
	}

	ZipWriter::~ZipWriter()
	{
		if (file)
			std::fclose(file);
	}

	bool ZipWriter::open(const std::string& path)
	{
		file = openFile(path, "wb");
		if (!file)
		{
			message = "Could not write " + path;
			return false;
		}
		return true;
	}

	uint32_t ZipWriter::position() const
	{
		return file ? static_cast<uint32_t>(std::ftell(file)) : 0;
	}

	bool ZipWriter::write(const void* data, size_t size)
	{
		if (failed || !file)
			return false;
		if (size && std::fwrite(data, 1, size, file) != size)
		{
			failed = true;
			message = "Writing the zip failed (disk full?)";
			return false;
		}
		return true;
	}

	bool ZipWriter::addEntry(const std::string& name, FILE* source, const uint8_t* bytes, size_t length, bool compress)
	{
		if (failed || !file)
			return false;

		Entry entry;
		entry.name = name;
		entry.method = compress ? 8 : 0;
		entry.offset = position();
		uint16_t dosTime = 0, dosDate = 0;
		dosNow(dosTime, dosDate);

		// local header with sizes 0: they are patched in once the data is written
		std::vector<uint8_t> header;
		put32(header, 0x04034b50);
		put16(header, 20);
		put16(header, 0x0800);		// UTF-8 names
		put16(header, entry.method);
		put16(header, dosTime);
		put16(header, dosDate);
		put32(header, 0);			// crc
		put32(header, 0);			// compressed size
		put32(header, 0);			// size
		put16(header, static_cast<uint16_t>(name.size()));
		put16(header, 0);
		header.insert(header.end(), name.begin(), name.end());
		if (!write(header.data(), header.size()))
			return false;

		z_stream z{};
		if (compress && deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		{
			failed = true;
			message = "zlib could not start";
			return false;
		}
		std::vector<uint8_t> in(CHUNK), out(CHUNK);
		uLong crc = crc32(0L, Z_NULL, 0);
		uint64_t size = 0, written = 0;
		size_t fed = 0;
		bool more = true;
		while (more)
		{
			size_t n = 0;
			if (source)
				n = std::fread(in.data(), 1, in.size(), source);
			else
			{
				n = std::min(CHUNK, length - fed);
				std::memcpy(in.data(), bytes + fed, n);
				fed += n;
			}
			more = n == CHUNK;
			crc = crc32(crc, in.data(), static_cast<uInt>(n));
			size += n;
			if (!compress)
			{
				if (!write(in.data(), n))
					return false;
				written += n;
				continue;
			}
			z.next_in = in.data();
			z.avail_in = static_cast<uInt>(n);
			const int flush = more ? Z_NO_FLUSH : Z_FINISH;
			do
			{
				z.next_out = out.data();
				z.avail_out = static_cast<uInt>(out.size());
				deflate(&z, flush);
				const size_t produced = out.size() - z.avail_out;
				if (!write(out.data(), produced))
				{
					deflateEnd(&z);
					return false;
				}
				written += produced;
			} while (z.avail_out == 0);
		}
		if (compress)
			deflateEnd(&z);
		if (size > 0xFFFFFFFFull || written > 0xFFFFFFFFull)
		{
			failed = true;
			message = name + " is over 4 GB";
			return false;
		}

		entry.crc = static_cast<uint32_t>(crc);
		entry.size = static_cast<uint32_t>(size);
		entry.compressedSize = static_cast<uint32_t>(written);
		// patch crc and sizes into the local header
		const long end = std::ftell(file);
		std::fseek(file, static_cast<long>(entry.offset) + 14, SEEK_SET);
		std::vector<uint8_t> sizes;
		put32(sizes, entry.crc);
		put32(sizes, entry.compressedSize);
		put32(sizes, entry.size);
		write(sizes.data(), sizes.size());
		std::fseek(file, end, SEEK_SET);
		entries.push_back(entry);
		return !failed;
	}

	bool ZipWriter::addFile(const std::string& name, const std::string& sourcePath, bool compress)
	{
		FILE* source = openFile(sourcePath, "rb");
		if (!source)
		{
			failed = true;
			message = "Could not read " + sourcePath;
			return false;
		}
		const bool ok = addEntry(name, source, nullptr, 0, compress);
		std::fclose(source);
		return ok;
	}

	bool ZipWriter::addBytes(const std::string& name, const std::vector<uint8_t>& bytes, bool compress)
	{
		return addEntry(name, nullptr, bytes.data(), bytes.size(), compress);
	}

	bool ZipWriter::addText(const std::string& name, const std::string& text, bool compress)
	{
		return addEntry(name, nullptr, reinterpret_cast<const uint8_t*>(text.data()), text.size(), compress);
	}

	bool ZipWriter::close()
	{
		if (!file)
			return false;
		const uint32_t directoryStart = position();
		uint16_t dosTime = 0, dosDate = 0;
		dosNow(dosTime, dosDate);
		for (const Entry& e : entries)
		{
			std::vector<uint8_t> d;
			put32(d, 0x02014b50);
			put16(d, 20);		// made by
			put16(d, 20);		// needed
			put16(d, 0x0800);
			put16(d, e.method);
			put16(d, dosTime);
			put16(d, dosDate);
			put32(d, e.crc);
			put32(d, e.compressedSize);
			put32(d, e.size);
			put16(d, static_cast<uint16_t>(e.name.size()));
			put16(d, 0);
			put16(d, 0);
			put16(d, 0);
			put16(d, 0);
			put32(d, 0);
			put32(d, e.offset);
			d.insert(d.end(), e.name.begin(), e.name.end());
			write(d.data(), d.size());
		}
		const uint32_t directorySize = position() - directoryStart;
		std::vector<uint8_t> end;
		put32(end, 0x06054b50);
		put16(end, 0);
		put16(end, 0);
		put16(end, static_cast<uint16_t>(entries.size()));
		put16(end, static_cast<uint16_t>(entries.size()));
		put32(end, directorySize);
		put32(end, directoryStart);
		put16(end, 0);
		write(end.data(), end.size());
		const bool ok = !failed && std::fclose(file) == 0;
		file = nullptr;
		return ok;
	}
}
