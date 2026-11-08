#include "../../Audio/AudioManager.h"
#include "../../UI.h"
#include "VideoExporter.h"
#include "../Core/HitSounds.h"
#include "../../Application.h"
#include "../../ApplicationConfiguration.h"
#include "../../IconsFontAwesome5.h"
#include "../../ScoreContext.h"
#include "../../ScorePreview.h"
#include "../../ScoreEditorTimeline.h"
#include "../../Constants.h"
#include "../../ImGui/imgui_stdlib.h"
#include "../../ImGui/imgui_internal.h"
#include "../../ImGui/imgui_impl_opengl3.h"
#include "../Preview/EffectAudio.h"
#include "../Preview/LiveOverlay.h"
#include "../Preview/CustomSkins.h"
#include "../Preview/IntroOverlay.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using namespace MikuMikuWorld;

namespace Canvas::Video
{
	namespace
	{
		constexpr int SAMPLE_RATE = 44100;

		// the same per-sound levels the editor plays them at (AudioManager::loadSoundEffects)
		float seGain(const std::string& name)
		{
			static const std::map<std::string, float> gains{
				{ SE_PERFECT, 0.75f }, { SE_FLICK, 0.75f }, { SE_TICK, 0.90f }, { SE_FRICTION, 0.80f }, { SE_CONNECT, 0.70f },
				{ SE_CRITICAL_TAP, 0.75f }, { SE_CRITICAL_FLICK, 0.80f }, { SE_CRITICAL_TICK, 0.92f }, { SE_CRITICAL_FRICTION, 0.82f },
				{ SE_CRITICAL_CONNECT, 0.70f } };
			auto it = gains.find(name);
			return it == gains.end() ? 0.75f : it->second;
		}

		bool decode(const std::string& path, std::vector<float>& out)
		{
			return Canvas::Preview::decodeAudioFile(path, out);
		}

		bool writeWav(const std::string& path, const std::vector<float>& stereo)
		{
			std::ofstream file(fs::u8path(path), std::ios::binary);
			if (!file)
				return false;

			auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
			auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
			const uint32_t dataBytes = static_cast<uint32_t>(stereo.size() * 2);
			file.write("RIFF", 4); u32(36 + dataBytes);
			file.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
			u32(SAMPLE_RATE); u32(SAMPLE_RATE * 4); u16(4); u16(16);
			file.write("data", 4); u32(dataBytes);

			std::vector<int16_t> samples(stereo.size());
			for (size_t i = 0; i < stereo.size(); ++i)
				samples[i] = static_cast<int16_t>(std::clamp(stereo[i], -1.0f, 1.0f) * 32767.0f);
			file.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(samples.size() * 2));
			return static_cast<bool>(file);
		}

		std::string seconds(double value) { return IO::formatString("%.3f", value); }

		bool ffmpegAvailable(const std::string& ffmpeg)
		{
			if (ffmpeg.empty())
				return false;
			if (ffmpeg.find('/') != std::string::npos || ffmpeg.find('\\') != std::string::npos)
				return IO::File::exists(ffmpeg);
			return IO::commandExists(ffmpeg);
		}

		double lastNoteSeconds(const Score& score)
		{
			int last = 0;
			for (const auto& [id, note] : score.notes)
				last = std::max(last, note.tick);
			return accumulateDuration(last, TICKS_PER_BEAT, score.tempoChanges);
		}

