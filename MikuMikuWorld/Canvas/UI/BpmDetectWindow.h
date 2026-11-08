#pragma once
#include "../Core/BeatDetect.h"
#include "../Core/BpmTools.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace MikuMikuWorld
{
	class ScoreContext;
}

// Chart > Detect BPM... : finds the song's tempo with Beat This! (Canvas/Core/BeatDetect) on a
// background thread and lists up to 3 tempo maps in a "Select BPM" popup (graph, changes, ramps,
// time signatures). Apply writes tempo changes + time signatures + music offset as ONE undo step.
// Chart > Interpolate BPM... : a small dialog that ramps the BPM between two values.
//
// Self-contained: it applies its result to the chart itself.
namespace Canvas
{
	class BpmDetectWindow
	{
	public:
		BpmDetectWindow() = default;
		~BpmDetectWindow();
		BpmDetectWindow(const BpmDetectWindow&) = delete;
		BpmDetectWindow& operator=(const BpmDetectWindow&) = delete;

		// Starts the detection and opens the popup.
		void open(const std::string& musicFile);
		// From the selected notes' range, else the cursor and four beats on.
		void openInterpolate(const MikuMikuWorld::ScoreContext& context);
		// Call every frame (outside any menu). True on the frame it changed the chart's tempo map:
		// then refresh the timeline / issues like after any timing edit.
		bool update(MikuMikuWorld::ScoreContext& context);

	private:
		struct Job
		{
			std::atomic<float> progress{ 0.0f };
			std::atomic<int> stage{ 0 };
			std::atomic<bool> cancel{ false };
			std::atomic<bool> done{ false };
			Canvas::BeatDetect::Result result;
		};
		enum class State { Idle, Running, Failed, Ready };

		State state{ State::Idle };
		bool openRequested{ false }, interpolateRequested{ false };
		std::string musicFile, error;
		std::shared_ptr<Job> job;
		std::thread worker;
		std::vector<Canvas::BeatDetect::Candidate> candidates;
		int selected{ 0 };
		bool keepNoteTicks{ true };

		// Interpolate BPM
		float fromBeat{ 0 }, toBeat{ 4 }, startBpm{ 120 }, endBpm{ 120 };
		int stepIndex{ 1 }, curveIndex{ 0 };

		void start();
		void stop();
		void drawSelect(MikuMikuWorld::ScoreContext& context, bool& changed);
		void drawCandidate(int index, float height);
		void drawInterpolate(MikuMikuWorld::ScoreContext& context, bool& changed);
	};
}
