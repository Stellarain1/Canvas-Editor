#pragma once
#include <array>
#include <memory>
#include <string>

struct ma_engine;
struct ma_sound;

// SEKAI-Stories UI sounds: "select" on clicks, "open" / "close" for windows.
// Played from the configured SEKAI-Stories folder; when there is none, Canvas generates its own
// short stand-in sounds (see generateFallbackSounds) so SEKAI Mode still clicks and chimes.
namespace Canvas::Sekai
{
	enum class UiSound
	{
		Select,
		Open,
		Close,
		Slide,
		Count
	};

	class UiSounds
	{
	public:
		void initialize(ma_engine* engine, const std::string& fallbackDir);
		void shutdown();

		void play(UiSound sound, float volume);

		// "ui sounds: select 3, open 1, close 0, slide 12, not played 0" (for automated runs)
		std::string summary() const;

		// Writes select/open/close/slide .wav stand-ins into `dir` (only the missing ones).
		static void generateFallbackSounds(const std::string& dir);

	private:
		struct Loaded
		{
			std::string path;
			std::unique_ptr<ma_sound> sound;
		};

		ma_engine* engine{ nullptr };
		std::string fallbackDir;
		std::array<Loaded, static_cast<size_t>(UiSound::Count)> loaded;
		std::array<int, static_cast<size_t>(UiSound::Count)> started{};
		int notPlayed{ 0 };

		std::string resolve(UiSound sound) const;
	};

	UiSounds& uiSounds();
}
