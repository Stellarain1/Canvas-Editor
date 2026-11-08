#include "EffectAudio.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../Score.h"
#include "../../IO.h"
#include "../../File.h"
#include "LiveCards.h"
#include <miniaudio.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using namespace MikuMikuWorld;

namespace Canvas::Preview
{
	namespace
	{
		constexpr double TAU = 6.283185307179586;

		// ---- built-in chimes (no game audio needed) ---------------------------------------------------

		// a bell: a few inharmonic partials decaying at different rates
		void bell(std::vector<float>& out, double start, double freq, double length, float gain, float pan = 0.0f)
		{
			static const double partials[][3] = { { 1.0, 1.0, 1.0 }, { 2.0, 0.45, 1.8 }, { 2.76, 0.28, 2.6 }, { 5.4, 0.12, 4.0 } };
			const size_t first = static_cast<size_t>(start * EFFECT_SAMPLE_RATE);
			const size_t count = static_cast<size_t>(length * EFFECT_SAMPLE_RATE);
			if (out.size() < (first + count) * 2)
				out.resize((first + count) * 2, 0.0f);
			const float left = gain * std::min(1.0f, 1.0f - pan), right = gain * std::min(1.0f, 1.0f + pan);
			for (size_t i = 0; i < count; ++i)
			{
				const double t = static_cast<double>(i) / EFFECT_SAMPLE_RATE;
				const double attack = std::min(1.0, t / 0.004);
				double v = 0.0;
				for (const auto& p : partials)
					v += p[1] * std::sin(TAU * freq * p[0] * t) * std::exp(-t * p[2] * 3.0 / length);
				v *= attack * 0.35;
				out[(first + i) * 2] += static_cast<float>(v * left);
				out[(first + i) * 2 + 1] += static_cast<float>(v * right);
			}
		}

		void sweep(std::vector<float>& out, double start, double f0, double f1, double length, float gain)
		{
			const size_t first = static_cast<size_t>(start * EFFECT_SAMPLE_RATE);
			const size_t count = static_cast<size_t>(length * EFFECT_SAMPLE_RATE);
			if (out.size() < (first + count) * 2)
				out.resize((first + count) * 2, 0.0f);
			double phase = 0.0;
			for (size_t i = 0; i < count; ++i)
			{
				const double t = static_cast<double>(i) / count;
				const double f = f0 * std::pow(f1 / f0, t);
				phase += TAU * f / EFFECT_SAMPLE_RATE;
				const double env = std::sin(3.14159265 * t) * (1.0 - t * 0.5);
				const float v = static_cast<float>(std::sin(phase) * env * gain * 0.3);
				out[(first + i) * 2] += v;
				out[(first + i) * 2 + 1] += v;
			}
		}

		void finish(std::vector<float>& out)
		{
			float peak = 0.0f;
			for (float v : out)
				peak = std::max(peak, std::abs(v));
			if (peak > 0.9f)
				for (float& v : out)
					v *= 0.9f / peak;
		}

		std::vector<float> synthesize(EffectSound sound)
		{
			std::vector<float> out;
			switch (sound)
			{
			case EffectSound::Skill:
				sweep(out, 0.0, 900.0, 2600.0, 0.22, 0.9f);
				bell(out, 0.05, 2093.0, 0.8, 0.5f, -0.3f);
				bell(out, 0.09, 2637.0, 0.8, 0.45f, 0.3f);
				bell(out, 0.13, 3136.0, 0.9, 0.4f, 0.0f);
				break;
			case EffectSound::FeverChance:
				bell(out, 0.0, 784.0, 1.2, 0.7f, -0.2f);
				bell(out, 0.14, 1175.0, 1.3, 0.7f, 0.2f);
				sweep(out, 0.0, 300.0, 1200.0, 0.5, 0.25f);
				break;
			case EffectSound::Fever:
			case EffectSound::SuperFever:
			{
				const double notes[] = { 1047.0, 1319.0, 1568.0, 2093.0 };
				for (int i = 0; i < 4; ++i)
					bell(out, i * 0.07, notes[i], 1.4, 0.6f, (i - 1.5f) * 0.25f);
				if (sound == EffectSound::SuperFever)
					for (int i = 0; i < 4; ++i)
						bell(out, 0.32 + i * 0.06, notes[i] * 2.0, 1.4, 0.35f, (1.5f - i) * 0.25f);
				sweep(out, 0.0, 400.0, 2000.0, 0.35, 0.3f);
				break;
			}
			case EffectSound::AllPerfect:
			{
				for (double f : { 523.25, 659.25, 783.99 })
					bell(out, 0.0, f, 3.0, 0.45f);
				const double run[] = { 1046.5, 1318.5, 1568.0, 2093.0, 2637.0, 3136.0, 4186.0 };
				for (int i = 0; i < 7; ++i)
					bell(out, 0.18 + i * 0.075, run[i], 2.2, 0.38f, (i % 2 ? 0.35f : -0.35f));
				sweep(out, 0.0, 200.0, 3200.0, 0.6, 0.25f);
				break;
			}
			default:
				break;
			}
			finish(out);
			return out;
		}

