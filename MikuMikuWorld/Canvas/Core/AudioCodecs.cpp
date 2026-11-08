// The one translation unit that compiles miniaudio (with its built-in WAV / FLAC / MP3 decoders).
// It lives in canvas_core so canvas-cli and the tests can read songs too; the editor
// links it from here. stb_vorbis (Ogg) is compiled as its own C file next to it, and its header
// has to come before miniaudio.h for miniaudio to use it.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "AudioCodecs.h"

namespace Canvas::AudioCodecs
{
	int16_t* decodeMp3S16(const void* data, size_t size, uint32_t* channels, uint32_t* sampleRate, uint64_t* frameCount)
	{
		ma_dr_mp3_config config{};
		ma_uint64 frames{};
		int16_t* samples = ma_dr_mp3_open_memory_and_read_pcm_frames_s16(data, size, &config, &frames, nullptr);
		*channels = config.channels;
		*sampleRate = config.sampleRate;
		*frameCount = frames;
		return samples;
	}

	int16_t* decodeWavS16(const void* data, size_t size, uint32_t* channels, uint32_t* sampleRate, uint64_t* frameCount)
	{
		ma_uint64 frames{};
		int16_t* samples = ma_dr_wav_open_memory_and_read_pcm_frames_s16(data, size, channels, sampleRate, &frames, nullptr);
		*frameCount = frames;
		return samples;
	}

	int16_t* decodeFlacS16(const void* data, size_t size, uint32_t* channels, uint32_t* sampleRate, uint64_t* frameCount)
	{
		ma_uint64 frames{};
		int16_t* samples = ma_dr_flac_open_memory_and_read_pcm_frames_s16(data, size, channels, sampleRate, &frames, nullptr);
		*frameCount = frames;
		return samples;
	}
}
