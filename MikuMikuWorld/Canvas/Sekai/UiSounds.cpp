#include "../../Audio/AudioManager.h"
#include "UiSounds.h"
#include "SekaiAssets.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace Canvas::Sekai
{
	namespace
	{
		constexpr const char* soundNames[] = { "select", "open", "close", "slide" };
		constexpr int SAMPLE_RATE = 44100;
		constexpr double PI = 3.14159265358979323846;

		struct Tone
		{
			double start;		// seconds
			double frequency;	// Hz at the start
			double glide;		// Hz at the end of the tone
			double length;		// seconds
			double gain;
		};

		// soft bell: sine + a little octave, 3 ms attack, exponential decay
		std::vector<int16_t> render(const std::vector<Tone>& tones, double total)
		{
			std::vector<double> mix(static_cast<size_t>(total * SAMPLE_RATE), 0.0);
			for (const Tone& tone : tones)
			{
				const size_t begin = static_cast<size_t>(tone.start * SAMPLE_RATE);
				const size_t count = static_cast<size_t>(tone.length * SAMPLE_RATE);
				double phase = 0;
				for (size_t i = 0; i < count && begin + i < mix.size(); ++i)
				{
					const double t = static_cast<double>(i) / SAMPLE_RATE;
					const double progress = t / tone.length;
					const double frequency = tone.frequency + (tone.glide - tone.frequency) * progress;
					phase += 2 * PI * frequency / SAMPLE_RATE;
					const double attack = std::min(1.0, t / 0.003);
					const double decay = std::exp(-5.0 * progress);
					mix[begin + i] += tone.gain * attack * decay * (std::sin(phase) + 0.25 * std::sin(2 * phase));
				}
			}

			std::vector<int16_t> samples(mix.size());
			for (size_t i = 0; i < mix.size(); ++i)
			{
				const double fadeOut = std::min(1.0, static_cast<double>(mix.size() - i) / (SAMPLE_RATE * 0.01));
				samples[i] = static_cast<int16_t>(std::clamp(mix[i] * fadeOut, -1.0, 1.0) * 32767 * 0.8);
			}
			return samples;
		}

		void writeWav(const fs::path& path, const std::vector<int16_t>& samples)
		{
			FILE* file = nullptr;
#ifdef _WIN32
			_wfopen_s(&file, path.wstring().c_str(), L"wb");
#else
			file = std::fopen(path.string().c_str(), "wb");
#endif
			if (!file)
				return;

			auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, file); };
			auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, file); };
			const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
			std::fwrite("RIFF", 1, 4, file); u32(36 + dataBytes);
			std::fwrite("WAVEfmt ", 1, 8, file); u32(16); u16(1); u16(1);
			u32(SAMPLE_RATE); u32(SAMPLE_RATE * 2); u16(2); u16(16);
			std::fwrite("data", 1, 4, file); u32(dataBytes);
			std::fwrite(samples.data(), 2, samples.size(), file);
			std::fclose(file);
		}
	}

	void UiSounds::generateFallbackSounds(const std::string& dir)
	{
		std::error_code error;
		const fs::path folder = fs::u8path(dir);
		fs::create_directories(folder, error);

		const std::pair<const char*, std::vector<int16_t>> sounds[] = {
			// quick bright blip
			{ "select", render({ { 0.0, 1568.0, 2093.0, 0.07, 0.55 }, { 0.012, 2637.0, 2637.0, 0.05, 0.18 } }, 0.10) },
			// rising two-note chime (A5 -> E6)
			{ "open", render({ { 0.0, 880.0, 880.0, 0.20, 0.45 }, { 0.07, 1318.5, 1318.5, 0.26, 0.45 } }, 0.36) },
			// falling (E6 -> A5), shorter
			{ "close", render({ { 0.0, 1318.5, 1318.5, 0.12, 0.40 }, { 0.05, 880.0, 830.0, 0.16, 0.40 } }, 0.22) },
			// tiny tick
			{ "slide", render({ { 0.0, 2349.0, 1975.0, 0.04, 0.30 } }, 0.06) },
		};

		for (const auto& [name, samples] : sounds)
		{
			const fs::path path = folder / (std::string(name) + ".wav");
			if (!fs::exists(path, error))
				writeWav(path, samples);
		}
	}

	void UiSounds::initialize(ma_engine* engine, const std::string& fallbackDir)
	{
		this->engine = engine;
		this->fallbackDir = fallbackDir;
	}

	void UiSounds::shutdown()
	{
		for (auto& entry : loaded)
		{
			if (entry.sound)
				ma_sound_uninit(entry.sound.get());
			entry.sound.reset();
			entry.path.clear();
		}
		engine = nullptr;
	}

	std::string UiSounds::resolve(UiSound sound) const
	{
		const std::string name = soundNames[static_cast<int>(sound)];
		std::string path = assets().sound(name);
		if (!path.empty() || fallbackDir.empty())
			return path;

		const fs::path fallback = fs::u8path(fallbackDir) / (name + ".wav");
		std::error_code error;
		if (!fs::exists(fallback, error))
			generateFallbackSounds(fallbackDir);
		return fs::exists(fallback, error) ? fallback.u8string() : std::string{};
	}

	void UiSounds::play(UiSound sound, float volume)
	{
		if (sound == UiSound::Count || volume <= 0.0f)
			return;
		if (!engine)
		{
			++notPlayed;
			return;
		}

		Loaded& entry = loaded[static_cast<size_t>(sound)];
		const std::string path = resolve(sound);
		if (path.empty())
		{
			++notPlayed;
			return;
		}

		if (entry.path != path)
		{
			if (entry.sound)
				ma_sound_uninit(entry.sound.get());
			entry.sound = std::make_unique<ma_sound>();
			if (ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, entry.sound.get()) != MA_SUCCESS)
			{
				entry.sound.reset();
				entry.path.clear();
				++notPlayed;
				return;
			}
			entry.path = path;
		}

		ma_sound_set_volume(entry.sound.get(), volume);
		ma_sound_seek_to_pcm_frame(entry.sound.get(), 0);
		if (ma_sound_start(entry.sound.get()) == MA_SUCCESS)
			++started[static_cast<size_t>(sound)];
		else
			++notPlayed;
	}

	std::string UiSounds::summary() const
	{
		char buffer[160];
		std::snprintf(buffer, sizeof(buffer), "ui sounds: select %d, open %d, close %d, slide %d, not played %d",
			started[0], started[1], started[2], started[3], notPlayed);
		return buffer;
	}

	UiSounds& uiSounds()
	{
		static UiSounds instance;
		return instance;
	}
}
