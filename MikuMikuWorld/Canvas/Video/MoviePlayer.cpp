#include "MoviePlayer.h"
#include "../Core/Profiler.h"
#include "../../IO.h"
#include "../../File.h"
#include <glad/glad.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <d3d11.h>
#include <d3d10.h>
#ifdef _MSC_VER
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif
#endif

namespace fs = std::filesystem;

namespace Canvas::Video
{
	namespace
	{
		constexpr int MAX_QUEUED = 12;			// frames decoded ahead of the clock
		constexpr double JUMP = 1.5;			// a clock jump this far ahead restarts ffmpeg
		constexpr double RESTART_GAP = 0.12;	// while scrubbing, restart at most this often (s)

		double now()
		{
			using namespace std::chrono;
			return duration<double>(steady_clock::now().time_since_epoch()).count();
		}

		FILE* openForReading(const std::string& command)
		{
#ifdef _WIN32
			return _wpopen(IO::mbToWideStr("\"" + command + "\"").c_str(), L"rb");
#else
			return popen(command.c_str(), "r");
#endif
		}

		std::string lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

#ifdef _WIN32
		// ---------------------------------------------------------- Media Foundation (Windows)
		// Media Foundation is used from worker threads only, each in the multithreaded apartment,
		// so it never meets the UI thread's COM setup (file dialogs make that single-threaded).
		struct MediaFoundation
		{
			bool com{}, ok{};
			MediaFoundation()
			{
				com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
				ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
			}
			~MediaFoundation()
			{
				if (ok)
					MFShutdown();
				if (com)
					CoUninitialize();
			}
		};

		template <typename T> void release(T*& p)
		{
			if (p)
				p->Release();
			p = nullptr;
		}

		// The picture inside the decoded frame (1080p video decodes into 1088 lines, for one).
		struct VideoLayout
		{
			int frameWidth{}, frameHeight{};	// decoded buffer
			int x{}, y{}, width{}, height{};	// visible part
			LONG stride{};						// bytes per line (negative: bottom-up)
			bool nv12{};						// NV12 (the decoders' own output) or RGB32
			bool bt709{ true };					// HD colours (BT.709) or SD (BT.601)
		};

