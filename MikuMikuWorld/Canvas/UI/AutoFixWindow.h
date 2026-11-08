#pragma once
#include "../Core/ChartAutoFix.h"
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace MikuMikuWorld
{
	class ScoreContext;

	// Edit > Auto Fix Chart Issues...: scans for what can be fixed on its own, lets you pick which
	// groups to fix (and whether hi-speed fixing may rewrite odd-but-readable speeds), then fixes a
	// copy of the chart on a worker thread, so the editor keeps drawing while it works. The finished
	// chart comes back through update() once and the editor applies it as a single undo step.
	class AutoFixWindow
	{
	public:
		struct Output
		{
			Score score;
			Canvas::Fixes::AutoFixReport report;
		};

		void open(const ScoreContext& context);
		// for automated runs: press Fix as soon as the scan is done
		void fixWhenReady() { startWhenReady = true; }
		std::optional<Output> update(ScoreContext& context);

	private:
		enum class State { Scanning, Ready, Running, Done };

		bool openRequested{ false };
		bool closeRequested{ false };
		bool startWhenReady{ false };
		State state{ State::Ready };
		double musicSeconds{ -1.0 };
		Canvas::Fixes::AutoFixPreview preview;
		Canvas::Fixes::AutoFixReport report;
		std::future<Canvas::Fixes::AutoFixPreview> scanJob;
		std::future<Output> fixJob;

		// the stage label the worker is on
		std::mutex stageLock;
		std::string stage;
		std::string stageText();
		void setStage(const std::string& text);
		void startFix(const ScoreContext& context);
	};
}