		std::string tailOf(const std::string& path, size_t lines)
		{
			std::ifstream in(fs::u8path(path));
			std::vector<std::string> all;
			std::string line;
			while (std::getline(in, line))
				if (!line.empty())
					all.push_back(line);
			std::string out;
			for (size_t i = all.size() > lines ? all.size() - lines : 0; i < all.size(); ++i)
				out += all[i] + "\n";
			return out;
		}
	}

	bool renderHitSounds(const ScoreContext& context, const std::string& soundDir, double startSeconds, double durationSeconds,
		const std::string& wavPath, std::string& error, bool hits, bool effects)
	{
		const auto schedule = hits ? scheduleHitSounds(context.score) : std::vector<HitSound>{};
		const size_t frames = static_cast<size_t>(std::ceil(durationSeconds * SAMPLE_RATE));
		std::vector<float> mix(frames * 2, 0.0f);
		std::map<std::string, std::vector<float>> cache;
		// the export's own volume: a muted editor mixer used to leave the video silent
		const float master = config.videoSeVolume;

		for (const HitSound& sound : schedule)
		{
			auto it = cache.find(sound.name);
			if (it == cache.end())
			{
				std::vector<float> samples;
				decode(Canvas::CustomSkins::soundFile(config.seProfileIndex, sound.name), samples);
				it = cache.emplace(sound.name, std::move(samples)).first;
			}
			const std::vector<float>& sample = it->second;
			if (sample.empty())
				continue;

			const float gain = seGain(sound.name) * sound.volume * master;
			const size_t sampleFrames = sample.size() / 2;
			const double begin = sound.time - startSeconds;

			if (sound.loopUntil > sound.time)
			{
				// hold loop: repeat the sample from the head to the end, short fade at the end
				const double end = sound.loopUntil - startSeconds;
				if (end <= 0)
					continue;
				const size_t from = static_cast<size_t>(std::max(0.0, begin) * SAMPLE_RATE);
				const size_t to = std::min(frames, static_cast<size_t>(end * SAMPLE_RATE));
				const size_t fade = SAMPLE_RATE / 100;
				const long long phase0 = static_cast<long long>(std::llround(begin * SAMPLE_RATE));
				for (size_t f = from; f < to; ++f)
				{
					const size_t s = static_cast<size_t>((static_cast<long long>(f) - phase0) % static_cast<long long>(sampleFrames));
					const float envelope = to - f < fade ? static_cast<float>(to - f) / fade : 1.0f;
					mix[f * 2] += sample[s * 2] * gain * envelope;
					mix[f * 2 + 1] += sample[s * 2 + 1] * gain * envelope;
				}
				continue;
			}

			if (begin + static_cast<double>(sampleFrames) / SAMPLE_RATE <= 0 || begin >= durationSeconds)
				continue;
			const long long offset = static_cast<long long>(std::llround(begin * SAMPLE_RATE));
			for (size_t s = 0; s < sampleFrames; ++s)
			{
				const long long f = offset + static_cast<long long>(s);
				if (f < 0)
					continue;
				if (f >= static_cast<long long>(frames))
					break;
				mix[f * 2] += sample[s * 2] * gain;
				mix[f * 2 + 1] += sample[s * 2 + 1] * gain;
			}
		}

		bool decodedAny = false;
		for (const auto& [name, samples] : cache)
			decodedAny |= !samples.empty();
		if (!decodedAny && !schedule.empty())
		{
			error = "Could not decode the hit sounds in " + soundDir;
			return false;
		}

		// skills, FEVER and ALL PERFECT on top
		if (effects)
		{
			const auto options = ScorePreviewWindow::overlayOptions(true);
			for (const auto& cue : Canvas::Preview::effectCues(context.score, options))
			{
				const std::vector<float>& sample = Canvas::Preview::effectPcm(cue.sound);
				const long long offset = static_cast<long long>(std::llround((cue.time - startSeconds) * SAMPLE_RATE));
				const float gain = master * 0.9f;
				for (size_t i = 0; i < sample.size() / 2; ++i)
				{
					const long long f = offset + static_cast<long long>(i);
					if (f < 0)
						continue;
					if (f >= static_cast<long long>(frames))
						break;
					mix[f * 2] += sample[i * 2] * gain;
					mix[f * 2 + 1] += sample[i * 2 + 1] * gain;
				}
			}
		}

		// dense charts stack many sounds: a soft knee above 0.8 instead of turning the whole track
		// down (one stacked moment used to make every hit sound faint)
		for (float& v : mix)
		{
			const float level = std::abs(v);
			if (level > 0.8f)
				v = std::copysign(0.8f + 0.19f * std::tanh((level - 0.8f) / 0.19f), v);
		}

		if (!writeWav(wavPath, mix))
		{
			error = "Could not write " + wavPath;
			return false;
		}
		return true;
	}

	std::string buildFfmpegCommand(const std::string& ffmpeg, const Settings& settings, double durationSeconds,
		const std::string& musicFile, double musicStartSeconds, const std::string& hitSoundsWav, const std::string& logFile)
	{
		std::string cmd = IO::shellQuote(ffmpeg) + " -hide_banner -loglevel error -y";
		cmd += IO::formatString(" -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i -", settings.width, settings.height, settings.fps);

		int inputs = 1, musicInput = -1, hitsInput = -1;
		if (!musicFile.empty())
		{
			if (musicStartSeconds > 0)
				cmd += " -ss " + seconds(musicStartSeconds);
			cmd += " -i " + IO::shellQuote(musicFile);
			musicInput = inputs++;
		}
		if (!hitSoundsWav.empty())
		{
			cmd += " -i " + IO::shellQuote(hitSoundsWav);
			hitsInput = inputs++;
		}

		// frames come bottom-up from OpenGL
		std::string graph = "[0:v]vflip[v]";
		if (musicInput >= 0)
		{
			graph += IO::formatString(";[%d:a]", musicInput);
			if (musicStartSeconds < 0)
				graph += IO::formatString("adelay=%d:all=1,", static_cast<int>(std::lround(-musicStartSeconds * 1000)));
			graph += IO::formatString("volume=%.3f[music]", config.videoMusicVolume);
		}
		if (hitsInput >= 0)
			graph += IO::formatString(";[%d:a]anull[hits]", hitsInput);
		if (musicInput >= 0 && hitsInput >= 0)
			// amix halves each input (normalize=0 needs ffmpeg 4.4+): double it back, then limit
			graph += ";[music][hits]amix=inputs=2:duration=longest:dropout_transition=0,volume=2,alimiter=limit=0.97:level=false[a]";
		else if (musicInput >= 0)
			graph += ";[music]anull[a]";
		else if (hitsInput >= 0)
			graph += ";[hits]anull[a]";

		cmd += " -filter_complex " + IO::shellQuote(graph) + " -map \"[v]\"";
		if (musicInput >= 0 || hitsInput >= 0)
			cmd += " -map \"[a]\" -c:a aac -b:a 192k";
		else
			cmd += " -an";

		// 4K: x264's "superfast" keeps a 4K60 export from taking forever; the picture stays crf 18
		const bool huge = static_cast<long long>(settings.width) * settings.height > 2560LL * 1440LL;
		cmd += std::string(" -c:v libx264 -preset ") + (huge ? "superfast" : "veryfast") + " -crf 18 -pix_fmt yuv420p -movflags +faststart";
		cmd += " -t " + seconds(durationSeconds);
		cmd += " " + IO::shellQuote(settings.output);
		if (!logFile.empty())
			cmd += " 2> " + IO::shellQuote(logFile);
		return cmd;
	}

	void ExportWindow::open(const ScoreContext& context)
	{
		if (state == State::Running)
			return;

		settings.width = config.videoWidth;
		settings.height = config.videoHeight;
		settings.fps = config.videoFps;
		settings.hitSounds = config.videoHitSounds;
		if (settings.output.empty())
		{
			const std::string base = context.workingData.filename.empty()
				? (context.workingData.title.empty() ? std::string("chart") : context.workingData.title)
				: IO::File::getFilenameWithoutExtension(context.workingData.filename);
			const std::string folder = context.workingData.filename.empty() ? std::string{} : fs::u8path(context.workingData.filename).parent_path().u8string();
			// never "<name>.mp4": next to a song that name is its 2DMV, which the export would overwrite
			const std::string name = base + " (preview).mp4";
			settings.output = folder.empty() ? name : (fs::u8path(folder) / name).u8string();
		}

		ffmpegFound = ffmpegAvailable(config.ffmpegPath);
		state = State::Settings;
		openRequested = true;
	}

	void ExportWindow::startAutomated(ScoreContext& context, ScoreEditorTimeline& timeline, const std::string& output)
	{
		open(context);
		openRequested = false;
		settings.output = output;
		settings.fromCursor = context.currentTick > 0;	// --tick picks where the video starts
		resultMessage.clear();
		state = start(context, timeline) ? State::Running : State::Finished;
	}

	bool ExportWindow::start(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		settings.width = std::max(2, settings.width & ~1);
		settings.height = std::max(2, settings.height & ~1);
		config.videoWidth = settings.width;
		config.videoHeight = settings.height;
		config.videoFps = settings.fps;
		config.videoHitSounds = settings.hitSounds;

		if (timeline.isPlaying())
			timeline.setPlaying(context, false);

		startSeconds = settings.fromCursor ? context.getTimeAtCurrentTick() : 0.0;
		// the start animation plays first: the video begins before the chart, which then waits for it
		leadIn = (config.pvDrawIntro && config.videoDrawUi && !settings.fromCursor) ? Canvas::Preview::INTRO_LEAD_IN : 0.0;
		startSeconds -= leadIn;
		double end = lastNoteSeconds(context.score) + settings.tailSeconds;
		// keep rolling until the ALL PERFECT has played out
		if (config.videoDrawUi && config.pvDrawAllPerfect && !context.score.notes.empty())
			end = std::max(end, lastNoteSeconds(context.score) + Canvas::Preview::ALL_PERFECT_DELAY + std::max(Canvas::Preview::ALL_PERFECT_DURATION, Canvas::Preview::AP_FADE_START + Canvas::Preview::AP_FADE_LENGTH) + 0.2);
		const std::string& music = context.workingData.musicFilename;
		const bool hasMusic = !music.empty() && IO::File::exists(music);
		const double offset = context.workingData.musicOffset / 1000.0;
		if (context.score.metadata.endTick >= 0)
		{
			// the chart's own end wins over the song's (the ALL PERFECT still finishes)
			end = accumulateDuration(context.score.metadata.endTick, TICKS_PER_BEAT, context.score.tempoChanges);
			if (config.videoDrawUi && config.pvDrawAllPerfect && !context.score.notes.empty())
				end = std::max(end, lastNoteSeconds(context.score) + Canvas::Preview::ALL_PERFECT_DELAY + std::max(Canvas::Preview::ALL_PERFECT_DURATION, Canvas::Preview::AP_FADE_START + Canvas::Preview::AP_FADE_LENGTH) + 0.2);
		}
		else if (hasMusic && context.audio.isMusicInitialized())
			end = std::max(end, offset + context.audio.getMusicLength());
		const double duration = std::max(1.0, end - startSeconds);
		frameCount = static_cast<int>(std::ceil(duration * settings.fps));

		const fs::path temp = fs::temp_directory_path();
		const std::string stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		logFile = (temp / ("canvas_video_" + stamp + ".log")).u8string();
		hitSoundsWav.clear();

		warning.clear();
		const bool effects = config.videoEffectSounds && config.videoDrawUi;
		if (settings.hitSounds || effects)
		{
			std::string error;
			const std::string soundDir = Canvas::CustomSkins::soundDir(config.seProfileIndex);
			const std::string wav = (temp / ("canvas_hits_" + stamp + ".wav")).u8string();
			if (renderHitSounds(context, soundDir, startSeconds, duration, wav, error, settings.hitSounds, effects))
				hitSoundsWav = wav;
			else
				warning = std::string(getString("video_hits_failed")) + " " + error;
		}
		Canvas::Preview::setMovieFrameWaiting(true);

		// chart time t plays music position t - offset
		const std::string cmd = buildFfmpegCommand(config.ffmpegPath, settings, duration, hasMusic ? music : std::string{},
			startSeconds - offset, hitSoundsWav, logFile);

#ifndef _WIN32
		std::signal(SIGPIPE, SIG_IGN);	// a dead ffmpeg must not take the editor with it
#endif
		pipe = IO::openProcessForWriting(cmd);
		if (!pipe)
		{
			resultOk = false;
			resultMessage = getString("video_ffmpeg_failed_to_start");
			cleanupTemporaryFiles();
			return false;
		}

		pixels.resize(static_cast<size_t>(settings.width) * settings.height * 4);
		if (!framebuffer)
			framebuffer = std::make_unique<Framebuffer>(settings.width, settings.height);
		else
			framebuffer->resize(settings.width, settings.height);
		frame = 0;
		savedTick = context.currentTick;
		startedAt = ImGui::GetTime();
		context.scorePreviewDrawData.effectView.reset();
		return true;
	}

	void ExportWindow::renderSome(ScoreContext& context, ScorePreviewWindow& preview, Renderer* renderer)
	{
		using clock = std::chrono::steady_clock;
		const auto deadline = clock::now() + std::chrono::milliseconds(40);

		if (config.drawBackground && preview.getBackground().shouldUpdate(context.workingData.jacket))
			preview.getBackground().update(renderer, context.workingData.jacket);
		if (!context.scorePreviewDrawData.effectView.isInitialized())
			context.scorePreviewDrawData.effectView.init();

		while (frame < frameCount && clock::now() < deadline)
		{
			const double t = startSeconds + static_cast<double>(frame) / settings.fps;
			// before the chart starts it holds still at tick 0 while the intro plays
			context.currentTick = accumulateTicks(static_cast<float>(std::max(0.0, t)), TICKS_PER_BEAT, context.score.tempoChanges);
			Canvas::Preview::setIntroClock(leadIn > 0.0 ? static_cast<double>(frame) / settings.fps : -1.0);
			context.scorePreviewDrawData.effectView.update(context);

			// every video frame gets its own 2DMV frame (the live preview shows the newest decoded one)
			preview.movieWaitsForFrames = true;
			const bool rendered = preview.renderFrame(context, renderer, settings.width, settings.height, *framebuffer);
			preview.movieWaitsForFrames = false;
			if (rendered && (config.videoDrawUi || config.pvDrawAllPerfect || !context.score.animation.effects.empty()))
				drawOverlay(context, preview, config.videoDrawUi);
			if (!rendered)
			{
				resultOk = false;
				resultMessage = "The preview renderer is not available.";
				finish(context, true);
				return;
			}

			framebuffer->bind();
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, settings.width, settings.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			framebuffer->unblind();

			if (std::fwrite(pixels.data(), 1, pixels.size(), pipe) != pixels.size())
			{
				resultOk = false;
				resultMessage = getString("video_ffmpeg_stopped");
				finish(context, true);
				return;
			}
			++frame;
		}

		Canvas::Preview::setIntroClock(-1.0);
		if (frame >= frameCount)
			finish(context, false);
	}

	// The live overlay (score, combo, life, skills, FEVER, ALL PERFECT...) burnt into the frame: the
	// same ImGui drawing the preview window uses, rendered into the export framebuffer.
	void ExportWindow::drawOverlay(ScoreContext& context, ScorePreviewWindow& preview, bool hud)
	{
		const ImVec2 size(static_cast<float>(settings.width), static_cast<float>(settings.height));
		ImDrawList list(ImGui::GetDrawListSharedData());
		list._ResetForNewFrame();
		list.PushTexture(ImGui::GetIO().Fonts->TexRef);
		list.PushClipRect(ImVec2(0, 0), size, false);
		if (hud)
			Canvas::Preview::drawLiveOverlay(&list, ImVec2(0, 0), size, context, ScorePreviewWindow::overlayOptions(true), preview.projector(ImVec2(0, 0), size));
		Canvas::Preview::drawAnimationEffects(&list, ImVec2(0, 0), size, context);
		if (list.CmdBuffer.Size > 0 && list.CmdBuffer.back().ElemCount == 0)
			list.CmdBuffer.pop_back();
		if (list.VtxBuffer.Size == 0)
			return;

		ImDrawData data;
		data.Valid = true;
		data.CmdListsCount = 1;
		data.CmdLists.push_back(&list);
		data.TotalVtxCount = list.VtxBuffer.Size;
		data.TotalIdxCount = list.IdxBuffer.Size;
		data.DisplayPos = ImVec2(0, 0);
		data.DisplaySize = size;
		data.FramebufferScale = ImVec2(1, 1);
		data.OwnerViewport = ImGui::GetMainViewport();
		data.Textures = &ImGui::GetPlatformIO().Textures;
		framebuffer->bind();
		ImGui_ImplOpenGL3_RenderDrawData(&data);
		framebuffer->unblind();
	}

	// "Install ffmpeg": shown while ffmpeg is missing (or installing); when it is done the export
	// starts by itself if the button was pressed from here
	void ExportWindow::drawInstaller(ScoreContext& context, ScoreEditorTimeline& timeline)
	{
		using S = FfmpegInstaller::State;
		const S installState = installer.state();
		if (installState == S::Done)
		{
			config.ffmpegPath = installer.installedPath();
			ffmpegFound = ffmpegAvailable(config.ffmpegPath);
			installer.acknowledge();
			if (ffmpegFound && exportAfterInstall && !settings.output.empty())
			{
				exportAfterInstall = false;
				resultMessage.clear();
				state = start(context, timeline) ? State::Running : State::Finished;
				return;
			}
		}
		if (installState == S::Failed)
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s %s", getString("video_ffmpeg_install_failed"), installer.message().c_str());
			ImGui::PopTextWrapPos();
		}
		if (installer.busy())
		{
			const double mb = installer.downloadedBytes() / 1048576.0;
			const float spin = static_cast<float>(std::fmod(ImGui::GetTime() * 0.6, 1.0));
			ImGui::ProgressBar(installer.state() == S::Extracting ? 1.0f : spin, ImVec2(-1, 0),
				IO::formatString("%s  %.1f MB", installer.state() == S::Extracting ? "..." : "", mb).c_str());
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", getString("video_installing_ffmpeg"));
			ImGui::PopTextWrapPos();
		}
		else if (!ffmpegFound)
		{
			if (ImGui::Button(IO::concat(ICON_FA_DOWNLOAD, getString("video_install_ffmpeg"), " ").c_str(), ImVec2(-1, 0)))
			{
				exportAfterInstall = true;
				installer.start();
			}
			UI::tooltip(FfmpegInstaller::sourceUrl().c_str());
		}
	}

	void ExportWindow::cleanupTemporaryFiles()
	{
		std::error_code error;
		if (!hitSoundsWav.empty())
			fs::remove(fs::u8path(hitSoundsWav), error);
		hitSoundsWav.clear();
	}

	void ExportWindow::finish(ScoreContext& context, bool cancelled)
	{
		Canvas::Preview::setMovieFrameWaiting(false);
		const int status = IO::closeProcess(pipe);
		pipe = nullptr;
		context.currentTick = savedTick;
		context.scorePreviewDrawData.effectView.reset();
		cleanupTemporaryFiles();

		const std::string log = tailOf(logFile, 8);
		std::error_code error;
		fs::remove(fs::u8path(logFile), error);

		if (cancelled)
		{
			if (resultMessage.empty())
				resultMessage = getString("video_cancelled");
			if (!log.empty())
				resultMessage += "\n\n" + log;
			fs::remove(fs::u8path(settings.output), error);
			resultOk = false;
		}
		else if (status != 0)
		{
			resultOk = false;
			resultMessage = IO::formatString("%s (%d)\n\n%s", getString("video_ffmpeg_error"), status, log.c_str());
		}
		else
		{
			resultOk = true;
			resultMessage = IO::formatString("%s\n%s", getString("video_done"), settings.output.c_str());
			if (!warning.empty())
				resultMessage += "\n\n" + warning;
		}
		state = State::Finished;
		openRequested = true;
	}

	void ExportWindow::update(ScoreContext& context, ScorePreviewWindow& preview, Renderer* renderer, ScoreEditorTimeline& timeline)
	{
		if (state == State::Closed)
			return;

		if (state == State::Running)
			renderSome(context, preview, renderer);

		const char* popupId = "###video_export";
		if (openRequested)
		{
			ImGui::OpenPopup(popupId);
			openRequested = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize({ 520, 0 }, ImGuiCond_Always);
		const std::string title = IO::formatString("%s %s%s", ICON_FA_FILM, getString("export_video"), popupId);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (state != State::Running)
				state = State::Closed;
			return;
		}

		if (state == State::Settings)
		{
			UI::beginPropertyColumns();
			UI::propertyLabel(getString("video_output"));
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - UI::btnSmall.x - ImGui::GetStyle().ItemSpacing.x);
			ImGui::InputText("##video_output", &settings.output);
			ImGui::SameLine();
			if (ImGui::Button("...", UI::btnSmall))
			{
				IO::FileDialog dialog{};
				dialog.title = getString("export_video");
				dialog.filters = { { "MP4 Video", "*.mp4" } };
				dialog.defaultExtension = "mp4";
				dialog.inputFilename = IO::File::getFilenameWithoutExtension(settings.output);
				dialog.parentWindowHandle = Application::windowState.windowHandle;
				if (dialog.saveFile() == IO::FileDialogResult::OK)
					settings.output = dialog.outputFilename;
			}
			ImGui::NextColumn();

			static const std::pair<const char*, std::pair<int, int>> presets[] = {
				{ "1280 x 720 (720p)", { 1280, 720 } }, { "1920 x 1080 (1080p)", { 1920, 1080 } }, { "2560 x 1440 (1440p)", { 2560, 1440 } },
				{ "3840 x 2160 (4K)", { 3840, 2160 } }, { "1080 x 1920 (vertical)", { 1080, 1920 } }, { "2160 x 3840 (vertical 4K)", { 2160, 3840 } } };
			UI::propertyLabel(getString("video_resolution"));
			const std::string current = IO::formatString("%d x %d", settings.width, settings.height);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##video_resolution", current.c_str()))
			{
				for (const auto& [label, size] : presets)
					if (ImGui::Selectable(label, settings.width == size.first && settings.height == size.second))
					{
						settings.width = size.first;
						settings.height = size.second;
					}
				ImGui::EndCombo();
			}
			ImGui::NextColumn();
			UI::addIntProperty(getString("video_width"), settings.width, 320, 3840);
			UI::addIntProperty(getString("video_height"), settings.height, 240, 3840);

			UI::propertyLabel(getString("video_fps"));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##video_fps", std::to_string(settings.fps).c_str()))
			{
				for (int fps : { 24, 30, 60, 120 })
					if (ImGui::Selectable(std::to_string(fps).c_str(), settings.fps == fps))
						settings.fps = fps;
				ImGui::EndCombo();
			}
			ImGui::NextColumn();
			UI::addCheckboxProperty(getString("video_hit_sounds"), settings.hitSounds);
			UI::addCheckboxProperty(getString("video_from_cursor"), settings.fromCursor);
			// the live mode also picks FEVER's look, so it works with the UI off too
			static const char* liveModes[] = { "live_mode_solo", "live_mode_coop", "live_mode_carnival" };
			UI::addSelectProperty(getString("video_live_mode"), config.videoLiveMode, liveModes, 3);
			UI::addCheckboxProperty(getString("video_draw_ui"), config.videoDrawUi);
			ImGui::BeginDisabled(!config.videoDrawUi);
				UI::addCheckboxProperty(getString("preview_score_delta"), config.pvDrawScoreDelta);
			UI::addCheckboxProperty(getString("video_effect_sounds"), config.videoEffectSounds);
			ImGui::EndDisabled();
			UI::addPercentSliderProperty(getString("video_music_volume"), config.videoMusicVolume);
			UI::addPercentSliderProperty(getString("video_se_volume"), config.videoSeVolume);
			UI::endPropertyColumns();
			if (settings.hitSounds && config.videoSeVolume <= 0.001f)
				ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", getString("video_se_muted"));

			const std::string& music = context.workingData.musicFilename;
			if (music.empty() || !IO::File::exists(music))
				ImGui::TextDisabled("%s", getString("video_no_music"));
			else
				ImGui::TextDisabled("%s %s", ICON_FA_MUSIC, IO::File::getFilename(music).c_str());

			if (!ffmpegFound)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(IO::formatString(getString("video_ffmpeg_missing"), config.ffmpegPath.c_str()).c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
			}
			drawInstaller(context, timeline);
			ImGui::TextDisabled("%s", getString("video_note"));

			// pjsekai-overlay-APPEND's terms: videos made with its art credit the tool
			if (config.videoDrawUi && !Canvas::Preview::activeSkin().overlayDir.empty())
			{
				static const char* credit =
					"PJSekai-style video creation tool:\n"
					"- Forked ver. by TootieJin (https://tootiejin.com) & \xe3\x81\xb4\xe3\x81\x83\xe3\x81\xbe\xe3\x82\x93 (https://pim4n-net.com)\n"
					"- Developed by \xe5\x90\x8d\xe7\x84\xa1\xe3\x81\x97\xef\xbd\xa1 (https://sevenc7c.com)\n"
					"   https://github.com/TootieJin/pjsekai-overlay-APPEND";
				if (ImGui::TreeNode(getString("video_credit_title")))
				{
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextDisabled("%s", credit);
					ImGui::PopTextWrapPos();
					if (ImGui::Button(getString("video_credit_copy")))
						ImGui::SetClipboardText(credit);
					ImGui::TreePop();
				}
			}

			ImGui::Separator();
			const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
			ImGui::BeginDisabled(!ffmpegFound || settings.output.empty() || installer.busy());
			if (ImGui::Button(IO::concat(ICON_FA_FILM, getString("export"), " ").c_str(), { half, 0 }))
			{
				resultMessage.clear();
				if (start(context, timeline))
					state = State::Running;
				else
					state = State::Finished;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button(getString("cancel"), { half, 0 }))
			{
				state = State::Closed;
				ImGui::CloseCurrentPopup();
			}
		}
		else if (state == State::Running)
		{
			const float progress = frameCount > 0 ? static_cast<float>(frame) / frameCount : 0.0f;
			const double elapsed = ImGui::GetTime() - startedAt;
			const double remaining = progress > 0.01f ? elapsed / progress - elapsed : 0.0;
			ImGui::Text("%s  %d / %d", getString("video_rendering"), frame, frameCount);
			ImGui::ProgressBar(progress, { -1, 0 });
			ImGui::TextDisabled("%s %.0f s", getString("video_remaining"), remaining);
			if (ImGui::Button(getString("cancel"), { -1, 0 }))
				finish(context, true);
		}
		else if (state == State::Finished)
		{
			ImGui::PushTextWrapPos(480.0f);
			if (resultOk)
				ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1.0f }, "%s %s", ICON_FA_CHECK_CIRCLE, resultMessage.c_str());
			else
				ImGui::TextUnformatted(resultMessage.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Separator();
			const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
			ImGui::BeginDisabled(!resultOk);
			if (ImGui::Button(getString("video_show_file"), { half, 0 }))
				IO::openInShell(fs::u8path(settings.output).parent_path().u8string());
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("OK", { half, 0 }))
			{
				state = State::Closed;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
}
