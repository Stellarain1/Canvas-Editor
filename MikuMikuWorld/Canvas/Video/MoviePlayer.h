#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Plays a song's 2DMV behind the score preview.
//
// Frames are decoded on a worker thread: on Windows by Media Foundation (part of Windows, nothing
// to install), elsewhere (or when Windows cannot read the file) by ffmpeg, the same executable the
// video exporter uses. The worker only copies the decoder's own YUV planes (NV12); the render
// thread uploads them and a small shader turns them into the RGBA texture (colours, scaling and
// the effect clips' "unmult" alpha all on the GPU, so a 1080p60 clip costs a memcpy). The player follows
// the preview's clock instead of keeping its own: every frame asks for the picture at a movie
// time, and a jump (seek, restart, scrubbing) restarts decoding at the new position. Without a
// decoder or a movie file nothing is drawn and the normal background shows.
namespace Canvas::Video
{
	class MoviePlayer
	{
	public:
		// the last opened movie decodes on the GPU (DXVA) rather than a CPU core
		bool decodesOnGpu() const { return usedGpu; }
	private:
		std::atomic<bool> usedGpu{ false };
	public:
		~MoviePlayer();

		// Opens a movie file (empty = close). Cheap when the file is already open.
		bool open(const std::string& file, const std::string& ffmpeg);
		void close();

		bool isOpen() const { return width > 0; }
		const std::string& getFile() const { return file; }
		const std::string& getError() const { return error; }
		// what decodes the open movie: "Windows" (Media Foundation) or "ffmpeg"
		const char* getDecoder() const { return native ? "Windows" : "ffmpeg"; }
		int getWidth() const { return width; }
		int getHeight() const { return height; }
		double getDuration() const { return duration; }

		// Shows the frame at `seconds` of the movie. With `wait` it blocks until that frame is
		// decoded (video export); otherwise it shows the newest frame available (live preview).
		// Returns false when there is no picture for that time (before the start, after the end,
		// or nothing decoded yet).
		bool update(double seconds, bool wait = false);
		unsigned int getTexture() const { return texture; }

		// Movie files the editor picks up next to a song: "<music name>.mp4", "2dmv.mp4", "mv.mp4",
		// or any video whose name contains "2dmv" / "mv". Empty when there is none.
		static std::string findNextTo(const std::string& musicFile);
		static bool isMovieFile(const std::string& file);

		// AviUtl's "unmult" for effect clips drawn over the picture: black becomes see-through and
		// each pixel's alpha is its brightest channel, so the texture is premultiplied (draw it with
		// GL_ONE, GL_ONE_MINUS_SRC_ALPHA). Set before the first update.
		void setUnmultiply(bool on) { unmultiply = on; }

		static constexpr double FPS = 30.0;
		// the clock the frames are shown on (30 for 2DMVs; the overlay's effect clips are 60). Set
		// before open.
		void setFrameRate(double rate) { fps = rate > 1.0 ? rate : FPS; }
		// widest picture kept (the movie's own size up to this; 4K by default so exports stay
		// sharp). Set before open.
		void setMaxWidth(int pixels) { maxWidth = std::max(64, pixels); }
		// how wide it is about to be drawn (pixels on screen; 0 = the movie's own size): the texture
		// follows, in steps of 320 px, so a small preview does not pay for a 1080p picture. The next
		// decoded frame comes out at the new size.
		void setOutputWidth(int pixels) { requestedWidth = pixels; }
		// starts decoding from `seconds` ahead of time so the first frames are ready when needed
		void prime(double seconds);

		// AviUtl's "clip to the layer above" for an unmultiplied clip: instead of its own colour each
		// pixel takes `texture` (a picture `size` pixels wide, centred, turned by `radians`) at the
		// clip's alpha. pjsekai-overlay-MULTI puts its spinning rainbow.png on the white FEVER frame
		// this way. Sizes are in the clip's own pixels; texture 0 = off. Set before each update.
		void setTint(unsigned int texture, float radians, float size)
		{
			tintTexture = texture;
			tintRadians = radians;
			tintSize = size;
		}

	private:
		struct Frame
		{
			double time{};
			// NV12 (sourceWidth x sourceHeight luma, then half-size interleaved U/V), or BGRX rows
			// when the decoder only gives RGB
			std::vector<unsigned char> pixels;
			bool nv12{ true };
			bool bt709{ true };
		};

		void start(double seconds);
		void stop();
		void readFrames();
		void readFramesNative();
		// queues a decoded frame, waiting while enough are queued; false when decoding should stop
		bool pushFrame(Frame&& frame);
		// uploads a decoded frame and converts it into `texture` (render thread)
		void present(const Frame& frame);

		std::string file, ffmpegPath, error;
		int width{}, height{};				// the RGBA texture
		int sourceWidth{}, sourceHeight{};	// the decoded planes (even)
		double duration{};
		unsigned int planeY[2]{}, planeUV[2]{}, planeRgb[2]{}, framebuffer{};
		int planeSet{};		// which of the two plane sets the next frame goes into
		unsigned int tintTexture{};
		float tintRadians{}, tintSize{ 1.0f };

		FILE* pipe{};
		bool native{};			// Media Foundation decodes this file (Windows)
		bool decoding{};		// a decoder is running (since the last start)
		std::thread worker;
		std::mutex mutex;
		std::condition_variable changed;
		std::deque<Frame> frames;
		bool stopping{}, finished{};
		std::string decodeError;	// set by the decoding thread (under the mutex)
		double startTime{};

		std::atomic<bool> unmultiply{ false };
		double fps{ FPS };
		int maxWidth{ 3840 };
		int requestedWidth{ 0 };
		void resizeOutput(int newWidth);

		unsigned int texture{};
		double shownTime{ -1.0 };
		bool hasPicture{};
		double lastRestart{ -1.0 };
	};
}
