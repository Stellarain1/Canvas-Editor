#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// "Install ffmpeg" in the Export Video window: downloads a static ffmpeg build for this platform
// with the system's own tools (curl + tar on Windows 10+, curl + unzip / tar on macOS and Linux),
// unpacks it into <user data>/ffmpeg and points config.ffmpegPath at it. Runs on a worker
// thread; the window polls state().
namespace Canvas::Video
{
	class FfmpegInstaller
	{
	public:
		enum class State { Idle, Downloading, Extracting, Done, Failed };

		~FfmpegInstaller();

		void start();
		State state() const { return currentState.load(); }
		bool busy() const { const State s = state(); return s == State::Downloading || s == State::Extracting; }
		// bytes on disk so far (the download size is not known up front)
		long long downloadedBytes() const;
		std::string message() const;
		// the installed executable once state() == Done
		std::string installedPath() const;
		// consume the Done / Failed state (after the window acted on it)
		void acknowledge();

		static std::string installFolder();
		// where the download comes from on this platform (shown in the window)
		static std::string sourceUrl();

	private:
		void run();
		void fail(const std::string& why);

		std::thread worker;
		std::atomic<State> currentState{ State::Idle };
		mutable std::mutex mutex;
		std::string status, executable, archive;
	};
}
