#pragma once
#include "../../Rendering/Framebuffer.h"
#include "FfmpegInstaller.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	class ScoreContext;
	class ScorePreviewWindow;
	class ScoreEditorTimeline;
	class Renderer;
}

// File > Export Video: renders the preview frame by frame into an offscreen framebuffer and pipes
// raw RGBA frames into ffmpeg, together with the song and (optionally) the hit sounds, which are
// mixed into a temporary WAV first. Rendering happens a few frames per UI frame so the editor
// stays responsive and the export can be cancelled.
namespace Canvas::Video
{
	struct Settings
	{
		std::string output;
		int width{ 1920 };
		int height{ 1080 };
		int fps{ 60 };
		bool hitSounds{ true };
		bool fromCursor{ false };	// start at the timeline cursor instead of the beginning
		float tailSeconds{ 2.0f };	// keep rolling after the last note
	};

	// ffmpeg command line for the given inputs (exposed for testing / the log)
	std::string buildFfmpegCommand(const std::string& ffmpeg, const Settings& settings, double durationSeconds,
		const std::string& musicFile, double musicStartSeconds, const std::string& hitSoundsWav, const std::string& logFile);

	class ExportWindow
	{
	public:
		void open(const MikuMikuWorld::ScoreContext& context);
		void update(MikuMikuWorld::ScoreContext& context, MikuMikuWorld::ScorePreviewWindow& preview,
			MikuMikuWorld::Renderer* renderer, MikuMikuWorld::ScoreEditorTimeline& timeline);
		bool isRunning() const { return state == State::Running; }
		// --export-video: the export with the saved settings, no dialog
		void startAutomated(MikuMikuWorld::ScoreContext& context, MikuMikuWorld::ScoreEditorTimeline& timeline, const std::string& output);
		const std::string& lastResult() const { return resultMessage; }

	private:
		enum class State { Closed, Settings, Running, Finished };

		State state{ State::Closed };
		bool openRequested{ false };
		Settings settings;
		std::string ffmpegStatus;
		bool ffmpegFound{ false };

		// running
		FILE* pipe{ nullptr };
		// created at export start: a default-constructed Framebuffer has no GL objects
		std::unique_ptr<MikuMikuWorld::Framebuffer> framebuffer;
		std::vector<uint8_t> pixels;
		int frame{ 0 };
		int frameCount{ 0 };
		double startSeconds{ 0 };
		double leadIn{ 0 };		// seconds of intro before chart time 0 (startSeconds is -leadIn then)
		int savedTick{ 0 };
		double startedAt{ 0 };
		std::string hitSoundsWav;
		std::string logFile;
		std::string resultMessage;
		bool resultOk{ false };
		std::string warning;			// shown with the result (e.g. hit sounds that could not be mixed)
		FfmpegInstaller installer;
		bool exportAfterInstall{ false };	// "Install ffmpeg" was pressed: export once it is there

		void drawOverlay(MikuMikuWorld::ScoreContext& context, MikuMikuWorld::ScorePreviewWindow& preview, bool hud);
		void drawInstaller(MikuMikuWorld::ScoreContext& context, MikuMikuWorld::ScoreEditorTimeline& timeline);
		bool start(MikuMikuWorld::ScoreContext& context, MikuMikuWorld::ScoreEditorTimeline& timeline);
		void renderSome(MikuMikuWorld::ScoreContext& context, MikuMikuWorld::ScorePreviewWindow& preview, MikuMikuWorld::Renderer* renderer);
		void finish(MikuMikuWorld::ScoreContext& context, bool cancelled);
		void cleanupTemporaryFiles();
	};

	// Mixes the chart's hit sounds from `soundDir` (res/sound/NN/*.mp3), and with `effects` the
	// skill / FEVER / ALL PERFECT sounds, into a 44.1 kHz stereo WAV covering
	// [startSeconds, startSeconds + duration) at config.videoSeVolume. Returns false when nothing
	// could be decoded.
	bool renderHitSounds(const MikuMikuWorld::ScoreContext& context, const std::string& soundDir, double startSeconds,
		double durationSeconds, const std::string& wavPath, std::string& error, bool hits = true, bool effects = false);
}