		// ---- game audio from the asset pack ---------------------------------------------------------------

		std::string sourceOf(EffectSound sound, const std::string& overlay)
		{
			if (overlay.empty())
				return {};
			const char* relative = nullptr;
			switch (sound)
			{
			case EffectSound::Skill: relative = "assets/skill/SE_LIVE_SKILL.mp3"; break;
			case EffectSound::FeverChance: relative = "extra-assets/fever/fx_fever_chance_v2.mp4"; break;
			case EffectSound::Fever: relative = "extra-assets/fever/fx_fever_v2.mp4"; break;
			case EffectSound::SuperFever: relative = "extra-assets/fever/fx_super_fever_v2.mp4"; break;
			case EffectSound::AllPerfect: relative = "assets/ap.mp4"; break;
			default: return {};
			}
			const std::string path = (fs::u8path(overlay) / fs::u8path(relative)).u8string();
			return IO::File::exists(path) ? path : std::string{};
		}

		const char* cacheName(EffectSound sound)
		{
			static const char* names[] = { "skill", "fever_chance", "fever", "super_fever", "all_perfect", "cutin", "voice1", "voice2", "voice3", "voice4", "voice5" };
			return names[static_cast<int>(sound)];
		}

		// the movie clips carry their sound: ffmpeg copies it out once into <user data>/cache
		bool extractAudio(const std::string& movie, const std::string& wav)
		{
			const std::string& ffmpeg = config.ffmpegPath;
			if (ffmpeg.empty())
				return false;
			std::error_code error;
			fs::create_directories(fs::u8path(wav).parent_path(), error);
			int exitCode = -1;
			IO::runProcess(IO::shellQuote(ffmpeg) + " -v error -y -i " + IO::shellQuote(movie) + " -vn -ac 2 -ar 44100 -c:a pcm_s16le " + IO::shellQuote(wav), &exitCode);
			return exitCode == 0 && IO::File::exists(wav);
		}

		struct Cache
		{
			std::array<std::vector<float>, static_cast<size_t>(EffectSound::Count)> pcm;
			std::array<std::string, static_cast<size_t>(EffectSound::Count)> key;
		};

		Cache& cache()
		{
			static Cache c;
			return c;
		}
	}

