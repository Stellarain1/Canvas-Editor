#pragma once
#include <string>
#include <vector>
#include "Sprite.h"
#include "../File.h"
#include <glad/glad.h>

namespace MikuMikuWorld
{
	enum class WrapMode
	{
		Clamp,
		Repeat
	};

	enum class TextureFilterMode
	{
		Linear = GL_LINEAR,
		Nearest = GL_NEAREST,
		LinearMipMapLinear = GL_LINEAR_MIPMAP_LINEAR,
		LinearMipMapNearest = GL_LINEAR_MIPMAP_NEAREST,
		NearestMipMapLinear = GL_NEAREST_MIPMAP_LINEAR,
		NearestMipMapNearest = GL_NEAREST_MIPMAP_NEAREST
	};

	class Texture
	{
	private:
		std::string name;
		std::string filename;
		int width;
		int height;
		unsigned int glID;

		Sprite parseSprite(const std::string& line);

	public:
		std::vector<Sprite> sprites;

		Texture(const std::string& filename, TextureFilterMode min, TextureFilterMode mag);
		Texture(const std::string& filename, TextureFilterMode filter);
		Texture(const std::string& filename);
		// HUD art drawn much smaller than its pixels: trilinear mipmaps, edges clamped (nothing
		// bleeds in from the opposite side) and see-through pixels coloured like their neighbours
		// so the small mip levels get no dark fringes.
		struct SmoothTag {};
		static constexpr SmoothTag Smooth{};
		Texture(const std::string& filename, SmoothTag);

		inline int getWidth() const { return width; }
		inline int getHeight() const { return height; }
		inline unsigned int getID() const { return glID; }
		inline const std::string& getName() const { return name; }
		inline const std::string& getFilename() const { return filename; }

		void bind() const;
		void dispose();
		void read(const std::string& filename, TextureFilterMode minFilter = TextureFilterMode::Linear, TextureFilterMode magFilter = TextureFilterMode::Linear);
		void readSprites(const std::string& filename);
		void readSmooth(const std::string& filename);
	};
}