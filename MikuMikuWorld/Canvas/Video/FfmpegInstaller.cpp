#include "FfmpegInstaller.h"
#include "../../Application.h"
#include "../../IO.h"
#include "../../File.h"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace Canvas::Video
{
	namespace
	{
		struct Source
		{
			std::vector<std::string> urls;	// tried in order
			std::string archive;			// file name of the download
		};

		Source platformSource()
		{
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
			return { { "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-winarm64-gpl.zip" }, "ffmpeg.zip" };
#else
			return { { "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip",
				"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" }, "ffmpeg.zip" };
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
			return { { "https://ffmpeg.martin-riedl.de/redirect/latest/macos/arm64/release/ffmpeg.zip" }, "ffmpeg.zip" };
#else
			return { { "https://ffmpeg.martin-riedl.de/redirect/latest/macos/amd64/release/ffmpeg.zip",
				"https://evermeet.cx/ffmpeg/getrelease/zip" }, "ffmpeg.zip" };
#endif
#else
#if defined(__aarch64__)
			return { { "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linuxarm64-gpl.tar.xz" }, "ffmpeg.tar.xz" };
#else
			return { { "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl.tar.xz" }, "ffmpeg.tar.xz" };
#endif
#endif
		}

		std::string findExecutable(const fs::path& dir)
		{
#ifdef _WIN32
			const std::string name = "ffmpeg.exe";
#else
			const std::string name = "ffmpeg";
#endif
			std::error_code error;
			for (auto it = fs::recursive_directory_iterator(dir, error); !error && it != fs::recursive_directory_iterator(); it.increment(error))
				if (it->is_regular_file(error) && it->path().filename() == name)
					return it->path().u8string();
			return {};
		}
	}

	FfmpegInstaller::~FfmpegInstaller()
	{
		if (worker.joinable())
			worker.join();
	}

	std::string FfmpegInstaller::installFolder()
	{
		return MikuMikuWorld::Application::getUserDataDir() + "ffmpeg";
	}

	std::string FfmpegInstaller::sourceUrl()
	{
		return platformSource().urls.front();
	}

	void FfmpegInstaller::start()
	{
		if (busy())
			return;
		if (worker.joinable())
			worker.join();
		{
			std::lock_guard<std::mutex> lock(mutex);
			status.clear();
			executable.clear();
		}
		currentState = State::Downloading;
		worker = std::thread(&FfmpegInstaller::run, this);
	}

	long long FfmpegInstaller::downloadedBytes() const
	{
		std::string file;
		{
			std::lock_guard<std::mutex> lock(mutex);
			file = archive;
		}
		std::error_code error;
		const auto size = file.empty() ? 0 : fs::file_size(fs::u8path(file), error);
		return error ? 0 : static_cast<long long>(size);
	}

	std::string FfmpegInstaller::message() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return status;
	}

	std::string FfmpegInstaller::installedPath() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return executable;
	}

	void FfmpegInstaller::acknowledge()
	{
		if (!busy())
			currentState = State::Idle;
	}

	void FfmpegInstaller::fail(const std::string& why)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			status = why;
		}
		currentState = State::Failed;
	}

	void FfmpegInstaller::run()
	{
		const Source source = platformSource();
		const fs::path folder = fs::u8path(installFolder());
		std::error_code error;
		fs::remove_all(folder, error);
		fs::create_directories(folder, error);
		const fs::path download = folder / source.archive;
		{
			std::lock_guard<std::mutex> lock(mutex);
			archive = download.u8string();
		}

		const bool haveCurl = IO::commandExists("curl");
		bool downloaded = false;
		std::string lastOutput;
		for (const std::string& url : source.urls)
		{
			int exitCode = -1;
			const std::string command = haveCurl
				? "curl -L -f -s -S -o " + IO::shellQuote(download.u8string()) + " " + IO::shellQuote(url)
				: "wget -q -O " + IO::shellQuote(download.u8string()) + " " + IO::shellQuote(url);
			lastOutput = IO::runProcess(command, &exitCode);
			if (exitCode == 0 && fs::file_size(download, error) > 1000000)
			{
				downloaded = true;
				break;
			}
		}
		if (!downloaded)
		{
			fail(haveCurl || IO::commandExists("wget") ? "the download failed (" + sourceUrl() + ")" + (lastOutput.empty() ? "" : ": " + lastOutput)
				: "neither curl nor wget is installed");
			return;
		}

		currentState = State::Extracting;
		int exitCode = -1;
#if defined(_WIN32)
		IO::runProcess("tar -xf " + IO::shellQuote(download.u8string()) + " -C " + IO::shellQuote(folder.u8string()), &exitCode);
#elif defined(__APPLE__)
		IO::runProcess("unzip -o -q " + IO::shellQuote(download.u8string()) + " -d " + IO::shellQuote(folder.u8string()), &exitCode);
#else
		IO::runProcess("tar -xJf " + IO::shellQuote(download.u8string()) + " -C " + IO::shellQuote(folder.u8string()), &exitCode);
#endif
		fs::remove(download, error);
		const std::string found = findExecutable(folder);
		if (exitCode != 0 || found.empty())
		{
			fail("the download could not be unpacked");
			return;
		}
#ifndef _WIN32
		int ignored = 0;
		IO::runProcess("chmod +x " + IO::shellQuote(found), &ignored);
#ifdef __APPLE__
		// downloaded executables are quarantined: Gatekeeper would refuse to start ffmpeg
		IO::runProcess("xattr -dr com.apple.quarantine " + IO::shellQuote(folder.u8string()), &ignored);
#endif
#endif
		{
			std::lock_guard<std::mutex> lock(mutex);
			executable = found;
			status = found;
			archive.clear();
		}
		currentState = State::Done;
	}
}