	bool decodeAudioFile(const std::string& path, std::vector<float>& out)
	{
		ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, EFFECT_SAMPLE_RATE);
		ma_decoder decoder;
#ifdef _WIN32
		// the narrow version goes through the ANSI code page: any non-ASCII folder broke it
		if (ma_decoder_init_file_w(IO::mbToWideStr(path).c_str(), &decoderConfig, &decoder) != MA_SUCCESS)
			return false;
#else
		if (ma_decoder_init_file(path.c_str(), &decoderConfig, &decoder) != MA_SUCCESS)
			return false;
#endif
		std::vector<float> chunk(4096 * 2);
		ma_uint64 read = 0;
		do
		{
			read = 0;
			if (ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read) != MA_SUCCESS && read == 0)
				break;
			out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<size_t>(read) * 2);
		} while (read > 0);
		ma_decoder_uninit(&decoder);
		return !out.empty();
	}

	const std::vector<float>& effectPcm(EffectSound sound)
	{
		Cache& c = cache();
		const size_t index = static_cast<size_t>(sound);
		if (sound >= EffectSound::Voice1 && sound <= EffectSound::Voice5)
		{
			const std::string& voice = deckCard(static_cast<int>(index - static_cast<size_t>(EffectSound::Voice1))).voice;
			const std::string key = voice + "|" + std::to_string(deckRevision());
			if (c.key[index] != key)
			{
				c.key[index] = key;
				c.pcm[index].clear();
				if (!voice.empty())
					decodeAudioFile(voice, c.pcm[index]);
			}
			return c.pcm[index];
		}
		const std::string overlay = activeSkin().overlayDir;
		const std::string key = overlay + "|" + config.ffmpegPath;
		if (c.key[index] == key && !c.pcm[index].empty())
			return c.pcm[index];
		c.key[index] = key;
		c.pcm[index].clear();

		// the game's own live sounds (res/sound/live, from OpenSekai's se_live_common)
		{
			const char* game = nullptr;
			switch (sound)
			{
			case EffectSound::Skill: game = "skill"; break;
			case EffectSound::FeverChance: game = "fever_chance"; break;
			case EffectSound::Fever: case EffectSound::SuperFever: game = "fever"; break;
			case EffectSound::AllPerfect: game = "all_perfect"; break;
			case EffectSound::Cutin: game = "cutin"; break;
			default: break;
			}
			if (game)
			{
				const std::string path = Application::getAppDir() + "res/sound/live/" + game + ".flac";
				if (IO::File::exists(path) && decodeAudioFile(path, c.pcm[index]))
					return c.pcm[index];
			}
			if (sound == EffectSound::Cutin)
				return c.pcm[index];	// no stand-in for the cut-in whoosh
		}

		const std::string source = sourceOf(sound, overlay);
		if (!source.empty())
		{
			if (sound == EffectSound::Skill)
				decodeAudioFile(source, c.pcm[index]);
			else
			{
				const std::string wav = Application::getUserDataDir() + "cache/effects/" + cacheName(sound) + ".wav";
				if (IO::File::exists(wav) || extractAudio(source, wav))
					decodeAudioFile(wav, c.pcm[index]);
			}
		}
		if (c.pcm[index].empty())
			c.pcm[index] = synthesize(sound);
		return c.pcm[index];
	}

	std::vector<EffectCue> effectCues(const Score& score, const OverlayOptions& options)
	{
		std::vector<EffectCue> cues;
		const EffectTimes times = effectTimes(score);
		if (options.skills)
			for (double t : times.skills)
				cues.push_back({ t, EffectSound::Skill });
		// the cut-in sweeps in, and its card says its line as it lands (skill n is card n % 5)
		if (options.cutins)
			for (size_t i = 0; i < times.skills.size(); ++i)
			{
				cues.push_back({ times.skills[i], EffectSound::Cutin });
				if (!deckCard(static_cast<int>(i)).voice.empty())
					cues.push_back({ times.skills[i] + 0.12, static_cast<EffectSound>(static_cast<int>(EffectSound::Voice1) + static_cast<int>(i % DECK_SIZE)) });
			}
		if (options.fever)
		{
			if (times.feverChance >= 0)
				cues.push_back({ times.feverChance, EffectSound::FeverChance });
			if (times.feverStart >= 0)
				cues.push_back({ times.feverStart, options.mode != LiveMode::Solo ? EffectSound::SuperFever : EffectSound::Fever });
		}
		if (options.allPerfect && times.allPerfect >= 0)
			cues.push_back({ times.allPerfect, EffectSound::AllPerfect });
		std::sort(cues.begin(), cues.end(), [](const EffectCue& a, const EffectCue& b) { return a.time < b.time; });
		return cues;
	}

	// ---- live playback -------------------------------------------------------------------------------------

	struct EffectPlayer::Voice
	{
		ma_audio_buffer buffer{};
		ma_sound sound{};
		bool ready{ false };
	};

	EffectPlayer::~EffectPlayer()
	{
		stop();
	}

	void EffectPlayer::play(ma_engine* engine, EffectSound sound, float volume, double offsetSeconds)
	{
		const std::vector<float>& pcm = effectPcm(sound);
		const ma_uint64 frames = pcm.size() / 2;
		const ma_uint64 offset = static_cast<ma_uint64>(std::max(0.0, offsetSeconds) * EFFECT_SAMPLE_RATE);
		if (frames == 0 || offset >= frames)
			return;
		Voice* voice = new Voice();
		ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_f32, 2, frames, pcm.data(), nullptr);
		bufferConfig.sampleRate = EFFECT_SAMPLE_RATE;
		if (ma_audio_buffer_init(&bufferConfig, &voice->buffer) != MA_SUCCESS)
		{
			delete voice;
			return;
		}
		if (ma_sound_init_from_data_source(engine, &voice->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice->sound) != MA_SUCCESS)
		{
			ma_audio_buffer_uninit(&voice->buffer);
			delete voice;
			return;
		}
		voice->ready = true;
		if (offset > 0)
			ma_sound_seek_to_pcm_frame(&voice->sound, offset);
		ma_sound_set_volume(&voice->sound, volume);
		ma_sound_start(&voice->sound);
		voices.push_back(voice);
	}

	void EffectPlayer::reap()
	{
		for (auto it = voices.begin(); it != voices.end();)
		{
			Voice* v = *it;
			if (v->ready && ma_sound_at_end(&v->sound))
			{
				ma_sound_uninit(&v->sound);
				ma_audio_buffer_uninit(&v->buffer);
				delete v;
				it = voices.erase(it);
			}
			else
				++it;
		}
	}

	void EffectPlayer::stop()
	{
		for (Voice* v : voices)
		{
			if (v->ready)
			{
				ma_sound_stop(&v->sound);
				ma_sound_uninit(&v->sound);
				ma_audio_buffer_uninit(&v->buffer);
			}
			delete v;
		}
		voices.clear();
	}

	void EffectPlayer::update(ma_engine* engine, bool playing, double time, const std::vector<EffectCue>& cues, float volume)
	{
		reap();
		if (!playing || !engine)
		{
			if (wasPlaying)
				stop();
			wasPlaying = false;
			lastTime = time;
			return;
		}
		if (!wasPlaying || time < lastTime - 0.05 || time > lastTime + 1.0)
		{
			// playback started or jumped: only what starts from here on (a cue that began a moment
			// ago still plays, from where it would be)
			stop();
			for (const EffectCue& cue : cues)
				if (cue.time <= time && time - cue.time < 0.25)
					play(engine, cue.sound, volume, time - cue.time);
		}
		else
		{
			for (const EffectCue& cue : cues)
				if (cue.time > lastTime && cue.time <= time)
					play(engine, cue.sound, volume, time - cue.time);
		}
		wasPlaying = true;
		lastTime = time;
	}
}
