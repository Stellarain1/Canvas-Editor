#include "BinaryReader.h"
#include "IO.h"

namespace IO
{
	BinaryReader::BinaryReader(const std::string& filename)
	{
		stream = NULL;
#ifdef _WIN32
		std::wstring wFilename = mbToWideStr(filename);
		stream = _wfopen(wFilename.c_str(), L"rb");
#else
		stream = fopen(filename.c_str(), "rb");
#endif
	}

	BinaryReader::~BinaryReader()
	{
		close();
	}

	bool BinaryReader::isStreamValid()
	{
		return stream;
	}

	void BinaryReader::close()
	{
		if (stream)
			fclose(stream);

		stream = NULL;
	}

	size_t BinaryReader::getFileSize()
	{
		size_t pos = ftell(stream);
		fseek(stream, 0, SEEK_END);

		size_t size = ftell(stream);
		fseek(stream, pos, SEEK_SET);

		return size;
	}

	size_t BinaryReader::getStreamPosition()
	{
		return ftell(stream);
	}

	uint16_t IO::BinaryReader::readInt16()
	{
		uint16_t data = 0;
		if (stream)
			fread(&data, sizeof(uint16_t), 1, stream);
		return data;
	}

	uint32_t BinaryReader::readInt32()
	{
		uint32_t data = 0;
		if (stream)
			fread(&data, sizeof(uint32_t), 1, stream);
		return data;
	}

	float BinaryReader::readSingle()
	{
		float data = 0;
		if (stream)
			fread(&data, sizeof(float), 1, stream);
		return data;
	}

	std::string BinaryReader::readString()
	{
		char c = 'a';
		std::string data = "";
		if (stream)
		{
			while (c && !feof(stream))
			{
				fread(&c, sizeof(uint8_t), 1, stream);
				if (c)
					data += c;
			}

		}
		return data;
	}

	std::string BinaryReader::readBytes(size_t count)
	{
		std::string data(count, '\0');
		size_t read = stream ? fread(data.data(), 1, count, stream) : 0;
		data.resize(read);
		return data;
	}

	void BinaryReader::seek(size_t pos)
	{
		if (stream)
			fseek(stream, pos, SEEK_SET);
	}
}