		bool layoutOf(IMFMediaType* type, VideoLayout& layout)
		{
			UINT64 size = 0;
			if (FAILED(type->GetUINT64(MF_MT_FRAME_SIZE, &size)))
				return false;
			layout.frameWidth = static_cast<int>(size >> 32);
			layout.frameHeight = static_cast<int>(size & 0xFFFFFFFF);
			layout.x = layout.y = 0;
			layout.width = layout.frameWidth;
			layout.height = layout.frameHeight;
			MFVideoArea area{};
			if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr)) &&
				area.Area.cx > 0 && area.Area.cy > 0)
			{
				layout.x = std::clamp<int>(area.OffsetX.value, 0, layout.frameWidth - 1);
				layout.y = std::clamp<int>(area.OffsetY.value, 0, layout.frameHeight - 1);
				layout.width = std::min<int>(area.Area.cx, layout.frameWidth - layout.x);
				layout.height = std::min<int>(area.Area.cy, layout.frameHeight - layout.y);
			}
			GUID subtype{};
			layout.nv12 = SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_NV12;
			UINT32 stride = 0;
			layout.stride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride)
				: layout.frameWidth * (layout.nv12 ? 1 : 4);
			UINT32 matrix = 0;
			layout.bt709 = SUCCEEDED(type->GetUINT32(MF_MT_YUV_MATRIX, &matrix)) ? matrix != MFVideoTransferMatrix_BT601 : layout.height >= 720;
			return layout.frameWidth > 0 && layout.frameHeight > 0;
		}

		// A D3D11 device for Media Foundation: with it the decoder runs on the GPU's video engine
		// (DXVA) instead of a CPU core, which was most of what a 1080p MV cost on a laptop. Frames
		// still come back as NV12 in memory (MF copies them out of the GPU on Lock2D).
		struct GpuDecoder
		{
			ID3D11Device* device{};
			IMFDXGIDeviceManager* manager{};

			bool create()
			{
				const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
				if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
					levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, nullptr)))
					return false;
				// the decoder and the reader both use the device from their own threads
				ID3D10Multithread* multithread = nullptr;
				if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&multithread))))
				{
					multithread->SetMultithreadProtected(TRUE);
					multithread->Release();
				}
				UINT token = 0;
				if (FAILED(MFCreateDXGIDeviceManager(&token, &manager)) || FAILED(manager->ResetDevice(device, token)))
				{
					release();
					return false;
				}
				return true;
			}

			void release()
			{
				drop(manager);
				drop(device);
			}

			template <typename T> static void drop(T*& p)
			{
				if (p)
					p->Release();
				p = nullptr;
			}

			~GpuDecoder() { release(); }
		};

		// One device for every movie (the MV and the FEVER / ALL PERFECT clips): making a device costs
		// ~300 ms, which showed as a longer black flash on every play or seek. Made on first use and
		// kept for the whole run (never torn down: MF may still hold it from a worker at exit).
		GpuDecoder* sharedGpu()
		{
			static std::once_flag once;
			static GpuDecoder* gpu = nullptr;
			std::call_once(once, [] {
				auto* made = new GpuDecoder();
				if (made->create())
					gpu = made;
				else
					delete made;
			});
			return gpu;
		}

		// `gpu`: decode on the GPU (convert must be true); null = Windows' software decoders
		IMFSourceReader* openReader(const std::string& file, bool convert, GpuDecoder* gpu = nullptr)
		{
			IMFAttributes* attributes = nullptr;
			IMFSourceReader* reader = nullptr;
			if (convert && SUCCEEDED(MFCreateAttributes(&attributes, 3)))
			{
				if (gpu && gpu->manager)
				{
					// plain video processing is not allowed with a device manager; the advanced one is
					attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, gpu->manager);
					attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
				}
				else
					attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
				attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
			}
			if (FAILED(MFCreateSourceReaderFromURL(IO::mbToWideStr(file).c_str(), attributes, &reader)))
				reader = nullptr;
			release(attributes);
			return reader;
		}

		// Size and length of the movie, when Windows can decode it.
		bool probeNative(const std::string& file, int& width, int& height, double& duration)
		{
			bool ok = false;
			std::thread([&] {
				MediaFoundation mf;
				if (!mf.ok)
					return;
				IMFSourceReader* reader = openReader(file, false);
				if (!reader)
					return;
				IMFMediaType* type = nullptr;
				VideoLayout layout;
				if (SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type)) && layoutOf(type, layout))
				{
					width = layout.width;
					height = layout.height;
					ok = true;
				}
				release(type);
				PROPVARIANT value;
				PropVariantInit(&value);
				if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)))
					duration = static_cast<double>(value.uhVal.QuadPart) / 1e7;
				PropVariantClear(&value);
				release(reader);
			}).join();
			return ok;
		}

		// The visible picture of an NV12 frame (full-size luma plane, then half-size interleaved
		// U/V), copied row by row into a tight `width` x `height` buffer: no maths, the shader
		// does the colours.
		void copyNV12(const BYTE* top, LONG pitch, const VideoLayout& layout, int width, int height, std::vector<unsigned char>& out)
		{
			out.resize(static_cast<size_t>(width) * height * 3 / 2);
			const int x0 = layout.x & ~1, y0 = layout.y & ~1;
			for (int y = 0; y < height; ++y)
				std::memcpy(&out[static_cast<size_t>(y) * width], top + static_cast<ptrdiff_t>(y0 + y) * pitch + x0, width);
			const BYTE* chroma = top + static_cast<ptrdiff_t>(pitch) * layout.frameHeight;
			unsigned char* uv = &out[static_cast<size_t>(width) * height];
			for (int y = 0; y < height / 2; ++y)
				std::memcpy(uv + static_cast<size_t>(y) * width, chroma + static_cast<ptrdiff_t>(y0 / 2 + y) * pitch + x0, width);
		}

		// BGRX (Media Foundation's RGB32) rows, top first, into a tight buffer
		void copyBGRX(const BYTE* top, LONG pitch, const VideoLayout& layout, int width, int height, std::vector<unsigned char>& out)
		{
			out.resize(static_cast<size_t>(width) * height * 4);
			for (int y = 0; y < height; ++y)
				std::memcpy(&out[static_cast<size_t>(y) * width * 4], top + static_cast<ptrdiff_t>(layout.y + y) * pitch + static_cast<ptrdiff_t>(layout.x) * 4,
					static_cast<size_t>(width) * 4);
		}
