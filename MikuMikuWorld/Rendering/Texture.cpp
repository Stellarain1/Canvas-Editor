#include "../File.h"
#include "../IO.h"
#include "Texture.h"
#include <glad/glad.h>
#include "GLFW/glfw3.h"
#include "stb_image.h"

using namespace IO;

namespace MikuMikuWorld
{
	Texture::Texture(const std::string& filename) :
		Texture(filename, TextureFilterMode::Linear, TextureFilterMode::Linear)
	{
	}

	Texture::Texture(const std::string& filename, TextureFilterMode min, TextureFilterMode mag) :
		glID{ 0 }, width{ 0 }, height{ 0 }, filename{ filename }
	{
		name = File::getFilenameWithoutExtension(filename);
		if (!File::exists(filename))
			return;

		read(filename, min, mag);

		std::string sprSheet = File::getFilepath(filename) + "spr/" + name + ".txt";
		if (File::exists(sprSheet))
		{
			readSprites(sprSheet);
		}
		else
		{
			sprites.push_back(Sprite(0, 0, width, height));
		}
	}

	Texture::Texture(const std::string& filename, TextureFilterMode filter) :
		Texture(filename, filter, filter)
	{

	}

	Texture::Texture(const std::string& filename, SmoothTag) :
		glID{ 0 }, width{ 0 }, height{ 0 }, filename{ filename }
	{
		name = File::getFilenameWithoutExtension(filename);
		if (!File::exists(filename))
			return;
		readSmooth(filename);
		sprites.push_back(Sprite(0, 0, width, height));
	}

	void Texture::bind() const
	{
		glBindTexture(GL_TEXTURE_2D, glID);
	}

	void Texture::dispose()
	{
		if (glID > 0)
		{
			glDeleteTextures(1, &glID);
			width = height = glID = 0;
		}
	}

	void Texture::readSprites(const std::string& filename)
	{
		File f(filename, FileMode::Read);
		std::vector<std::string> lines = f.readAllLines();
		f.close();

		for (auto& line : lines)
		{
			line = trim(line);
			if (!isComment(line, "#") && !line.empty())
				sprites.push_back(parseSprite(line));
		}
	}

	Sprite Texture::parseSprite(const std::string& line)
	{
		std::vector<std::string> values = split(line, ",");
		int x = atoi(values[0].c_str());
		int y = atoi(values[1].c_str());
		int w = atoi(values[2].c_str());
		int h = atoi(values[3].c_str());

		return Sprite(x, y, w, h);
	}

	void Texture::readSmooth(const std::string& filename)
	{
		int nrChannels;
		stbi_set_flip_vertically_on_load(0);
		unsigned char* data = stbi_load(filename.c_str(), &width, &height, &nrChannels, 4);
		if (!data)
		{
			width = height = 0;
			return;
		}

		// spread the colour of visible pixels into the see-through ones next to them (a few rings
		// cover the mip levels the HUD actually uses); their alpha stays 0
		std::vector<unsigned char> filled(static_cast<size_t>(width) * height, 0);
		for (int i = 0; i < width * height; ++i)
			filled[i] = data[i * 4 + 3] > 0;
		std::vector<int> ring;
		for (int pass = 0; pass < 8; ++pass)
		{
			ring.clear();
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
				{
					const int i = y * width + x;
					if (filled[i])
						continue;
					int r = 0, g = 0, b = 0, n = 0;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int nx = x + dx, ny = y + dy;
							if ((dx || dy) && nx >= 0 && ny >= 0 && nx < width && ny < height && filled[ny * width + nx] == 1)
							{
								const unsigned char* p = data + (static_cast<size_t>(ny) * width + nx) * 4;
								r += p[0], g += p[1], b += p[2], ++n;
							}
						}
					if (n)
					{
						unsigned char* p = data + static_cast<size_t>(i) * 4;
						p[0] = static_cast<unsigned char>(r / n), p[1] = static_cast<unsigned char>(g / n), p[2] = static_cast<unsigned char>(b / n);
						ring.push_back(i);
					}
				}
			if (ring.empty())
				break;
			for (int i : ring)
				filled[i] = 1;
		}

		glGenTextures(1, &glID);
		glBindTexture(GL_TEXTURE_2D, glID);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
		stbi_image_free(data);
	}

	void Texture::read(const std::string& filename, TextureFilterMode minFilter, TextureFilterMode magFilter)
	{
		glGenTextures(1, &glID);
		glBindTexture(GL_TEXTURE_2D, glID);

		int nrChannels;
		stbi_set_flip_vertically_on_load(0);
		auto data = stbi_load(filename.c_str(), &width, &height, &nrChannels, 4);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)minFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)magFilter);

		glBindTexture(GL_TEXTURE_2D, 0);
		free(data);
	}
}