#include "SekaiAssets.h"
#include "../../IO.h"
#include "../../Application.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace Canvas::Sekai
{
	namespace
	{
		bool isPublicDir(const fs::path& dir)
		{
			std::error_code error;
			return fs::is_directory(dir / "sound", error) || fs::is_directory(dir / "fonts", error) ||
				fs::is_directory(dir / "background_low_jpg", error);
		}

		std::string existing(const fs::path& path)
		{
			std::error_code error;
			return fs::is_regular_file(path, error) ? path.u8string() : std::string{};
		}
	}

	void Assets::refresh(const std::string& configuredDir)
	{
		if (configuredDir == configured)
			return;

		configured = configuredDir;
		publicDir.clear();
		backgroundCache.clear();
		backgroundsScanned = false;

		bundled = false;
		if (!configuredDir.empty())
		{
			const fs::path dir = fs::u8path(configuredDir);
			if (isPublicDir(dir))
				publicDir = dir.u8string();
			else if (isPublicDir(dir / "public"))
				publicDir = (dir / "public").u8string();
		}

		// no SEKAI-Stories folder: the asset pack Canvas ships (res/sekai) has the same sounds and fonts
		if (publicDir.empty())
		{
			const fs::path pack = fs::u8path(MikuMikuWorld::Application::getAppDir() + "res/sekai");
			if (isPublicDir(pack))
			{
				publicDir = pack.u8string();
				bundled = true;
			}
		}
	}

	std::string Assets::sound(const std::string& name) const
	{
		if (publicDir.empty())
			return {};
		const fs::path base = fs::u8path(publicDir) / "sound";
		std::string path = existing(base / (name + ".wav"));
		return path.empty() ? existing(base / (name + ".mp3")) : path;
	}

	std::string Assets::image(const std::string& file) const
	{
		if (publicDir.empty())
			return {};
		const std::string img = existing(fs::u8path(publicDir) / "img" / file);
		return img.empty() ? existing(fs::u8path(publicDir) / "bg" / file) : img;
	}

	std::string Assets::font(const std::string& file) const
	{
		return publicDir.empty() ? std::string{} : existing(fs::u8path(publicDir) / "fonts" / file);
	}

	const std::vector<std::string>& Assets::backgrounds()
	{
		if (backgroundsScanned || publicDir.empty())
			return backgroundCache;

		backgroundsScanned = true;
		for (const char* folder : { "background_compressed", "background_low_jpg" })
		{
			std::error_code error;
			const fs::path dir = fs::u8path(publicDir) / folder;
			if (!fs::is_directory(dir, error))
				continue;

			for (const auto& entry : fs::directory_iterator(dir, error))
			{
				std::string extension = entry.path().extension().u8string();
				std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
				if (entry.is_regular_file() && (extension == ".jpg" || extension == ".jpeg" || extension == ".png"))
					backgroundCache.push_back(entry.path().u8string());
			}

			if (!backgroundCache.empty())
				break;
		}

		std::sort(backgroundCache.begin(), backgroundCache.end());
		return backgroundCache;
	}

	Assets& assets()
	{
		static Assets instance;
		return instance;
	}
}