#endif

		// ---------------------------------------------------------- YUV -> RGBA on the GPU
		// One shared program: a full-screen triangle sampling the planes into the player's texture.
		// mode 0 = NV12 (limited range, BT.709 or BT.601), 1 = BGRX. `unmult` gives AviUtl's alpha
		// (the brightest channel) for the effect clips.
		const char* CONVERT_VS = R"(#version 330 core
out vec2 uv;
void main()
{
	vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
	uv = p * 0.5 + 0.5;
	gl_Position = vec4(p, 0.0, 1.0);
})";
		const char* CONVERT_FS = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D planeY;
uniform sampler2D planeUV;
uniform sampler2D planeRgb;
uniform int mode;
uniform int bt709;
uniform int unmult;
uniform sampler2D tint;
uniform int useTint;
uniform float tintAngle;
uniform vec2 frameSize;
uniform float tintSize;
void main()
{
	vec3 rgb;
	if (mode == 0)
	{
		float y = (texture(planeY, uv).r - 16.0 / 255.0) * 1.164383;
		vec2 c = texture(planeUV, uv).rg - vec2(128.0 / 255.0);
		rgb = bt709 == 1 ? vec3(y + 1.792741 * c.y, y - 0.213249 * c.x - 0.532909 * c.y, y + 2.112402 * c.x)
		                 : vec3(y + 1.596027 * c.y, y - 0.391762 * c.x - 0.812968 * c.y, y + 2.017232 * c.x);
	}
	else
		rgb = texture(planeRgb, uv).bgr;
	rgb = clamp(rgb, 0.0, 1.0);
	float a = unmult == 1 ? max(rgb.r, max(rgb.g, rgb.b)) : 1.0;
	if (useTint == 1)
	{
		// the picture on top, turned round the centre, shown where the clip is (its alpha)
		vec2 q = (uv - 0.5) * frameSize;
		float c = cos(tintAngle), s = sin(tintAngle);
		vec2 r = vec2(c * q.x - s * q.y, s * q.x + c * q.y);
		rgb = texture(tint, r / tintSize + 0.5).rgb * a;
	}
	color = vec4(rgb, a);
})";

		struct Converter
		{
			GLuint program{}, vao{};
			GLint mode{ -1 }, bt709{ -1 }, unmult{ -1 }, useTint{ -1 }, tintAngle{ -1 }, frameSize{ -1 }, tintSize{ -1 };
			bool failed{};

			bool ready()
			{
				if (program || failed)
					return program != 0;
				auto compile = [](GLenum type, const char* source) {
					const GLuint shader = glCreateShader(type);
					glShaderSource(shader, 1, &source, nullptr);
					glCompileShader(shader);
					GLint ok = 0;
					glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
					if (!ok)
					{
						glDeleteShader(shader);
						return 0u;
					}
					return shader;
				};
				const GLuint vs = compile(GL_VERTEX_SHADER, CONVERT_VS), fs = compile(GL_FRAGMENT_SHADER, CONVERT_FS);
				if (vs && fs)
				{
					program = glCreateProgram();
					glAttachShader(program, vs);
					glAttachShader(program, fs);
					glLinkProgram(program);
					GLint ok = 0;
					glGetProgramiv(program, GL_LINK_STATUS, &ok);
					if (!ok)
					{
						glDeleteProgram(program);
						program = 0;
					}
				}
				if (vs)
					glDeleteShader(vs);
				if (fs)
					glDeleteShader(fs);
				if (!program)
				{
					failed = true;
					return false;
				}
				GLint previous = 0;
				glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
				glUseProgram(program);
				glUniform1i(glGetUniformLocation(program, "planeY"), 0);
				glUniform1i(glGetUniformLocation(program, "planeUV"), 1);
				glUniform1i(glGetUniformLocation(program, "planeRgb"), 2);
				glUniform1i(glGetUniformLocation(program, "tint"), 3);
				glUseProgram(static_cast<GLuint>(previous));
				mode = glGetUniformLocation(program, "mode");
				bt709 = glGetUniformLocation(program, "bt709");
				unmult = glGetUniformLocation(program, "unmult");
				useTint = glGetUniformLocation(program, "useTint");
				tintAngle = glGetUniformLocation(program, "tintAngle");
				frameSize = glGetUniformLocation(program, "frameSize");
				tintSize = glGetUniformLocation(program, "tintSize");
				glGenVertexArrays(1, &vao);
				return true;
			}
		};

		Converter& converter()
		{
			static Converter c;
			return c;
		}

		GLuint makeTexture(GLint format, int w, int h, GLenum dataFormat)
		{
			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, dataFormat, GL_UNSIGNED_BYTE, nullptr);
			return texture;
		}
	}

	MoviePlayer::~MoviePlayer()
	{
		close();
	}

	bool MoviePlayer::isMovieFile(const std::string& name)
	{
		const std::string extension = lower(fs::u8path(name).extension().u8string());
		return extension == ".mp4" || extension == ".webm" || extension == ".mkv" || extension == ".mov" || extension == ".avi" || extension == ".m4v";
	}

	std::string MoviePlayer::findNextTo(const std::string& musicFile)
	{
		if (musicFile.empty())
			return {};
		std::error_code ec;
		const fs::path music = fs::u8path(musicFile);
		const fs::path folder = music.parent_path();
		if (folder.empty() || !fs::is_directory(folder, ec))
			return {};

		const std::string stem = lower(music.stem().u8string());
		std::string best;
		int bestRank = 0;
		for (const auto& entry : fs::directory_iterator(folder, ec))
		{
			if (!entry.is_regular_file(ec) || !isMovieFile(entry.path().u8string()))
				continue;
			const std::string name = lower(entry.path().stem().u8string());
			if (name.size() >= 9 && name.compare(name.size() - 9, 9, "(preview)") == 0)
				continue;	// the editor's own video exports
			int rank = 0;
			if (name == stem)
				rank = 5;
			else if (name == "2dmv")
				rank = 4;
			else if (name.find("2dmv") != std::string::npos)
				rank = 3;
			else if (name == "mv" || name == "movie")
				rank = 2;
			else if (name.find("mv") != std::string::npos || name.find("movie") != std::string::npos)
				rank = 1;
			if (rank > bestRank || (rank == bestRank && rank > 0 && entry.path().u8string() < best))
			{
				bestRank = rank;
				best = entry.path().u8string();
			}
		}
		return best;
	}

	bool MoviePlayer::open(const std::string& movie, const std::string& ffmpeg)
	{
		if (movie == file && ffmpeg == ffmpegPath)
			return isOpen();
		close();
		file = movie;
		ffmpegPath = ffmpeg;
		if (movie.empty())
			return false;
		if (!IO::File::exists(movie))
		{
			error = "movie file not found";
			return false;
		}

		int sourceWidth = 0, sourceHeight = 0;
		native = false;
#ifdef _WIN32
		// Windows decodes MP4 (H.264 / HEVC with its extension), WMV, AVI...; ffmpeg only when it cannot
		native = probeNative(movie, sourceWidth, sourceHeight, duration);
#endif
		if (!native)
		{
			// ffmpeg prints the stream layout to stderr: "Duration: 00:03:12.34" and "Video: ... 1920x1080"
			int status = 0;
			const std::string info = ffmpeg.empty() ? std::string() :
				IO::runProcess(IO::shellQuote(ffmpeg) + " -hide_banner -i " + IO::shellQuote(movie) + " 2>&1", &status);
			std::smatch match;
			const std::regex video(R"(Video:[^\n]*?\b(\d{2,5})x(\d{2,5})\b)");
			if (!std::regex_search(info, match, video))
			{
#ifdef _WIN32
				error = info.empty() ? "Windows cannot play this video and ffmpeg was not found (MP4 / H.264 always works)" : "no video stream";
#else
				error = info.empty() ? "ffmpeg could not be started" : "no video stream";
#endif
				return false;
			}
			sourceWidth = std::stoi(match[1]);
			sourceHeight = std::stoi(match[2]);
			const std::regex length(R"(Duration: (\d+):(\d+):(\d+(?:\.\d+)?))");
			if (std::regex_search(info, match, length))
				duration = std::stoi(match[1]) * 3600.0 + std::stoi(match[2]) * 60.0 + std::stod(match[3]);
		}
		if (sourceWidth <= 0 || sourceHeight <= 0)
		{
			error = "no video stream";
			return false;
		}

		if (!converter().ready())
		{
			error = "the video shader could not be compiled";
			return false;
		}

		// the planes are decoded at the movie's own size (NV12 needs even sizes); the shader
		// scales into a texture at most maxWidth wide
		this->sourceWidth = std::max(2, sourceWidth & ~1);
		this->sourceHeight = std::max(2, sourceHeight & ~1);
		const double scale = std::min(1.0, static_cast<double>(maxWidth) / this->sourceWidth);
		width = std::max(2, static_cast<int>(std::lround(this->sourceWidth * scale / 2.0)) * 2);
		height = std::max(2, static_cast<int>(std::lround(this->sourceHeight * scale / 2.0)) * 2);
		error.clear();

		texture = makeTexture(GL_RGBA8, width, height, GL_RGBA);
		for (int set = 0; set < 2; ++set)
		{
			planeY[set] = makeTexture(GL_R8, this->sourceWidth, this->sourceHeight, GL_RED);
			planeUV[set] = makeTexture(GL_RG8, this->sourceWidth / 2, this->sourceHeight / 2, GL_RG);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		glGenFramebuffers(1, &framebuffer);
		GLint previous = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
		return true;
	}

	void MoviePlayer::resizeOutput(int newWidth)
	{
		const int newHeight = std::max(2, static_cast<int>(std::lround(static_cast<double>(sourceHeight) * newWidth / sourceWidth / 2.0)) * 2);
		if (newWidth == width && newHeight == height)
			return;
		width = newWidth;
		height = newHeight;
		GLint previousTexture = 0, previousFramebuffer = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
	}

	void MoviePlayer::present(const Frame& frame)
	{
		CANVAS_PROFILE("movie.present");
		Converter& c = converter();
		if (!c.ready() || !framebuffer)
			return;
		{
			// the size it is drawn at, rounded up to 320 px steps (at least 640, at most the movie's own)
			const int cap = std::min(sourceWidth, maxWidth);
			const int wanted = requestedWidth <= 0 ? cap : std::min(cap, std::max(640, (requestedWidth + 319) / 320 * 320));
			resizeOutput(std::max(2, wanted & ~1));
		}

		// upload the planes
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		GLint activeTexture = 0, bound[4]{};
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
		for (int unit = 0; unit < 4; ++unit)
		{
			glActiveTexture(GL_TEXTURE0 + unit);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound[unit]);
		}
		planeSet ^= 1;
		if (frame.nv12)
		{
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, planeY[planeSet]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sourceWidth, sourceHeight, GL_RED, GL_UNSIGNED_BYTE, frame.pixels.data());
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, planeUV[planeSet]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sourceWidth / 2, sourceHeight / 2, GL_RG, GL_UNSIGNED_BYTE,
				frame.pixels.data() + static_cast<size_t>(sourceWidth) * sourceHeight);
		}
		else
		{
			if (!planeRgb[planeSet])
				planeRgb[planeSet] = makeTexture(GL_RGBA8, sourceWidth, sourceHeight, GL_RGBA);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, planeRgb[planeSet]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sourceWidth, sourceHeight, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
		}

		// convert into the texture, leaving every bit of GL state as the caller had it (this runs
		// in the middle of the preview's own drawing)
		GLint previousFramebuffer = 0, previousProgram = 0, previousVao = 0, viewport[4]{};
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
		glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
		glGetIntegerv(GL_VIEWPORT, viewport);
		const GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST), depth = glIsEnabled(GL_DEPTH_TEST),
			cull = glIsEnabled(GL_CULL_FACE), stencil = glIsEnabled(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_STENCIL_TEST);

		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glViewport(0, 0, width, height);
		glUseProgram(c.program);
		glUniform1i(c.mode, frame.nv12 ? 0 : 1);
		glUniform1i(c.bt709, frame.bt709 ? 1 : 0);
		glUniform1i(c.unmult, unmultiply ? 1 : 0);
		glUniform1i(c.useTint, tintTexture ? 1 : 0);
		if (tintTexture)
		{
			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, tintTexture);
			glUniform1f(c.tintAngle, tintRadians);
			glUniform2f(c.frameSize, static_cast<float>(sourceWidth), static_cast<float>(sourceHeight));
			glUniform1f(c.tintSize, std::max(1.0f, tintSize));
		}
		glBindVertexArray(c.vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		glBindVertexArray(static_cast<GLuint>(previousVao));
		glUseProgram(static_cast<GLuint>(previousProgram));
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		if (blend)
			glEnable(GL_BLEND);
		if (scissor)
			glEnable(GL_SCISSOR_TEST);
		if (depth)
			glEnable(GL_DEPTH_TEST);
		if (cull)
			glEnable(GL_CULL_FACE);
		if (stencil)
			glEnable(GL_STENCIL_TEST);
		for (int unit = 3; unit >= 0; --unit)
		{
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound[unit]));
		}
		glActiveTexture(static_cast<GLenum>(activeTexture));
	}

	void MoviePlayer::close()
	{
		stop();
		for (unsigned int* t : { &texture, &planeY[0], &planeUV[0], &planeRgb[0], &planeY[1], &planeUV[1], &planeRgb[1] })
		{
			if (*t)
				glDeleteTextures(1, t);
			*t = 0;
		}
		if (framebuffer)
			glDeleteFramebuffers(1, &framebuffer);
		framebuffer = 0;
		width = height = sourceWidth = sourceHeight = 0;
		duration = 0.0;
		native = false;
		decodeError.clear();
		error.clear();
		file.clear();
		hasPicture = false;
		shownTime = -1.0;
	}

	void MoviePlayer::start(double seconds)
	{
		CANVAS_PROFILE("movie.start");
		stop();
		startTime = std::max(0.0, seconds);
		lastRestart = now();
		if (native)
		{
			stopping = finished = false;
			decoding = true;
			worker = std::thread(&MoviePlayer::readFramesNative, this);
			return;
		}
		// -ss before -i seeks by keyframe then decodes to the exact time; fps gives a steady clock.
		// NV12 at the movie's own (even) size: the shader does colours and scaling
		const std::string command = IO::shellQuote(ffmpegPath) + " -hide_banner -loglevel quiet -nostdin -ss " +
			std::to_string(startTime) + " -i " + IO::shellQuote(file) + " -an -sn -vf " +
			IO::shellQuote("fps=" + std::to_string(static_cast<int>(fps)) + ",scale=" + std::to_string(sourceWidth) + ":" + std::to_string(sourceHeight) +
				":out_range=tv") +
			" -f rawvideo -pix_fmt nv12 -";
#ifndef _WIN32
		std::signal(SIGPIPE, SIG_IGN);
#endif
		pipe = openForReading(command);
		if (!pipe)
		{
			error = "ffmpeg could not be started";
			return;
		}
		stopping = finished = false;
		decoding = true;
		worker = std::thread(&MoviePlayer::readFrames, this);
	}

	void MoviePlayer::stop()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
			frames.clear();
		}
		changed.notify_all();
		if (worker.joinable())
			worker.join();
		if (pipe)
			IO::closeProcess(pipe);
		pipe = nullptr;
		decoding = false;
		frames.clear();
	}

	bool MoviePlayer::pushFrame(Frame&& frame)
	{
		std::unique_lock lock(mutex);
		changed.wait(lock, [&] { return stopping || frames.size() < MAX_QUEUED; });
		if (stopping)
			return false;
		frames.push_back(std::move(frame));
		changed.notify_all();
		return true;
	}

	void MoviePlayer::readFrames()
	{
		const size_t size = static_cast<size_t>(sourceWidth) * sourceHeight * 3 / 2;
		for (long index = 0;; ++index)
		{
			Frame frame;
			frame.time = startTime + index / fps;
			frame.bt709 = sourceHeight >= 720;
			frame.pixels.resize(size);
			size_t got = 0;
			while (got < size)
			{
				const size_t n = fread(frame.pixels.data() + got, 1, size - got, pipe);
				if (n == 0)
					break;
				got += n;
			}

			if (got < size)
			{
				std::lock_guard lock(mutex);
				finished = true;
				changed.notify_all();
				return;
			}
			if (!pushFrame(std::move(frame)))
				return;
		}
	}

	void MoviePlayer::readFramesNative()
	{
		auto finish = [&] {
			std::lock_guard lock(mutex);
			finished = true;
			changed.notify_all();
		};
#ifdef _WIN32
		auto fail = [&](const char* what, HRESULT hr) {
			std::lock_guard lock(mutex);
			decodeError = IO::formatString("Windows could not decode the video (%s, 0x%08lX)", what, static_cast<unsigned long>(hr));
		};
		MediaFoundation mf;
		const int planeWidth = sourceWidth, planeHeight = sourceHeight;
		const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		VideoLayout layout;
		bool ok = false;
		HRESULT hr = E_FAIL;
		// GPU decoding first; when the GPU (or its driver) will not take the file, Windows' software
		// decoders as before. CANVAS_SOFTWARE_VIDEO=1 forces software (for comparing).
		const char* forceSoftware = std::getenv("CANVAS_SOFTWARE_VIDEO");
		GpuDecoder* gpu = mf.ok && !(forceSoftware && *forceSoftware == '1') ? sharedGpu() : nullptr;
		const bool tryGpu = gpu != nullptr;
		IMFSourceReader* reader = nullptr;
		for (int attempt = tryGpu ? 0 : 1; attempt < 2 && !ok; ++attempt)
		{
			release(reader);
			reader = mf.ok ? openReader(file, true, attempt == 0 ? gpu : nullptr) : nullptr;
			if (!reader)
				continue;
			reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
			reader->SetStreamSelection(stream, TRUE);
			// the decoder's own NV12 first (converted here); RGB32 through Windows' video processor
			// for decoders that do not give NV12
			for (const GUID& subtype : { MFVideoFormat_NV12, MFVideoFormat_RGB32 })
			{
				IMFMediaType* wanted = nullptr;
				IMFMediaType* current = nullptr;
				if (SUCCEEDED(hr = MFCreateMediaType(&wanted)) && SUCCEEDED(hr = wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
					SUCCEEDED(hr = wanted->SetGUID(MF_MT_SUBTYPE, subtype)) && SUCCEEDED(hr = reader->SetCurrentMediaType(stream, nullptr, wanted)) &&
					SUCCEEDED(hr = reader->GetCurrentMediaType(stream, &current)))
					ok = layoutOf(current, layout);
				release(wanted);
				release(current);
				if (ok)
					break;
			}
			usedGpu = ok && attempt == 0;
		}
		if (!reader)
		{
			fail("open", E_FAIL);
			return finish();
		}
		if (!ok)
			fail("no RGB or NV12 output", hr);
		if (ok && startTime > 0.0)
		{
			PROPVARIANT position;
			PropVariantInit(&position);
			position.vt = VT_I8;
			position.hVal.QuadPart = static_cast<LONGLONG>(startTime * 1e7);
			reader->SetCurrentPosition(GUID_NULL, position);
			PropVariantClear(&position);
		}

		// The movie's own frames are shown on the player's steady 30 fps clock: each clock tick
		// gets the latest movie frame that started before it.
		std::vector<unsigned char> picture;
		bool pictureNv12 = true;
		double pictureEnd = 0.0;
		long tick = 0;
		auto emitUntil = [&](double limit) {
			while (!picture.empty())
			{
				const double time = startTime + tick / fps;
				if (time >= limit - 1e-6)
					return true;
				Frame frame;
				frame.time = time;
				frame.pixels = picture;
				frame.nv12 = pictureNv12;
				frame.bt709 = layout.bt709;
				if (!pushFrame(std::move(frame)))
					return false;
				++tick;
			}
			return true;
		};
		while (ok)
		{
			{
				std::lock_guard lock(mutex);
				if (stopping)
					break;
			}
			DWORD flags = 0;
			LONGLONG timestamp = 0;
			IMFSample* sample = nullptr;
			if (FAILED(hr = reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, &sample)))
			{
				if (picture.empty())
					fail("read", hr);
				break;
			}
			if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
			{
				IMFMediaType* current = nullptr;
				if (SUCCEEDED(reader->GetCurrentMediaType(stream, &current)))
					layoutOf(current, layout);
				release(current);
			}
			if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
			{
				release(sample);
				emitUntil(pictureEnd);
				break;
			}
			if (!sample)
				continue;
			const double time = static_cast<double>(timestamp) / 1e7;
			LONGLONG length = 0;
			const double frameLength = SUCCEEDED(sample->GetSampleDuration(&length)) && length > 0 ? length / 1e7 : 1.0 / fps;
			// the previous picture holds until this one starts
			if (!emitUntil(time))
			{
				release(sample);
				break;
			}
			IMFMediaBuffer* buffer = nullptr;
			if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)))
			{
				IMF2DBuffer* buffer2d = nullptr;
				BYTE* top = nullptr;
				LONG pitch = 0;
				// a picture smaller than the planes (a size change mid-stream) is skipped, never read
				// past its end
				const bool fits = layout.width >= planeWidth && layout.height >= planeHeight;
				pictureNv12 = layout.nv12;
				if (fits && SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))) && SUCCEEDED(buffer2d->Lock2D(&top, &pitch)))
				{
					if (layout.nv12)
						copyNV12(top, pitch, layout, planeWidth, planeHeight, picture);
					else
						copyBGRX(top, pitch, layout, planeWidth, planeHeight, picture);
					buffer2d->Unlock2D();
				}
				else if (fits)
				{
					BYTE* data = nullptr;
					DWORD bytes = 0;
					if (SUCCEEDED(buffer->Lock(&data, nullptr, &bytes)))
					{
						const LONG stride = layout.stride;
						if (layout.nv12)
						{
							if (stride > 0 && bytes >= static_cast<DWORD>(stride) * layout.frameHeight * 3 / 2)
								copyNV12(data, stride, layout, planeWidth, planeHeight, picture);
						}
						else
						{
							const BYTE* first = stride < 0 ? data + static_cast<ptrdiff_t>(-stride) * (layout.frameHeight - 1) : data;
							if (bytes >= static_cast<DWORD>(std::abs(stride)) * layout.frameHeight)
								copyBGRX(first, stride, layout, planeWidth, planeHeight, picture);
						}
						buffer->Unlock();
					}
				}
				release(buffer2d);
			}
			release(buffer);
			release(sample);
			pictureEnd = time + frameLength;
		}
		release(reader);
