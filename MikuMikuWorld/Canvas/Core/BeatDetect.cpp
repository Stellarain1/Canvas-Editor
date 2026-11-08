// Beat This! front-end, ONNX Runtime glue and peak picking. The tempo maps are in BeatTempoMap.cpp.
// Everything here follows CPJKU/beat_this (preprocessing.py, inference.py, postprocessor.py) 1:1.
#include "BeatDetect.h"
#include "AudioCodecs.h"
#include "ort/onnxruntime_c_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// stb_vorbis is compiled as C (see AudioCodecs.cpp)
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace Canvas::BeatDetect
{
	namespace fs = std::filesystem;

	// ====================================================================== runtime

	Runtime locate(const std::string& appDir)
	{
		fs::path dir = fs::u8path(appDir);
#ifdef _WIN32
		if (appDir.empty())
		{
			wchar_t buffer[MAX_PATH * 2]{};
			GetModuleFileNameW(nullptr, buffer, MAX_PATH * 2);
			dir = fs::path(buffer).parent_path();
		}
		const char* lib = "onnxruntime.dll";
#elif defined(__APPLE__)
		const char* lib = "libonnxruntime.dylib";
#else
		const char* lib = "libonnxruntime.so";
#endif
		Runtime runtime;
		runtime.dllPath = (dir / "res" / "ort" / lib).u8string();
		runtime.modelPath = (dir / "res" / "models" / "beat_this.onnx").u8string();
		return runtime;
	}

	namespace
	{
		struct Ort
		{
			const OrtApi* api{ nullptr };
			std::string loadedFrom, error;
		};

		// The DLL stays loaded for the life of the process. Only ever loaded by full path: a bare
		// "onnxruntime.dll" would pick Windows' own (older) copy from System32.
		const Ort& loadOrt(const std::string& dllPath)
		{
			static std::mutex lock;
			static Ort ort;
			std::lock_guard<std::mutex> guard(lock);
			if (ort.api && ort.loadedFrom == dllPath)
				return ort;
			if (ort.api)
				return ort;	// already have one; one runtime per process

			ort = Ort{};
			if (!fs::exists(fs::u8path(dllPath)))
			{
				ort.error = "ONNX Runtime was not found (" + dllPath + ").";
				return ort;
			}
#ifdef _WIN32
			HMODULE module = LoadLibraryExW(fs::u8path(dllPath).wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (!module)
			{
				ort.error = "ONNX Runtime could not be loaded (error " + std::to_string(GetLastError()) + "). It needs the Visual C++ runtime.";
				return ort;
			}
			auto getBase = reinterpret_cast<const OrtApiBase* (ORT_API_CALL*)()>(reinterpret_cast<void*>(GetProcAddress(module, "OrtGetApiBase")));
#else
			void* module = dlopen(dllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (!module)
			{
				ort.error = "ONNX Runtime could not be loaded.";
				return ort;
			}
			auto getBase = reinterpret_cast<const OrtApiBase* (*)()>(dlsym(module, "OrtGetApiBase"));
#endif
			if (!getBase)
			{
				ort.error = "This is not an ONNX Runtime library.";
				return ort;
			}
			// newest API first; older runtimes (the last macOS Intel build is 1.23) still have every
			// function we call, the table is only ever appended to
			for (int version = ORT_API_VERSION; version >= 17 && !ort.api; version--)
				ort.api = getBase()->GetApi(static_cast<uint32_t>(version));
			if (!ort.api)
			{
				ort.error = std::string("ONNX Runtime is too old (needs API ") + std::to_string(ORT_API_VERSION) + ").";
				return ort;
			}
			ort.loadedFrom = dllPath;
			return ort;
		}

		std::string statusMessage(const OrtApi* api, OrtStatus* status)
		{
			std::string text = api->GetErrorMessage(status);
			api->ReleaseStatus(status);
			return text;
		}
	}

	bool available(const Runtime& runtime, std::string* why)
	{
		const Ort& ort = loadOrt(runtime.dllPath);
		if (!ort.api)
		{
			if (why)
				*why = ort.error;
			return false;
		}
		if (!fs::exists(fs::u8path(runtime.modelPath)))
		{
			if (why)
				*why = "The Beat This! model was not found (" + runtime.modelPath + ").";
			return false;
		}
		return true;
	}

	// ====================================================================== audio in

	std::vector<float> decodeMono(const std::string& path, int& sampleRate, std::string& error)
	{
		std::ifstream in(fs::u8path(path), std::ios::binary);
		if (!in)
		{
			error = "Could not open the audio file.";
			return {};
		}
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (bytes.size() < 16)
		{
			error = "The audio file is empty.";
			return {};
		}

		uint32_t channels = 0, rate = 0;
		uint64_t frames = 0;
		int16_t* pcm = nullptr;
		bool vorbisBuffer = false;
		auto tryAll = [&](int first) {
			// first guess from the header, then the others
			for (int pass = 0; pass < 4 && !pcm; ++pass)
			{
				const int which = (first + pass) % 4;
				if (which == 0)
				{
					int ch = 0, sr = 0;
					short* out = nullptr;
					const int n = stb_vorbis_decode_memory(bytes.data(), static_cast<int>(bytes.size()), &ch, &sr, &out);
					if (n > 0 && out)
					{
						pcm = out;
						channels = static_cast<uint32_t>(ch);
						rate = static_cast<uint32_t>(sr);
						frames = static_cast<uint64_t>(n);
						vorbisBuffer = true;
					}
				}
				else if (which == 1)
					pcm = AudioCodecs::decodeFlacS16(bytes.data(), bytes.size(), &channels, &rate, &frames);
				else if (which == 2)
					pcm = AudioCodecs::decodeWavS16(bytes.data(), bytes.size(), &channels, &rate, &frames);
				else
					pcm = AudioCodecs::decodeMp3S16(bytes.data(), bytes.size(), &channels, &rate, &frames);
			}
		};
		int first = 3;
		if (!std::memcmp(bytes.data(), "OggS", 4))
			first = 0;
		else if (!std::memcmp(bytes.data(), "fLaC", 4))
			first = 1;
		else if (!std::memcmp(bytes.data(), "RIFF", 4))
			first = 2;
		tryAll(first);

		if (!pcm || channels == 0 || frames == 0)
		{
			error = "Could not decode the audio (WAV, MP3, FLAC and Ogg Vorbis are supported).";
			if (pcm)
				std::free(pcm);
			return {};
		}
		(void)vorbisBuffer;	// stb_vorbis also hands back a malloc buffer

		sampleRate = static_cast<int>(rate);
		std::vector<float> mono(frames);
		const float scale = 1.0f / (32768.0f * static_cast<float>(channels));
		for (uint64_t i = 0; i < frames; ++i)
		{
			int sum = 0;
			for (uint32_t c = 0; c < channels; ++c)
				sum += pcm[i * channels + c];
			mono[i] = static_cast<float>(sum) * scale;
		}
		std::free(pcm);
		return mono;
	}

	namespace
	{
		double besselI0(double x)
		{
			double sum = 1.0, term = 1.0;
			for (int k = 1; k < 60; ++k)
			{
				term *= (x / (2.0 * k)) * (x / (2.0 * k));
				sum += term;
				if (term < sum * 1e-17)
					break;
			}
			return sum;
		}
	}

	std::vector<float> resample(const std::vector<float>& in, int fromRate, int toRate)
	{
		if (fromRate == toRate || in.empty())
			return in;

		const int g = std::gcd(fromRate, toRate);
		const int64_t L = toRate / g, M = fromRate / g;			// output n sits at input n * M / L
		const int phases = static_cast<int>(std::min<int64_t>(L, 1024));
		const double fc = 0.5 * std::min(1.0, static_cast<double>(toRate) / fromRate) * 0.975;	// cycles per input sample
		const int half = static_cast<int>(std::ceil(20.0 / (2.0 * fc)));	// ~20 zero crossings per side
		const int taps = half * 2;
		const double beta = 9.0, i0Beta = besselI0(beta);
		const double pi = 3.14159265358979323846;

		std::vector<float> table(static_cast<size_t>(phases) * taps);
		for (int p = 0; p < phases; ++p)
		{
			const double frac = static_cast<double>(p) / phases;
			double sum = 0;
			std::vector<double> row(taps);
			for (int j = 0; j < taps; ++j)
			{
				const double x = frac + half - 1 - j;	// distance of the output position from input tap j
				const double r = x / half;
				double w = 0;
				if (std::abs(r) < 1.0)
				{
					const double arg = 2.0 * fc * x * pi;
					const double sinc = std::abs(arg) < 1e-12 ? 1.0 : std::sin(arg) / arg;
					w = 2.0 * fc * sinc * besselI0(beta * std::sqrt(1.0 - r * r)) / i0Beta;
				}
				row[j] = w;
				sum += w;
			}
			for (int j = 0; j < taps; ++j)
				table[static_cast<size_t>(p) * taps + j] = static_cast<float>(row[j] / sum);
		}

		const int64_t outCount = static_cast<int64_t>(in.size()) * toRate / fromRate;
		std::vector<float> out(static_cast<size_t>(outCount));
		const int64_t n = static_cast<int64_t>(in.size());
		for (int64_t o = 0; o < outCount; ++o)
		{
			const int64_t pos = o * M;
			const int64_t i0 = pos / L;
			const int64_t rem = pos % L;
			const int p = L == phases ? static_cast<int>(rem) : std::min(phases - 1, static_cast<int>(rem * phases / L));
			const float* row = &table[static_cast<size_t>(p) * taps];
			const int64_t k0 = i0 - half + 1;
			double acc = 0;
			if (k0 >= 0 && k0 + taps <= n)
			{
				const float* src = &in[static_cast<size_t>(k0)];
				for (int j = 0; j < taps; ++j)
					acc += static_cast<double>(src[j]) * row[j];
			}
			else
			{
				for (int j = 0; j < taps; ++j)
				{
					const int64_t k = k0 + j;
					if (k >= 0 && k < n)
						acc += static_cast<double>(in[static_cast<size_t>(k)]) * row[j];
				}
			}
			out[static_cast<size_t>(o)] = static_cast<float>(acc);
		}
		return out;
	}

	// ====================================================================== log-mel

	namespace
	{
		// Slaney mel scale, as torchaudio's mel_scale="slaney"
		double hzToMel(double hz)
		{
			const double fSp = 200.0 / 3.0, minLogHz = 1000.0, minLogMel = minLogHz / fSp, logStep = std::log(6.4) / 27.0;
			return hz >= minLogHz ? minLogMel + std::log(hz / minLogHz) / logStep : hz / fSp;
		}

		double melToHz(double mel)
		{
			const double fSp = 200.0 / 3.0, minLogHz = 1000.0, minLogMel = minLogHz / fSp, logStep = std::log(6.4) / 27.0;
			return mel >= minLogMel ? minLogHz * std::exp(logStep * (mel - minLogMel)) : fSp * mel;
		}

		struct MelBank
		{
			int first[MEL_BINS]{};
			std::vector<float> weights[MEL_BINS];	// from bin first[m]
		};

		// torchaudio.functional.melscale_fbanks(513, 30, 11000, 128, 22050, norm=None, "slaney")
		const MelBank& melBank()
		{
			static const MelBank bank = [] {
				MelBank b;
				const int freqs = FFT_SIZE / 2 + 1;
				std::vector<double> pts(MEL_BINS + 2);
				const double mLo = hzToMel(30.0), mHi = hzToMel(11000.0);
				for (int i = 0; i < MEL_BINS + 2; ++i)
					pts[i] = melToHz(mLo + (mHi - mLo) * i / (MEL_BINS + 1));
				for (int m = 0; m < MEL_BINS; ++m)
				{
					const double fDiffDown = pts[m + 1] - pts[m], fDiffUp = pts[m + 2] - pts[m + 1];
					int firstBin = -1;
					std::vector<float> w;
					for (int k = 0; k < freqs; ++k)
					{
						const double f = (SAMPLE_RATE / 2.0) * k / (freqs - 1);
						const double down = (f - pts[m]) / fDiffDown;
						const double up = (pts[m + 2] - f) / fDiffUp;
						const double v = std::max(0.0, std::min(down, up));
						if (v > 0)
						{
							if (firstBin < 0)
								firstBin = k;
							w.resize(static_cast<size_t>(k - firstBin + 1), 0.0f);
							w[static_cast<size_t>(k - firstBin)] = static_cast<float>(v);
						}
					}
					b.first[m] = std::max(firstBin, 0);
					b.weights[m] = std::move(w);
				}
				return b;
			}();
			return bank;
		}

		// in-place radix-2 FFT, n a power of two
		void fft(std::complex<double>* a, int n, const std::vector<std::complex<double>>& twiddle, const std::vector<int>& rev)
		{
			for (int i = 0; i < n; ++i)
				if (i < rev[i])
					std::swap(a[i], a[rev[i]]);
			for (int len = 2; len <= n; len <<= 1)
			{
				const int step = n / len;
				for (int i = 0; i < n; i += len)
					for (int j = 0; j < len / 2; ++j)
					{
						const std::complex<double> u = a[i + j], v = a[i + j + len / 2] * twiddle[j * step];
						a[i + j] = u + v;
						a[i + j + len / 2] = u - v;
					}
			}
		}
	}

	std::vector<float> logMel(const std::vector<float>& signal, int& frames)
	{
		const int n = FFT_SIZE, pad = n / 2;
		const int64_t count = static_cast<int64_t>(signal.size());
		frames = static_cast<int>(count / HOP + 1);	// center=True
		std::vector<float> out(static_cast<size_t>(frames) * MEL_BINS);
		if (count == 0)
			return out;

		const double pi = 3.14159265358979323846;
		std::vector<double> window(n);
		for (int i = 0; i < n; ++i)
			window[i] = 0.5 - 0.5 * std::cos(2.0 * pi * i / n);	// periodic hann
		std::vector<std::complex<double>> twiddle(n / 2);
		for (int i = 0; i < n / 2; ++i)
			twiddle[i] = std::polar(1.0, -2.0 * pi * i / n);
		std::vector<int> rev(n);
		for (int i = 0, bits = 10; i < n; ++i)
		{
			int r = 0;
			for (int b = 0; b < bits; ++b)
				if (i & (1 << b))
					r |= 1 << (bits - 1 - b);
			rev[i] = r;
		}

		// reflect padding like torch.stft(pad_mode="reflect")
		auto sample = [&](int64_t i) -> double {
			if (i < 0)
				i = -i;
			if (i >= count)
				i = 2 * (count - 1) - i;
			if (i < 0 || i >= count)
				return 0.0;
			return signal[static_cast<size_t>(i)];
		};

		const MelBank& bank = melBank();
		const double norm = 1.0 / std::sqrt(static_cast<double>(n));	// normalized="frame_length"
		std::vector<std::complex<double>> buffer(n);
		std::vector<float> mag(n / 2 + 1);
		for (int f = 0; f < frames; ++f)
		{
			const int64_t start = static_cast<int64_t>(f) * HOP - pad;
			for (int i = 0; i < n; ++i)
				buffer[i] = sample(start + i) * window[i];
			fft(buffer.data(), n, twiddle, rev);
			for (int k = 0; k <= n / 2; ++k)
				mag[k] = static_cast<float>(std::abs(buffer[k]) * norm);	// power=1
			float* row = &out[static_cast<size_t>(f) * MEL_BINS];
			for (int m = 0; m < MEL_BINS; ++m)
			{
				double acc = 0;
				const auto& w = bank.weights[m];
				for (size_t j = 0; j < w.size(); ++j)
					acc += static_cast<double>(mag[static_cast<size_t>(bank.first[m]) + j]) * w[j];
				row[m] = static_cast<float>(std::log1p(1000.0 * acc));
			}
		}
		return out;
	}

	// ====================================================================== peaks

	namespace
	{
		// beat_this.model.postprocessor.deduplicate_peaks(width=1): neighbours collapse to their mean
		std::vector<double> deduplicate(const std::vector<int>& peaks)
		{
			std::vector<double> result;
			if (peaks.empty())
				return result;
			double p = peaks[0];
			int c = 1;
			for (size_t i = 1; i < peaks.size(); ++i)
			{
				const double p2 = peaks[i];
				if (p2 - p <= 1)
				{
					++c;
					p += (p2 - p) / c;
				}
				else
				{
					result.push_back(p);
					p = p2;
					c = 1;
				}
			}
			result.push_back(p);
			return result;
		}

		std::vector<int> localMaxima(const std::vector<float>& x)
		{
			std::vector<int> peaks;
			const int n = static_cast<int>(x.size());
			for (int i = 0; i < n; ++i)
			{
				if (!(x[i] > 0.0f))
					continue;
				bool top = true;
				for (int j = std::max(0, i - 3); j <= std::min(n - 1, i + 3) && top; ++j)
					top = x[j] <= x[i];
				if (top)
					peaks.push_back(i);
			}
			return peaks;
		}
	}

	Beats pickPeaks(const Activations& act)
	{
		Beats out;
		const std::vector<double> beatFrames = deduplicate(localMaxima(act.beat));
		const std::vector<double> downFrames = deduplicate(localMaxima(act.downbeat));
		for (double f : beatFrames)
			out.beats.push_back(f / FPS);
		for (double f : downFrames)
		{
			const double t = f / FPS;
			if (out.beats.empty())
				break;
			// onto the nearest beat
			size_t best = 0;
			double bestDistance = 1e18;
			for (size_t i = 0; i < out.beats.size(); ++i)
			{
				const double d = std::abs(out.beats[i] - t);
				if (d < bestDistance)
				{
					bestDistance = d;
					best = i;
				}
			}
			out.downbeats.push_back(out.beats[best]);
		}
		std::sort(out.downbeats.begin(), out.downbeats.end());
		out.downbeats.erase(std::unique(out.downbeats.begin(), out.downbeats.end()), out.downbeats.end());
		return out;
	}

	void chunkStarts(int frames, int chunk, int border, std::vector<int>& starts)
	{
		starts.clear();
		const int hop = chunk - 2 * border;
		for (int s = -border; s < frames - border; s += hop)
			starts.push_back(s);
		if (starts.empty())
			starts.push_back(-border);
		if (frames > hop)
			starts.back() = frames - (chunk - border);	// the last chunk ends at the end of the piece
	}

	// ====================================================================== network

	namespace
	{
		constexpr int CHUNK = 1500, BORDER = 6;

		struct Cancelled
		{
		};

		// Runs the model over the whole spectrogram. Throws std::runtime_error / Cancelled.
		Activations runModel(const std::vector<float>& mel, int frames, const Runtime& runtime, const Progress& progress,
			const std::atomic<bool>* cancel)
		{
			const Ort& ort = loadOrt(runtime.dllPath);
			if (!ort.api)
				throw std::runtime_error(ort.error);
			const OrtApi* api = ort.api;

			OrtEnv* env = nullptr;
			OrtSessionOptions* options = nullptr;
			OrtSession* session = nullptr;
			OrtMemoryInfo* memory = nullptr;
			auto cleanup = [&] {
				if (memory)
					api->ReleaseMemoryInfo(memory);
				if (session)
					api->ReleaseSession(session);
				if (options)
					api->ReleaseSessionOptions(options);
				if (env)
					api->ReleaseEnv(env);
			};
			auto check = [&](OrtStatus* status) {
				if (status)
				{
					const std::string text = statusMessage(api, status);
					cleanup();
					throw std::runtime_error("ONNX Runtime: " + text);
				}
			};

			check(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "canvas-beat", &env));
			check(api->CreateSessionOptions(&options));
			const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
			check(api->SetIntraOpNumThreads(options, static_cast<int>(std::min(hardware, 8u))));
			check(api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
#ifdef _WIN32
			const std::wstring modelPath = fs::u8path(runtime.modelPath).wstring();
#else
			const std::string modelPath = runtime.modelPath;
#endif
			check(api->CreateSession(env, modelPath.c_str(), options, &session));
			check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));

			std::vector<int> starts;
			chunkStarts(frames, CHUNK, BORDER, starts);

			struct ChunkOut
			{
				std::vector<float> beat, downbeat;
			};
			std::vector<ChunkOut> outs(starts.size());
			// tensor names differ between exports ("spect", "input_spectrogram", ...): ask the model
			OrtAllocator* allocator = nullptr;
			check(api->GetAllocatorWithDefaultOptions(&allocator));
			char* inName = nullptr;
			char* outName0 = nullptr;
			char* outName1 = nullptr;
			check(api->SessionGetInputName(session, 0, allocator, &inName));
			check(api->SessionGetOutputName(session, 0, allocator, &outName0));
			check(api->SessionGetOutputName(session, 1, allocator, &outName1));
			const std::string inputName = inName, name0 = outName0, name1 = outName1;
			allocator->Free(allocator, inName);
			allocator->Free(allocator, outName0);
			allocator->Free(allocator, outName1);
			const bool swapped = name0.find("down") != std::string::npos;	// outputs listed downbeat first
			const char* inputNames[] = { inputName.c_str() };
			const char* outputNames[] = { name0.c_str(), name1.c_str() };

			for (size_t c = 0; c < starts.size(); ++c)
			{
				if (cancel && cancel->load())
				{
					cleanup();
					throw Cancelled{};
				}
				const int start = starts[c];
				const int s0 = std::max(start, 0), s1 = std::min(start + CHUNK, frames);
				const int left = std::max(0, -start), right = std::max(0, std::min(BORDER, start + CHUNK - frames));
				const int length = left + (s1 - s0) + right;
				std::vector<float> input(static_cast<size_t>(length) * MEL_BINS, 0.0f);
				std::memcpy(&input[static_cast<size_t>(left) * MEL_BINS], &mel[static_cast<size_t>(s0) * MEL_BINS],
					sizeof(float) * static_cast<size_t>(s1 - s0) * MEL_BINS);

				const int64_t shape[3] = { 1, length, MEL_BINS };
				OrtValue* in = nullptr;
				OrtValue* result[2] = { nullptr, nullptr };
				check(api->CreateTensorWithDataAsOrtValue(memory, input.data(), input.size() * sizeof(float), shape, 3,
					ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in));
				OrtStatus* status = api->Run(session, nullptr, inputNames, &in, 1, outputNames, 2, result);
				api->ReleaseValue(in);
				if (status)
				{
					const std::string text = statusMessage(api, status);
					cleanup();
					throw std::runtime_error("ONNX Runtime: " + text);
				}
				float* beat = nullptr;
				float* down = nullptr;
				api->GetTensorMutableData(result[swapped ? 1 : 0], reinterpret_cast<void**>(&beat));
				api->GetTensorMutableData(result[swapped ? 0 : 1], reinterpret_cast<void**>(&down));
				outs[c].beat.assign(beat, beat + length);
				outs[c].downbeat.assign(down, down + length);
				api->ReleaseValue(result[0]);
				api->ReleaseValue(result[1]);

				if (progress)
					progress(0.12f + 0.8f * static_cast<float>(c + 1) / static_cast<float>(starts.size()), "network");
			}
			cleanup();

			// stitch: drop the border of every chunk, earlier chunks win where they overlap (keep_first)
			Activations act;
			act.beat.assign(static_cast<size_t>(frames), -1000.0f);
			act.downbeat.assign(static_cast<size_t>(frames), -1000.0f);
			for (size_t c = starts.size(); c-- > 0;)
			{
				const auto& o = outs[c];
				const int usable = static_cast<int>(o.beat.size()) - 2 * BORDER;
				for (int j = 0; j < usable; ++j)
				{
					const int dest = starts[c] + BORDER + j;
					if (dest < 0 || dest >= frames)
						continue;
					act.beat[static_cast<size_t>(dest)] = o.beat[static_cast<size_t>(j + BORDER)];
					act.downbeat[static_cast<size_t>(dest)] = o.downbeat[static_cast<size_t>(j + BORDER)];
				}
			}
			return act;
		}
	}

	// ====================================================================== whole run

	Result detectSignal(const std::vector<float>& signal, const Runtime& runtime, const Progress& progress,
		const std::atomic<bool>* cancel, const BuildOptions& options, Activations* actOut)
	{
		const auto began = std::chrono::steady_clock::now();
		Result result;
		result.songSeconds = static_cast<double>(signal.size()) / SAMPLE_RATE;
		try
		{
			std::string why;
			if (!available(runtime, &why))
			{
				result.error = why;
				return result;
			}
			if (signal.size() < static_cast<size_t>(SAMPLE_RATE) * 5)
			{
				result.error = "The song is too short to find a tempo.";
				return result;
			}
			if (progress)
				progress(0.08f, "spectrogram");
			int frames = 0;
			const std::vector<float> mel = logMel(signal, frames);
			if (cancel && cancel->load())
				throw Cancelled{};
			if (progress)
				progress(0.12f, "network");

			Activations act = runModel(mel, frames, runtime, progress, cancel);
			result.beats = pickPeaks(act);
			if (actOut)
				*actOut = std::move(act);

			if (progress)
				progress(0.95f, "tempo");
			result.candidates = buildCandidates(result.beats, options);
			if (result.candidates.empty())
				result.error = "No steady beat was found in this song.";
			else
				result.ok = true;
		}
		catch (const Cancelled&)
		{
			result.error = "Cancelled.";
		}
		catch (const std::exception& e)
		{
			result.error = e.what();
		}
		result.tookSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
		if (progress)
			progress(1.0f, "done");
		return result;
	}

	Result detect(const std::string& musicFile, const Runtime& runtime, const Progress& progress, const std::atomic<bool>* cancel,
		const BuildOptions& options)
	{
		Result result;
		std::string why;
		if (!available(runtime, &why))
		{
			result.error = why;
			return result;
		}
		if (progress)
			progress(0.01f, "decode");
		int rate = 0;
		std::string error;
		std::vector<float> mono = decodeMono(musicFile, rate, error);
		if (mono.empty())
		{
			result.error = error;
			return result;
		}
		if (cancel && cancel->load())
		{
			result.error = "Cancelled.";
			return result;
		}
		if (progress)
			progress(0.04f, "resample");
		std::vector<float> signal = resample(mono, rate, SAMPLE_RATE);
		mono = {};
		return detectSignal(signal, runtime, progress, cancel, options);
	}
}
