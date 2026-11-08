#pragma once
#include <cstddef>
#include <cstdint>

// Whole-file decoders to interleaved 16-bit samples, from the miniaudio build in AudioCodecs.cpp
// (miniaudio only declares its MP3 / WAV / FLAC readers inside its implementation section).
// Each returns nullptr on failure; the buffer comes from malloc.
namespace Canvas::AudioCodecs
{
	int16_t* decodeMp3S16(const void* data, size_t size, uint32_t* channels, uint32_t* sampleRate, uint64_t* frameCount);
	int16_t* decodeWavS16(const void* data, size_t size, uint32_t* channels, uint32_t* sampleRate, uint64_t* frameCount);
	int16_t* decodeFlacS16(const void* data, size_t size, uint32_t* channels, uint32_t* sampleRate, uint64_t* frameCount);
}