#endif
		finish();
	}

	void MoviePlayer::prime(double seconds)
	{
		if (isOpen() && !decoding)
			start(std::max(0.0, std::floor(seconds * fps + 1e-6) / fps));
	}

	bool MoviePlayer::update(double seconds, bool wait)
	{
		if (!isOpen())
			return false;
		const double frameTime = 1.0 / fps;
		if (seconds < 0.0 || (duration > 0.0 && seconds > duration + frameTime))
			return false;
		// the frame showing at `seconds` is the one that started last before it
		const double target = std::floor(seconds * fps + 1e-6) / fps;
		if (hasPicture && std::abs(target - shownTime) < frameTime / 2)
			return true;

		bool restart = !decoding;
		{
			std::lock_guard lock(mutex);
			if (!decodeError.empty() && error.empty())
				error = decodeError;
			const double decoded = frames.empty() ? shownTime : frames.back().time;
			if (target < startTime - frameTime / 2 || (hasPicture && target < shownTime - frameTime / 2))
				restart = true;		// went back
			else if (target > std::max(decoded, startTime) + JUMP)
				restart = true;		// jumped ahead further than decoding would catch up
			else if (finished && frames.empty() && target > shownTime + frameTime)
				restart = !(duration > 0.0 && target >= duration - frameTime);
		}
		if (restart)
		{
			// scrubbing asks for a new position every frame; keep the old picture in between (and
			// do not restart a decoder that cannot decode this file every frame either)
			if (!wait && lastRestart >= 0.0 && now() - lastRestart < RESTART_GAP)
				return hasPicture;
			start(target);
			// live: keep showing the last picture until the new position decodes (a looping clip
			// would blink every time it wraps); exporting waits for the right frame instead
			if (wait)
				hasPicture = false;
			shownTime = -1.0;
			if (!decoding)
				return false;
		}

		Frame shown;
		bool found = false;
		{
			std::unique_lock lock(mutex);
			for (;;)
			{
				while (!frames.empty() && frames.front().time <= target + frameTime / 2)
				{
					shown = std::move(frames.front());
					frames.pop_front();
					found = true;
				}
				changed.notify_all();
				if (found && (shown.time >= target - frameTime / 2 || !wait))
					break;
				if (!wait || finished)
					break;
				changed.wait_for(lock, std::chrono::milliseconds(200), [&] { return !frames.empty() || finished; });
				if (frames.empty() && finished)
					break;
			}
		}
		// live preview: show the first decoded frame right after a restart even if it is a hair late
		if (!found && !wait && !hasPicture)
		{
			std::lock_guard lock(mutex);
			if (!frames.empty() && frames.front().time <= target + JUMP)
			{
				shown = std::move(frames.front());
				frames.pop_front();
				found = true;
				changed.notify_all();
			}
		}
		if (found)
		{
			present(shown);
			shownTime = shown.time;
			hasPicture = true;
		}
		return hasPicture;
	}
}
