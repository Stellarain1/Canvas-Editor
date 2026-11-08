#include "ZipReader.h"
#include "../../IO.h"
#include <zlib.h>
#include <algorithm>
#include <cstring>

namespace Canvas
{
	namespace
	{
		uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
		uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
		uint64_t u64(const uint8_t* p) { return u32(p) | (static_cast<uint64_t>(u32(p + 4)) << 32); }
	}

	ZipReader::~ZipReader()
	{
		if (file)
			std::fclose(file);
	}

	bool ZipReader::seek(uint64_t offset)
	{
#ifdef _WIN32
		return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
		return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
	}

	bool ZipReader::readBytes(void* out, size_t count)
	{
		return std::fread(out, 1, count, file) == count;
	}

	bool ZipReader::open(const std::string& path)
	{
		list.clear();
#ifdef _WIN32
		file = _wfopen(IO::mbToWideStr(path).c_str(), L"rb");
#else
		file = std::fopen(path.c_str(), "rb");
#endif
		if (!file)
		{
			message = "cannot open " + path;
			return false;
		}
#ifdef _WIN32
		_fseeki64(file, 0, SEEK_END);
		fileSize = static_cast<uint64_t>(_ftelli64(file));
#else
		fseeko(file, 0, SEEK_END);
		fileSize = static_cast<uint64_t>(ftello(file));
#endif
		// the end-of-central-directory record is in the last 64 KiB + 22 bytes
		const uint64_t tail = std::min<uint64_t>(fileSize, 65557);
		std::vector<uint8_t> buffer(static_cast<size_t>(tail));
		if (!seek(fileSize - tail) || !readBytes(buffer.data(), buffer.size()))
		{
			message = "cannot read the end of the archive";
			return false;
		}
		long eocd = -1;
		for (long i = static_cast<long>(buffer.size()) - 22; i >= 0; --i)
			if (u32(&buffer[i]) == 0x06054b50)
			{
				eocd = i;
				break;
			}
		if (eocd < 0)
		{
			message = "not a zip archive";
			return false;
		}
		uint64_t entryCount = u16(&buffer[eocd + 10]);
		uint64_t directorySize = u32(&buffer[eocd + 12]);
		uint64_t directoryOffset = u32(&buffer[eocd + 16]);

		// zip64: a locator right before the record points at the zip64 record
		if (eocd >= 20 && u32(&buffer[eocd - 20]) == 0x07064b50)
		{
			const uint64_t zip64Record = u64(&buffer[eocd - 20 + 8]);
			uint8_t record[56];
			if (seek(zip64Record) && readBytes(record, sizeof(record)) && u32(record) == 0x06064b50)
			{
				entryCount = u64(record + 32);
				directorySize = u64(record + 40);
				directoryOffset = u64(record + 48);
			}
		}

		std::vector<uint8_t> directory(static_cast<size_t>(directorySize));
		if (!seek(directoryOffset) || !readBytes(directory.data(), directory.size()))
		{
			message = "cannot read the archive's directory";
			return false;
		}
		size_t at = 0;
		for (uint64_t k = 0; k < entryCount && at + 46 <= directory.size(); ++k)
		{
			const uint8_t* p = &directory[at];
			if (u32(p) != 0x02014b50)
				break;
			Entry e;
			e.method = u16(p + 10);
			e.compressedSize = u32(p + 20);
			e.size = u32(p + 24);
			const uint16_t nameLength = u16(p + 28), extraLength = u16(p + 30), commentLength = u16(p + 32);
			e.localHeader = u32(p + 42);
			if (at + 46 + nameLength + extraLength > directory.size())
				break;
			e.name.assign(reinterpret_cast<const char*>(p + 46), nameLength);
			std::replace(e.name.begin(), e.name.end(), '\\', '/');
			// zip64 extra field: the 32-bit fields that are 0xFFFFFFFF follow in this order
			const uint8_t* extra = p + 46 + nameLength;
			for (size_t x = 0; x + 4 <= extraLength;)
			{
				const uint16_t id = u16(extra + x), length = u16(extra + x + 2);
				if (id == 0x0001)
				{
					size_t f = x + 4;
					if (e.size == 0xFFFFFFFFu && f + 8 <= x + 4 + length) { e.size = u64(extra + f); f += 8; }
					if (e.compressedSize == 0xFFFFFFFFu && f + 8 <= x + 4 + length) { e.compressedSize = u64(extra + f); f += 8; }
					if (e.localHeader == 0xFFFFFFFFu && f + 8 <= x + 4 + length) { e.localHeader = u64(extra + f); f += 8; }
				}
				x += 4 + length;
			}
			list.push_back(std::move(e));
			at += 46 + nameLength + extraLength + commentLength;
		}
		return true;
	}

	std::vector<uint8_t> ZipReader::read(const Entry& entry)
	{
		std::vector<uint8_t> out;
		uint8_t header[30];
		if (!file || !seek(entry.localHeader) || !readBytes(header, sizeof(header)) || u32(header) != 0x04034b50)
		{
			message = "broken entry " + entry.name;
			return out;
		}
		const uint64_t dataStart = entry.localHeader + 30 + u16(header + 26) + u16(header + 28);
		std::vector<uint8_t> packed(static_cast<size_t>(entry.compressedSize));
		if (!seek(dataStart) || !readBytes(packed.data(), packed.size()))
		{
			message = "cannot read " + entry.name;
			return out;
		}
		if (entry.method == 0)
			return packed;
		if (entry.method != 8)
		{
			message = "unsupported compression in " + entry.name;
			return out;
		}
		out.resize(static_cast<size_t>(entry.size));
		z_stream stream{};
		if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
			return {};
		stream.next_in = packed.data();
		stream.avail_in = static_cast<uInt>(packed.size());
		stream.next_out = out.data();
		stream.avail_out = static_cast<uInt>(out.size());
		const int result = inflate(&stream, Z_FINISH);
		inflateEnd(&stream);
		if (result != Z_STREAM_END)
		{
			message = "cannot unpack " + entry.name;
			return {};
		}
		return out;
	}
}
