#pragma once
#include "../../Tempo.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Beat / tempo detection with Beat This! (CPJKU/beat_this, ISMIR 2024, MIT) run through ONNX Runtime.
//
//   audio -> mono 22.05 kHz -> log-mel (the model's own front-end) -> network (1500-frame chunks)
//         -> beat + downbeat peaks -> tempo map (constant parts, steps, linear ramps, time signatures)
//
// ONNX Runtime is loaded at run time (onnxruntime.dll next to the model under res/), so the editor
// still starts without it: available() just says why detection can't run.
//
// The tempo-map part (buildCandidates / toTiming) is plain maths on beat lists and has no
// dependencies, so the tests drive it with synthetic beats.
namespace Canvas::BeatDetect
{
	// ------------------------------------------------------------------ where things live

	struct Runtime
	{
		std::string dllPath;	// onnxruntime.dll
		std::string modelPath;	// beat_this.onnx
	};

	// <appDir>/res/ort/onnxruntime.dll and <appDir>/res/models/beat_this.onnx (appDir empty = the exe's folder).
	Runtime locate(const std::string& appDir = {});
	// Loads the DLL and checks the API version; `why` says what is missing otherwise.
	bool available(const Runtime& runtime, std::string* why = nullptr);

	// ------------------------------------------------------------------ front-end (model input)

	constexpr int SAMPLE_RATE = 22050;
	constexpr int HOP = 441;			// 50 frames per second
	constexpr int FFT_SIZE = 1024;
	constexpr int MEL_BINS = 128;
	constexpr double FPS = 50.0;

	// File -> mono floats at the file's rate. Empty + `error` on failure.
	std::vector<float> decodeMono(const std::string& path, int& sampleRate, std::string& error);
	// Band-limited (Kaiser sinc) resampling.
	std::vector<float> resample(const std::vector<float>& in, int fromRate, int toRate);
	// Row-major (frames x 128), log1p(1000 * mel magnitude), exactly beat_this.preprocessing.LogMelSpect.
	std::vector<float> logMel(const std::vector<float>& signal22k, int& frames);

	// ------------------------------------------------------------------ network + peak picking

	struct Activations
	{
		std::vector<float> beat, downbeat;	// logits, one per 20 ms frame
	};

	struct Beats
	{
		std::vector<double> beats, downbeats;	// seconds
	};

	// beat_this.model.postprocessor "minimal": local maxima over +-3 frames with logit > 0, adjacent
	// peaks merged, downbeats moved onto the nearest beat.
	Beats pickPeaks(const Activations& act);

	// Chunking exactly like beat_this.inference (chunk 1500, border 6, last chunk shifted left).
	void chunkStarts(int frames, int chunk, int border, std::vector<int>& starts);

	// ------------------------------------------------------------------ tempo maps

	// One stretch of the chart's beat grid. Beats count from chart tick 0 (tick = beat * 480).
	// bpmStart == bpmEnd: constant tempo. Otherwise the BPM moves linearly with the beat position.
	struct TempoPiece
	{
		int startBeat{ 0 };
		int beats{ 0 };
		double bpmStart{ 120 };
		double bpmEnd{ 120 };
		bool ramp() const { return std::abs(bpmStart - bpmEnd) > 1e-6; }
	};

	struct SigChange
	{
		int measure{ 0 };	// 0-based, like Score::timeSignatures
		int numerator{ 4 };	// beats (quarter notes) in the bar
	};

	enum class Kind { Best, HalfTime, DoubleTime, Simple };

	struct Candidate
	{
		Kind kind{ Kind::Best };
		std::string description;			// short English text (the UI has its own wording too)
		std::vector<TempoPiece> pieces;		// from tick 0
		std::vector<SigChange> sigs;		// at least one
		double startSec{ 0 };				// song time of chart tick 0
		int totalBeats{ 0 };
		double minBpm{ 0 }, maxBpm{ 0 }, firstBpm{ 0 };
		int tempoChanges{ 0 };				// tempo steps and ramps after the first piece
		int ramps{ 0 };
		int sigChanges{ 0 };
		double rmsMs{ 0 }, maxMs{ 0 };		// how far the detected beats sit from this grid
	};

	struct BuildOptions
	{
		double sigma{ 0.010 };		// beat position noise (s); bigger = fewer tempo changes
		double penConst{ 30 };		// cost of starting a constant stretch
		double penRamp{ 70 };		// cost of a ramp (needs to explain more)
		int minConstBeats{ 16 };
		int minRampBeats{ 16 };
		double mergeMedianSec{ 0.018 };	// neighbours one steady tempo fits this well are merged
		double minRampPercent{ 4 };	// smaller moves are written as a constant tempo
		int leadInBars{ 1 };		// empty bars before the first downbeat (as far as the song allows)
	};

	// Up to 3 tempo maps: best guess, half/double time, simple. Empty when there are too few beats.
	std::vector<Candidate> buildCandidates(const Beats& beats, const BuildOptions& options = {});

	// What goes into a chart.
	struct Timing
	{
		std::vector<MikuMikuWorld::Tempo> tempos;
		std::map<int, MikuMikuWorld::TimeSignature> signatures;
		double musicOffsetMs{ 0 };	// Score metadata: audio time = chart time - offset
	};
	// Ramps become steps every `rampStepTicks`.
	Timing toTiming(const Candidate& candidate, int rampStepTicks = 120);

	// Song time of every beat of the grid (beat 0 = chart tick 0); for checks and graphs.
	std::vector<double> gridTimes(const Candidate& candidate);

	// ------------------------------------------------------------------ whole run

	struct Result
	{
		bool ok{ false };
		std::string error;
		Beats beats;
		std::vector<Candidate> candidates;
		double songSeconds{ 0 };
		double tookSeconds{ 0 };
	};

	// progress(0..1, what). Runs on the calling thread; poll `cancel` from another one.
	using Progress = std::function<void(float, const char*)>;
	Result detect(const std::string& musicFile, const Runtime& runtime, const Progress& progress = {},
		const std::atomic<bool>* cancel = nullptr, const BuildOptions& options = {});

	// Same, from a ready 22.05 kHz mono signal (tests / dumps). `act` receives the logits when given.
	Result detectSignal(const std::vector<float>& signal22k, const Runtime& runtime, const Progress& progress,
		const std::atomic<bool>* cancel, const BuildOptions& options, Activations* act = nullptr);
}
