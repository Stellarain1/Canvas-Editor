#include "Canvas/UI/CanvasSettings.h"
#include "Application.h"
#include "ScorePreview.h"
#include "PreviewEngine.h"
#include "Rendering/Camera.h"
#include "ResourceManager.h"
#include "Colors.h"
#include "ImageCrop.h"
#include "NoteSkin.h"
#include "ApplicationConfiguration.h"
#include "Canvas/Preview/LiveOverlay.h"
#include "Canvas/Core/Profiler.h"
#include "Canvas/Preview/CustomSkins.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <string>

namespace MikuMikuWorld
{
	struct PreviewPlaybackState
	{
		bool isPlaying{}, wasLastFramePlaying{};
	} playbackState;

	constexpr float EFFECTS_TARGET_ASPECT = 16.f / 9.f;

	namespace Utils
	{
		// Scale a rectangle with specified aspect ratio to be visible inside the target rectangle
		inline void fitRect(float target_width, float target_height, long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width  = target_aspect_ratio > source_aspect_ratio ? source_aspect_ratio * target_height : target_width;
			height = target_aspect_ratio < source_aspect_ratio ? target_width / source_aspect_ratio : target_height;
			return;
		}

		// Scale a rectangle with specified aspect ratio so it fill the area of the target rectangle
		inline void fillRect(float target_width, float target_height, long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width  = target_aspect_ratio < source_aspect_ratio ? source_aspect_ratio * target_height : target_width;
			height = target_aspect_ratio > source_aspect_ratio ? target_width / source_aspect_ratio : target_height;
			return;
		}
	};

	static const int NOTE_SIDE_WIDTH = 91;
	static const int NOTE_SIDE_PAD = 10;
	static const int MAX_FLICK_SPRITES = 6;
	static const int HOLD_XCUTOFF = 36;
	static const int GUIDE_XCUTOFF = 3;
	static const int GUIDE_Y_TOP_CUTOFF = -41;
	static const int GUIDE_Y_BOTTOM_CUTOFF = -12;
	static Color defaultTint { 1.f, 1.f, 1.f, 1.f };

	namespace
	{
		constexpr float HUD_LIFE_Y = 11.0f;
		constexpr float HUD_LIFE_HEIGHT = 104.0f;
		constexpr float PREVIEW_TOOLBAR_HUD_PADDING = 16.0f;
		constexpr float HUD_PAUSE_BUTTON_CENTER_X = 1834.56f;
		constexpr float HUD_PAUSE_BUTTON_CENTER_Y = 67.71f;
		constexpr float HUD_PAUSE_BUTTON_RADIUS = 39.55f;

		// MV lines: the stripes the game lays over a 2DMV (pattern at 1080p: period, lit pixels, alpha)
		unsigned int stripeTexture(int style)
		{
			static unsigned int textures[3]{};
			if (style < 1 || style > 2)
				return 0;
			if (textures[style])
				return textures[style];
			const int period = style == 1 ? 4 : 6, lit = style == 1 ? 2 : 3;
			const unsigned char alpha = style == 1 ? 64 : 128;
			std::vector<unsigned char> pixels(period * 4, 0);
			for (int x = period - lit; x < period; ++x)
				pixels[x * 4 + 3] = alpha;
			glGenTextures(1, &textures[style]);
			glBindTexture(GL_TEXTURE_2D, textures[style]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, period, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			glBindTexture(GL_TEXTURE_2D, 0);
			return textures[style];
		}
	}

	ScorePreviewBackground::ScorePreviewBackground() : backgroundFile(), jacketFile{}, brightness(0.5f), frameBuffer{2048, 2048}, init{false}
	{

	}

	ScorePreviewBackground::~ScorePreviewBackground()
	{
		frameBuffer.dispose();
	}

	void ScorePreviewBackground::setBrightness(float value)
	{
		brightness = value;
	}

	void ScorePreviewBackground::update(Renderer* renderer, const Jacket& jacket)
	{
		init = true;
		backgroundFile = config.backgroundImage;
		jacketFile = jacket.getFilename();
		brightness = config.pvBackgroundBrightness;
		bool useDefaultTexture = backgroundFile.empty() || !IO::File::exists(backgroundFile);
		Texture backgroundTex = { useDefaultTexture ? Application::getAppDir() + "res/editor/default.png" : backgroundFile};
		const float bgWidth = backgroundTex.getWidth(), bgHeight = backgroundTex.getHeight();
		if (bgWidth != frameBuffer.getWidth() || bgHeight != frameBuffer.getHeight())
			frameBuffer.resize(bgWidth, bgHeight);
		frameBuffer.bind();
		frameBuffer.clear(0, 0, 0, 0);
		int shaderId;
		if ((shaderId = ResourceManager::getShader("basic2d")) == -1) return;
		Shader* basicShader = ResourceManager::shaders[shaderId];
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;

		basicShader->use();

		// DirectXMath dies when projection size is too small
		const float projectionX{ std::max(bgWidth, 10.f) };
		const float projectionY{ std::max(bgHeight, 10.f) };
		basicShader->setMatrix4("projection", Camera::getOffCenterOrthographicProjection(0, projectionX, 0, projectionY));
		
		if (backgroundTex.getID() > 0)
		{
			renderer->beginBatch();
			renderer->drawRectangle({0, 0}, {bgWidth, bgHeight}, backgroundTex, 0, bgWidth, 0, bgHeight, defaultTint, 0);
			renderer->endBatch();
			if (useDefaultTexture && IO::File::exists(jacket.getFilename()))
				updateDrawDefaultJacket(renderer, jacket);
		}
		frameBuffer.unblind();
		backgroundTex.dispose();
	}

	void ScorePreviewBackground::updateDrawDefaultJacket(Renderer* renderer, const Jacket& jacket)
	{
		if (jacket.getFilename().empty()) return;
		int index = ResourceManager::getTexture("stage");
		if (index == -1) return;
		const Texture& stage = ResourceManager::textures[index];
		const Sprite* sprite;
		if (stage.sprites.size() < STAGE_SPR_COUNT) return;
		int shaderId;
		if ((shaderId = ResourceManager::getShader("basic2d")) == -1) return;
		Shader* basicShader = ResourceManager::shaders[shaderId];
		if ((shaderId = ResourceManager::getShader("masking")) == -1) return;
		Shader* maskShader = ResourceManager::shaders[shaderId];
		
		DirectX::XMFLOAT4 defCol = {defaultTint.r, defaultTint.g, defaultTint.b, defaultTint.a};
		DirectX::XMFLOAT4 mainCol = { 1, 1, 1, 0.65 };
		DirectX::XMFLOAT4 mirrorCol = { 1, 1, 1, 0.35 };
		auto mainLeftPos = Engine::quadvPos(602, 602 + 264, 816, 816 + 174);
		auto mainRightPos = Engine::quadvPos(1205, 1205 + 200, 629, 629 + 114);
		auto mirrorLeftPos = Engine::quadvPos(615, 615 + 256, 1170, 1170 + 162);
		auto mirrorRightPos = Engine::quadvPos(1186, 1186 + 196, 1387, 1387 + 105);
		auto mainLeftMask = Engine::quadUV(stage.sprites[SPR_JACKET_LEFT_MASK], stage);
		auto mainRightMask = Engine::quadUV(stage.sprites[SPR_JACKET_RIGHT_MASK], stage);
		auto mirrorLeftMask = Engine::quadUV(stage.sprites[SPR_MIRROR_JACKET_LEFT_MASK], stage);
		auto mirrorRightMask = Engine::quadUV(stage.sprites[SPR_MIRROR_JACKET_RIGHT_MASK], stage);

		maskShader->use();
		maskShader->setInt("baseTex", 0);
		maskShader->setInt("maskTex", 1);
		maskShader->setMatrix4("projection", Camera::getOffCenterOrthographicProjection(0, 2048, 0, 2048));
		renderer->beginBatch();
		renderer->pushQuadMasked(mainLeftPos, DefaultJacket::getLeftUV(), mainLeftMask, mainCol, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(mainRightPos, DefaultJacket::getRightUV(), mainRightMask, mainCol, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(mirrorLeftPos, DefaultJacket::getLeftMirrorUV(), mirrorLeftMask, mirrorCol, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(mirrorRightPos, DefaultJacket::getRightMirrorUV(), mirrorRightMask, mirrorCol, jacket.getTexID(), stage.getID());
		renderer->endBatch();
		
		basicShader->use();
		basicShader->setMatrix4("projection", Camera::getOffCenterOrthographicProjection(0, 2048, 0, 2048));
		renderer->beginBatch();
		renderer->pushQuad(mainLeftPos, mainLeftMask, DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(mainRightPos, mainRightMask, DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(mirrorLeftPos, mirrorLeftMask, DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(mirrorRightPos, mirrorRightMask, DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);

		sprite = &stage.sprites[SPR_JACKET_WINDOW];
		renderer->drawRectangle({682, 497}, {686, 686}, stage, sprite->getX1(), sprite->getX2(), sprite->getY1(), sprite->getY2(), defaultTint, 1);
		sprite = &stage.sprites[SPR_MIRROR_JACKET_WINDOW];
		renderer->drawRectangle({699, 958}, {651, 650}, stage, sprite->getX1(), sprite->getX2(), sprite->getY1(), sprite->getY2(), defaultTint.scaleAlpha(0.6f), 1);
		renderer->endBatch();

		auto mainWindowPos = Engine::quadvPos(824, 824 + 400, 666, 666 + 384);
		auto mainWindowMask = Engine::quadUV(stage.sprites[SPR_JACKET_MASK], stage);
		auto mirrorWindowPos = Engine::quadvPos(834, 834 + 386, 1120, 1120 + 336);
		auto mirrorWindowMask = Engine::quadUV(stage.sprites[SPR_MIRROR_JACKET_MASK], stage);
		maskShader->use();
		renderer->beginBatch();
		renderer->pushQuadMasked(mainWindowPos, DefaultJacket::getCenterUV(), mainWindowMask, {1, 1, 1, 0.8}, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(mirrorWindowPos, DefaultJacket::getMirrorCenterUV(), mirrorWindowMask, {1, 1, 1, 0.5}, jacket.getTexID(), stage.getID());
		renderer->endBatch();

		basicShader->use();
		renderer->beginBatch();
		renderer->pushQuad(mainWindowPos, mainWindowMask, DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(mirrorWindowPos, mirrorWindowMask, DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		sprite = &stage.sprites[SPR_SEKAI_FLOOR];
		renderer->drawRectangle({0, 1251}, {sprite->getWidth(), sprite->getHeight()}, stage, sprite->getX1(), sprite->getX2(), sprite->getY1(), sprite->getY2(), defaultTint.scaleAlpha(0.8f), 1);
		renderer->endBatch();
	}

	bool ScorePreviewBackground::shouldUpdate(const Jacket& jacket) const
	{
		return !init || backgroundFile != config.backgroundImage || jacketFile != jacket.getFilename();
	}

	void ScorePreviewBackground::draw(Renderer *renderer, float scrWidth, float scrHeight) const
	{
		float bgScrWidth = frameBuffer.getWidth(), bgScrHeight = frameBuffer.getHeight(), targetWidth, targetHeight;
		if (!backgroundFile.empty())
		{
			Utils::fillRect(scrWidth, scrHeight, bgScrWidth / bgScrHeight, bgScrWidth, bgScrHeight);
			targetWidth = Engine::STAGE_TARGET_WIDTH;
			targetHeight = Engine::STAGE_TARGET_HEIGHT;
		}
		else
		{
			bgScrWidth = Engine::BACKGROUND_SIZE;
			bgScrHeight = Engine::BACKGROUND_SIZE;
			targetWidth = Engine::STAGE_TARGET_WIDTH;
			targetHeight = Engine::STAGE_TARGET_HEIGHT;
		}
		
		const float bgWidth = bgScrWidth / (targetWidth * Engine::STAGE_WIDTH_RATIO);
		const float bgLeft = -bgWidth / 2;
		const float bgHeight = bgScrHeight / (targetHeight * Engine::STAGE_HEIGHT_RATIO);
		const float centerY = 0.5f / Engine::STAGE_HEIGHT_RATIO + Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float bgTop = centerY + -bgHeight / 2;
		auto vPos = Engine::quadvPos(bgLeft, bgLeft + bgWidth, bgTop, bgTop + bgHeight);
		auto uv = Engine::quadvPos(0, 1, 0, 1);
		renderer->pushQuad(vPos, uv, DirectX::XMMatrixIdentity(), DirectX::XMFLOAT4(brightness, brightness, brightness, 1), frameBuffer.getTexture(), -10);
	}

	void ScorePreviewWindow::drawMovie(ScoreContext& context, Renderer* renderer, float width, float height)
	{
		std::string file = config.pvMovieFile;
		if (file.empty())
		{
			// look for a 2DMV next to the music once per music file, not every frame
			if (movieMusic != context.workingData.musicFilename)
			{
				movieMusic = context.workingData.musicFilename;
				movieFound = Canvas::Video::MoviePlayer::findNextTo(movieMusic);
			}
			file = movieFound;
		}
		const bool opened = movie.open(file, config.ffmpegPath);
		movieStatus.found = movieFound;
		movieStatus.error = movie.getError();
		movieStatus.playing = opened ? file : std::string();
		movieStatus.decoder = movie.getDecoder();
		movieStatus.width = movie.getWidth();
		movieStatus.height = movie.getHeight();
		if (!opened)
			return;

		// cover the whole preview like the game does, keeping the movie's aspect ratio (same
		// placement as a custom background image); like the game's Background Brightness setting,
		// the brightness dims it, and the lanes go over it at the lane (stage) opacity
		float movieWidth{}, movieHeight{};
		Utils::fillRect(width, height, static_cast<long double>(movie.getWidth()) / movie.getHeight(), movieWidth, movieHeight);
		const float quadWidth = movieWidth / (Engine::STAGE_TARGET_WIDTH * Engine::STAGE_WIDTH_RATIO);
		const float quadHeight = movieHeight / (Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_HEIGHT_RATIO);
		const float left = -quadWidth / 2;
		const float centerY = 0.5f / Engine::STAGE_HEIGHT_RATIO + Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float top = centerY - quadHeight / 2;
		auto vPos = Engine::quadvPos(left, left + quadWidth, top, top + quadHeight);
		auto uv = Engine::quadvPos(0, 1, 0, 1);

		// the 2DMV starts with the music: chart time t plays music (and movie) position t - offset
		const double seconds = context.getTimeAtCurrentTick() - context.workingData.musicOffset / 1000.0 + config.pvMovieOffset / 1000.0;
		bool moviePicture = false;
		{
			CANVAS_PROFILE("movie.update");
			// the movie covers the frame (cropped on one side), so it is drawn at least frame-wide
			movie.setOutputWidth(static_cast<int>(lastPixelWidth * std::max(1.0f, movieWidth / std::max(1.0f, width))));
			moviePicture = movie.update(seconds, movieWaitsForFrames);
		}
		movieCovers = moviePicture;
		if (!moviePicture)
		{
			// before the MV starts and after it ends the game shows black, not the jacket background
			// (a fill bigger than the movie's own quad: the preview can be any shape)
			static unsigned int white = 0;
			if (!white)
			{
				const unsigned char pixel[4] = { 255, 255, 255, 255 };
				glGenTextures(1, &white);
				glBindTexture(GL_TEXTURE_2D, white);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
			const float big = std::max(quadWidth, quadHeight) * 4.0f;
			renderer->beginBatch();
			renderer->pushQuad(Engine::quadvPos(-big, big, centerY - big, centerY + big), uv, DirectX::XMMatrixIdentity(), DirectX::XMFLOAT4(0, 0, 0, 1), white, -9);
			renderer->endBatch();
			return;
		}

		const float brightness = config.pvBackgroundBrightness;
		renderer->beginBatch();
		renderer->pushQuad(vPos, uv, DirectX::XMMatrixIdentity(), DirectX::XMFLOAT4(brightness, brightness, brightness, 1), movie.getTexture(), -9);
		// the game's MV lines over the movie: one stripe period = 4 (or 6) px of a 1080p frame
		if (const unsigned int stripes = stripeTexture(config.pvMvLines); stripes && config.pvMvLineOpacity > 0.0f)
		{
			const float period = config.pvMvLines == 1 ? 4.0f : 6.0f;
			const float framePixelsPerUnit = lastPixelHeight > 0 ? lastPixelHeight / height : 1.0f;
			const float pitch = period * (height / 1080.0f) * framePixelsPerUnit;
			const float repeats = std::max(1.0f, movieWidth * framePixelsPerUnit / std::max(0.5f, pitch));
			auto stripeUv = Engine::quadvPos(0, repeats, 0, 1);
			const float strength = std::min(config.pvMvLineOpacity, 2.0f);
			for (float layer = strength; layer > 0.0f; layer -= 1.0f)
				renderer->pushQuad(vPos, stripeUv, DirectX::XMMatrixIdentity(), DirectX::XMFLOAT4(1, 1, 1, std::min(layer, 1.0f)), stripes, -8);
		}
		renderer->endBatch();
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getLeftUV()
	{
		return {{	
			{  303.8 / 740, 504.8 / 740, 0, 0 },
			{  317.5 / 740, 297.7 / 740, 0, 0 },
			{    5.5 / 740, 278.3 / 740, 0, 0 },
			{     -8 / 740, 497.4 / 740, 0, 0 }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getRightUV()
	{
		return {{
			{ 749.5 / 740, 377.7 / 740, 0, 0 },
			{ 738.2 / 740, 188.1 / 740, 0, 0 },
			{ 415.0 / 740, 171.4 / 740, 0, 0 },
			{ 432.1 / 740, 363.9 / 740, 0, 0 },
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getLeftMirrorUV()
	{
		return {{
			{ 292.761414 / 740, 247.401382 / 740, 0, 0 },
			{ 310.765869 / 740, 491.944763 / 740, 0, 0 },
			{   6.892246 / 740, 498.470642 / 740, 0, 0 },
			{  -6.246704 / 740, 258.264862 / 740, 0, 0 }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getRightMirrorUV()
	{
		return {{
			{ 733.444458 / 740, 183.954681 / 740, 0, 0 },	
			{ 743.541321 / 740, 355.960449 / 740, 0, 0 },
			{ 418.899414 / 740, 332.759491 / 740, 0, 0 },
			{ 410.746246 / 740, 155.907684 / 740, 0, 0 },
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getCenterUV()
	{
		return {{
			{ 755.541687 / 740, 744.057861 / 740, 0, 0 },
			{ 739.961182 / 740,  -1.859504 / 740, 0, 0 },
			{   0.043696 / 740,  -1.859504 / 740, 0, 0 },
			{ -17.484388 / 740, 744.057861 / 740, 0, 0 }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getMirrorCenterUV()
	{
		return {{
			{ 747.697083 / 740,   2.164453 / 740, 0, 0 },
			{ 743.909424 / 740, 731.297241 / 740, 0, 0 },
			{  -1.864066 / 740, 731.297241 / 740, 0, 0 },
			{   3.837242 / 740,   2.164453 / 740, 0, 0 }
		}};
	}

	ScorePreviewWindow::ScorePreviewWindow() : previewBuffer{ 1920, 1080 }, background(), scaledAspectRatio(1)
	{
		noteEffectsCamera.setFov(50.f);
		noteEffectsCamera.setRotation(-90.f, 27.1f);
		noteEffectsCamera.setPosition(DirectX::XMVectorSet(0, 5.32f, -5.86f, 0));
		noteEffectsCamera.positionCamNormal();
	}

	ScorePreviewWindow::~ScorePreviewWindow()
	{
	}

	void ScorePreviewWindow::update(ScoreContext& context, Renderer* renderer)
	{
		bool isWindowActive =  !ImGui::IsWindowDocked() || ImGui::GetCurrentWindow()->TabId == ImGui::GetWindowDockNode()->SelectedTabId;
		if (!isWindowActive)
			// Don't draw anything if the window is not active.
			// SFX and music updates are handled in the timeline
			return;
		if (context.scorePreviewDrawData.noteSpeed != config.pvNoteSpeed)
			context.scorePreviewDrawData.calculateDrawData(context.score);
		ImVec2 size = ImGui::GetContentRegionAvail() - ImVec2{ this->getScrollbarWidth(), 0 }; // Reserve space for the scrollbar
		ImVec2 position = ImGui::GetCursorScreenPos();
		ImRect boundaries = ImRect(position, position + size);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(boundaries.Min, boundaries.Max, 0xff202020);
		
		if (config.drawBackground && background.shouldUpdate(context.workingData.jacket))
			background.update(renderer, context.workingData.jacket);

		if (!context.scorePreviewDrawData.effectView.isInitialized())
			context.scorePreviewDrawData.effectView.init();

		if (playbackState.isPlaying)
		{
			context.scorePreviewDrawData.effectView.update(context);
		}
		else if (playbackState.wasLastFramePlaying)
		{
			context.scorePreviewDrawData.effectView.reset();
		}

		{
			CANVAS_PROFILE("preview.renderFrame");
			// everything the paused picture depends on (renderFrame reads no clock of its own)
			size_t key = 1469598103934665603ull;
			auto mix = [&key](size_t v) { key = (key ^ v) * 1099511628211ull; };
			auto mixf = [&mix](float v) { mix(std::hash<float>{}(v)); };
			mix(static_cast<size_t>(context.currentTick));
			mix(static_cast<size_t>(size.x));
			mix(static_cast<size_t>(size.y));
			mix(context.revision);
			// stage data lives in the score (revision covers edits); the eye / solo switches do not
			mix(context.score.stages.enabled ? 1 : 0);
			mix(static_cast<size_t>(Canvas::Stages::soloStage + 1));
			for (int hidden : Canvas::Stages::hiddenStages)
				mix(static_cast<size_t>(hidden) * 2654435761ull + 7);
			mix(static_cast<size_t>(context.currentLayer));
			mixf(context.workingData.musicOffset);
			mix(std::hash<std::string>{}(context.workingData.jacket.getFilename()));
			mix((config.pvMirrorScore << 0) | (config.pvFlickAnimation << 1) | (config.pvSimultaneousLine << 2) | (config.pvHoldAnimation << 3) |
				(config.pvDrawMovie << 4) | (config.drawBackground << 5));
			for (float v : { config.pvNoteSpeed, config.pvHoldAlpha, config.pvGuideAlpha, config.pvStageCover, config.pvStageOpacity,
				config.pvBackgroundBrightness, config.pvMovieOffset, config.pvMvLineOpacity })
				mixf(v);
			mix(static_cast<size_t>(config.pvEffectsProfile * 31 + config.notesSkin * 7 + config.pvMvLines));
			mix(std::hash<std::string>{}(config.pvMovieFile));
			const double now = ImGui::GetTime();
			// Auto quality: once a second of playback, compare the frame rate with the target
			if (config.pvRenderScale == 0 && playbackState.isPlaying)
			{
				if (autoWindowStart < 0.0)
				{
					autoWindowStart = now;
					autoFrames = 0;
				}
				++autoFrames;
				if (now - autoWindowStart >= 1.0)
				{
					// with vsync the rate tops out at 60: below 57 drops a step, five smooth seconds in a
					// row (or plenty of room without vsync) try a step back up
					const double fps = autoFrames / (now - autoWindowStart);
					autoSmoothSeconds = fps >= 59.0 ? autoSmoothSeconds + 1 : 0;
					if (fps < 57.0)
						autoScale = std::max(0.5f, autoScale - 0.15f);
					else if (fps > 75.0 || autoSmoothSeconds >= 5)
					{
						autoScale = std::min(1.0f, autoScale + 0.1f);
						autoSmoothSeconds = 0;
					}
					autoWindowStart = now;
					autoFrames = 0;
				}
			}
			else
				autoWindowStart = -1.0;
			const float scale = renderScale();
			const int pixelWidth = std::max(1, static_cast<int>(size.x * scale)), pixelHeight = std::max(1, static_cast<int>(size.y * scale));
			mixf(scale);
			const bool reuse = !playbackState.isPlaying && !playbackState.wasLastFramePlaying && key == lastFrameKey &&
				lastFrameAt >= 0.0 && now - lastFrameAt < 0.25 && previewBuffer.getWidth() == static_cast<unsigned int>(pixelWidth) &&
				previewBuffer.getHeight() == static_cast<unsigned int>(pixelHeight);
			if (!reuse)
			{
				if (!renderFrame(context, renderer, pixelWidth, pixelHeight, previewBuffer))
					return;
				lastFrameKey = key;
				lastFrameAt = now;
			}
		}

		drawList->AddImage((ImTextureID)(size_t)previewBuffer.getTexture(), position, position + size, {0, 1}, {1, 0});
		CANVAS_PROFILE("preview.hudOverlay");
		drawHudOverlay(context, drawList, position, size);
	}

	// A Score Preview Animation transform as a clip-space matrix, applied after the projection (row
	// vectors): turns and scales round the picture's middle, then moves. Turning has to happen in pixels,
	// hence the width / height factors.
	DirectX::XMMATRIX ScorePreviewWindow::animationMatrix(const Canvas::Animation::Transform& t, float width, float height)
	{
		if (t.identity() || width <= 0 || height <= 0)
			return DirectX::XMMatrixIdentity();
		const float r = t.rotation * 3.14159265f / 180.0f;
		const float c = std::cos(r) * t.scale, s = std::sin(r) * t.scale;
		const float unit = height / 1080.0f;
		return DirectX::XMMATRIX(
			c, -s * width / height, 0, 0,
			s * height / width, c, 0, 0,
			0, 0, 1, 0,
			2.0f * t.x * unit / width, -2.0f * t.y * unit / height, 0, 1);
	}

	float ScorePreviewWindow::renderScale() const
	{
		// paused, Auto shows full quality: the picture is reused anyway
		if (config.pvRenderScale == 0)
			return playbackState.isPlaying ? autoScale : 1.0f;
		return std::clamp(config.pvRenderScale / 100.0f, 0.25f, 1.0f);
	}

	bool ScorePreviewWindow::renderFrame(ScoreContext& context, Renderer* renderer, int pixelWidth, int pixelHeight, Framebuffer& target)
	{
		if (pixelWidth < 1 || pixelHeight < 1)
			return false;

		static int shaderId = ResourceManager::getShader("basic2d");
		static int pteShaderId = ResourceManager::getShader("particles");
		if (shaderId == -1 || pteShaderId == -1)
			return false;

		Shader* shader = ResourceManager::shaders[shaderId];
		Shader* pteShader = ResourceManager::shaders[pteShaderId];
		shader->use();

		// Create a projection to the engine coordinate system
		float width  = static_cast<float>(pixelWidth), height = static_cast<float>(pixelHeight);
		float scaledWidth = Engine::STAGE_TARGET_WIDTH * Engine::STAGE_WIDTH_RATIO;
		float scaledHeight = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_HEIGHT_RATIO;
		float scrTop  = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_TOP_RATIO;
		Utils::fillRect(Engine::STAGE_TARGET_WIDTH, Engine::STAGE_TARGET_HEIGHT, width / height, width, height);
		lastTargetWidth = width;
		lastTargetHeight = height;
		lastPixelHeight = static_cast<float>(pixelHeight);
		lastPixelWidth = pixelWidth;
		
		float aspectRatio = width / height;
		scaledAspectRatio = scaledWidth / scaledHeight;

		float currentTime = context.getTimeAtCurrentTick();
		prepareStages(context);
		const bool stageCover = config.pvStageCover != 0 && !stagesOn;	// the cover is made for the default stage

		// Score Preview Animation: the screen track moves everything (background and MV too), the stage
		// track only the lanes, notes and their effects
		DirectX::XMMATRIX screenMatrix = DirectX::XMMatrixIdentity(), stageMatrix = DirectX::XMMatrixIdentity();
		if (const auto& anim = context.score.animation; !anim.empty())
		{
			auto toSeconds = [&context](int tick) { return static_cast<double>(accumulateDuration(tick, TICKS_PER_BEAT, context.score.tempoChanges)); };
			screenMatrix = animationMatrix(Canvas::Animation::screenAt(anim, context.currentTick, currentTime, toSeconds), width, height);
			stageMatrix = animationMatrix(Canvas::Animation::evaluate(anim, "stage", context.currentTick), width, height) * screenMatrix;
		}

		auto view = DirectX::XMMatrixScaling(scaledWidth, scaledHeight, 1.f) * DirectX::XMMatrixTranslation(0.f, -scrTop, 0.f);
		auto projection = Camera::getOffCenterOrthographicProjection(-width / 2, width / 2, height / 2, -height / 2);
		const auto backgroundProjection = view * projection * screenMatrix;
		auto viewProjection = view * projection * stageMatrix;

		const auto pView = noteEffectsCamera.getViewMatrix();
		auto pProjection = noteEffectsCamera.getProjectionMatrix(aspectRatio, 0.3, 1000);
		float projectionScale = std::min(aspectRatio / EFFECTS_TARGET_ASPECT, 1.f);
		pProjection = DirectX::XMMatrixScaling(projectionScale, projectionScale, 1.f) * pProjection * stageMatrix;

		shader->setMatrix4("projection", backgroundProjection);

		if (target.getWidth() != static_cast<unsigned int>(pixelWidth) || target.getHeight() != static_cast<unsigned int>(pixelHeight))
			target.resize(pixelWidth, pixelHeight);
		target.bind();
		target.clear();

		{
			CANVAS_PROFILE_GPU("gpu.background");
			renderer->beginBatch();
			if (config.drawBackground && !(config.pvDrawMovie && movie.isOpen() && movieCovers))
			{
				background.setBrightness(config.pvBackgroundBrightness);
				background.draw(renderer, width, height);
			}
			renderer->endBatch();
		}
		if (config.pvDrawMovie)
		{
			CANVAS_PROFILE_GPU("gpu.movie");
			drawMovie(context, renderer, width, height);
		}

		{
			CANVAS_PROFILE_GPU("gpu.stage");
			shader->setMatrix4("projection", viewProjection);
			renderer->beginBatch();
			if (stagesOn)
				drawStagePlanes(renderer);
			else
				drawStage(renderer);
			renderer->endBatch();
		}

		{
			CANVAS_PROFILE("cpu.updateEffects");
			context.scorePreviewDrawData.effectView.updateEffects(context, noteEffectsCamera, currentTime);
		}
		CANVAS_PROFILE_GPU("gpu.notesAndEffects");

		shader->use();
		shader->setMatrix4("projection", viewProjection);
		renderer->beginBatch();
		drawLines(context, renderer);
		drawHoldCurves(context, renderer);
		if (stageCover) {
			drawStageCoverMask(renderer);
			renderer->endBatchWithDepthTest(GL_LEQUAL); // using depth test to cull the notes drawn
		}
		else
			renderer->endBatch();

		pteShader->use();
		pteShader->setMatrix4("projection", pProjection);
		pteShader->setMatrix4("view", pView);
		renderer->beginBatch();
		context.scorePreviewDrawData.effectView.drawUnderNoteEffects(renderer, currentTime);
		renderer->endBatchWithBlending(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		shader->use();
		shader->setMatrix4("projection", viewProjection);
		renderer->beginBatch();

		drawHoldTicks(context, renderer);
		drawNotes(context, renderer);
		if (stageCover) {
			drawStageCoverMask(renderer);
			drawStageCover(renderer);
			drawStageCoverDecoration(renderer);
			renderer->endBatchWithDepthTest(GL_LEQUAL); // using depth test to cull the notes drawn
		}
		else
			renderer->endBatch();

		pteShader->use();
		pteShader->setMatrix4("projection", pProjection);
		pteShader->setMatrix4("view", pView);
		renderer->beginBatch();
		context.scorePreviewDrawData.effectView.drawEffects(renderer, currentTime);
		renderer->endBatchWithBlending(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		target.unblind();
		return true;
	}

	void ScorePreviewWindow::updateUI(ScoreEditorTimeline& timeline, ScoreContext& context)
	{
		updateHudControls(timeline, context);
		updateToolbar(timeline, context);
		ImGuiIO io = ImGui::GetIO();
		float mouseWheel = io.MouseWheel * 1;
		if (!timeline.isPlaying() && ImGui::IsWindowHovered() && mouseWheel != 0)
		{
			context.currentTick += std::max(mouseWheel * TICKS_PER_BEAT / 2, (float)-context.currentTick);
			//timeline.focusCursor(context.currentTick, mouseWheel > 0 ? Direction::Up : Direction::Down);
		}
		updateScrollbar(timeline, context);

		playbackState.wasLastFramePlaying = playbackState.isPlaying;
		playbackState.isPlaying = timeline.isPlaying();

		if (isFullWindow())
		{
			if (ImGui::BeginPopupContextWindow("preview_context_menu", ImGuiPopupFlags_NoReopen | ImGuiPopupFlags_MouseButtonDefault_))
			{
				bool _fullWindow = fullWindow;
				if (ImGui::MenuItem(getString("fullscreen_preview"), ToShortcutString(config.input.togglePreviewFullWindow), &_fullWindow))
					setFullWindow(_fullWindow);

				ImGui::MenuItem(getString("preview_draw_toolbar"), NULL, &config.pvDrawToolbar);
				ImGui::MenuItem(getString("preview_draw_score_hud"), NULL, &config.pvDrawScoreHud);
				ImGui::MenuItem(getString("preview_draw_life_hud"), NULL, &config.pvDrawLifeHud);
				ImGui::MenuItem(getString("preview_draw_combo"), NULL, &config.pvDrawCombo);
				ImGui::MenuItem(getString("preview_draw_perfect"), NULL, &config.pvDrawPerfect);
				drawPreviewEffectMenuItems();
				ImGui::MenuItem(getString("return_to_last_tick"), NULL, &config.returnToLastSelectedTickOnPause);
				ImGui::EndPopup();
			}
		}
	}

	void ScorePreviewWindow::updateHudControls(ScoreEditorTimeline& timeline, ScoreContext& context) const
	{
		if (!config.pvDrawLifeHud)
			return;

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 previewPosition = ImGui::GetCursorScreenPos();
		const ImVec2 previewSize = ImGui::GetContentRegionAvail() - ImVec2{ this->getScrollbarWidth(), 0.0f };
		if (previewSize.x <= 1.0f || previewSize.y <= 1.0f)
			return;

		const float uiScale = std::min(previewSize.x / 1920.0f, previewSize.y / 1080.0f);
		const float offsetX = (previewSize.x - 1920.0f * uiScale) * 0.5f;
		const ImVec2 buttonCenter{
			previewPosition.x + offsetX + HUD_PAUSE_BUTTON_CENTER_X * uiScale,
			previewPosition.y + HUD_PAUSE_BUTTON_CENTER_Y * uiScale
		};
		const float radius = HUD_PAUSE_BUTTON_RADIUS * uiScale;
		const ImVec2 delta = io.MousePos - buttonCenter;
		const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
			delta.x * delta.x + delta.y * delta.y <= radius * radius;

		if (!hovered)
			return;

		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			timeline.setPlaying(context, !timeline.isPlaying());
	}

	void ScorePreviewWindow::setFullWindow(bool _fullWindow)
	{
		fullWindow = _fullWindow;
	}

	void ScorePreviewWindow::loadNoteEffects(Effect::EffectView& effectView)
	{
		// built-in normal (0) / reduced (1), or a custom effect skin (2+): each file from the skin when it
		// has it, else from the default set (Canvas/Preview/CustomSkins)
		static std::string loadedTexture;
		const int profile = config.pvEffectsProfile < Canvas::CustomSkins::effectProfileCount() ? config.pvEffectsProfile : 0;
		size_t effectCount = arrayLength(Effect::effectNames);

		// Cleanup. We don't want all profiles and their resources loaded in memory
		ResourceManager::removeAllParticleEffects();
		int texIndex = loadedTexture.empty() ? -1 : ResourceManager::getTextureByFilename(loadedTexture);
		if (texIndex > -1)
			ResourceManager::disposeTexture(ResourceManager::textures[texIndex].getID());

		loadedTexture = Canvas::CustomSkins::effectFile(profile, "tex_note_common_all_v2.png");
		ResourceManager::loadTexture(loadedTexture);

		std::vector<std::string> failedParticleFiles;
		for (size_t i = 0; i < effectCount; i++)
		{
			const std::string filename{ Canvas::CustomSkins::effectFile(profile, std::string(Effect::effectNames[i]) + ".json") };
			int particleId = ResourceManager::loadParticleEffect(filename);

			if (particleId == -1)
				failedParticleFiles.push_back(filename);
		}

		if (!failedParticleFiles.empty())
		{
			std::string fullErrorMessage = "Failed to load the following note effects: \n\n";
			for (const auto& error : failedParticleFiles)
				fullErrorMessage.append(error).append("\n");

			IO::messageBox(
				APP_NAME,
				fullErrorMessage,
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Warning,
				Application::windowState.windowHandle
			);
		}

		effectView.reset();
		effectView.init();
	}

	Canvas::Preview::OverlayOptions ScorePreviewWindow::overlayOptions(bool forVideo)
	{
		Canvas::Preview::OverlayOptions options;
		const bool ui = !forVideo || config.videoDrawUi;
		options.score = ui && config.pvDrawScoreHud;
		options.life = ui && config.pvDrawLifeHud;
		options.combo = ui && config.pvDrawCombo;
		options.judge = ui && config.pvDrawPerfect;
		options.skills = ui && config.pvDrawSkills;
		options.fever = false;	// FEVER is not drawn in the preview or in exported videos
		options.allPerfect = config.pvDrawAllPerfect;
		options.scoreDelta = ui && config.pvDrawScoreDelta;
		options.cutins = ui && config.pvDrawCutins;
		options.apCombo = config.pvApCombo;
		options.superFever = false;
		options.ui280 = config.pvUi280;
		options.intro = forVideo && config.pvDrawIntro;	// the start animation is for exported videos only
		// Multi-Live / Cheerful Carnival are hidden: always the solo HUD (the code stays)
		options.mode = Canvas::Preview::LiveMode::Solo;
		(void)forVideo;
		return options;
	}

	Canvas::Preview::Projector ScorePreviewWindow::projector(ImVec2 position, ImVec2 size) const
	{
		const float targetWidth = lastTargetWidth, targetHeight = lastTargetHeight;
		if (targetWidth <= 0 || targetHeight <= 0)
			return {};
		// Dynamic Stages: the stage 0 + camera picture, so the overlay's lane edges move with the camera
		if (hudStageValid)
		{
			const Canvas::Stages::StageProjection proj = hudProjection;
			return [=](float laneX, float depth) {
				const float d = std::max(depth, 1e-4f);
				const auto p = proj.apply(laneX / d, d);
				return ImVec2(position.x + (0.5f + (p.x - 0.5f) * Engine::STAGE_TARGET_WIDTH / targetWidth) * size.x,
					position.y + (0.5f + (p.y - 0.5f) * Engine::STAGE_TARGET_HEIGHT / targetHeight) * size.y);
			};
		}
		// engine -> world (renderFrame's view) -> pixels of the frame, y down
		return [=](float laneX, float depth) {
			const float scaledWidth = Engine::STAGE_TARGET_WIDTH * Engine::STAGE_WIDTH_RATIO;
			const float scaledHeight = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_HEIGHT_RATIO;
			const float scrTop = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_TOP_RATIO;
			const float wx = laneX * scaledWidth, wy = depth * scaledHeight - scrTop;
			return ImVec2(position.x + (0.5f + wx / targetWidth) * size.x, position.y + (0.5f + wy / targetHeight) * size.y);
		};
	}

	void ScorePreviewWindow::drawHudOverlay(const ScoreContext& context, ImDrawList* drawList, ImVec2 position, ImVec2 size)
	{
		const auto options = overlayOptions(false);
		Canvas::Preview::drawLiveOverlay(drawList, position, size, context, options, projector(position, size));
		Canvas::Preview::drawAnimationEffects(drawList, position, size, context);
		// FEVER lights up the whole editor, not just the preview
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		Canvas::Preview::drawEditorFeverFrame(ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport)), viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), context, options);
	}

	const Texture &ScorePreviewWindow::getNoteTexture()
	{
		return ResourceManager::textures[noteSkins.getItemIndex(NoteSkinItem::Notes)];
	}

	// ======================================================================== Dynamic Stages
	// Only runs while Score::stages is enabled and has data. A normal chart never gets here: every
	// helper below is behind `stagesOn`, which stays false.
	namespace
	{
		constexpr float kStageScaledW = Engine::STAGE_TARGET_WIDTH * Engine::STAGE_WIDTH_RATIO;
		constexpr float kStageScaledH = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_HEIGHT_RATIO;
		constexpr float kStageScrTop = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_TOP_RATIO;

		// a point of the 16:9 picture (0..1, y down) -> world space, what renderFrame's viewProjection takes.
		// Same maths as ScorePreviewWindow::projector, just backwards.
		DirectX::XMFLOAT2 pictureToWorld(const Canvas::Stages::Point& p)
		{
			return {
				(p.x - 0.5f) * Engine::STAGE_TARGET_WIDTH / kStageScaledW,
				((p.y - 0.5f) * Engine::STAGE_TARGET_HEIGHT + kStageScrTop) / kStageScaledH
			};
		}

		// holds ride the stage of their start note; ids that are not stages any more fall back to stage 0
		int stageIdOf(const Score& score, const Note& note)
		{
			const Note* n = &note;
			if (note.getType() == NoteType::HoldMid || note.getType() == NoteType::HoldEnd)
			{
				auto it = score.notes.find(note.parentID);
				if (it != score.notes.end())
					n = &it->second;
			}
			const int id = score.stages.stageOf(n->ID);
			return score.stages.find(id) ? id : 0;
		}
	}

	void ScorePreviewWindow::prepareStages(const ScoreContext& context)
	{
		using namespace Canvas::Stages;
		stageFrames.clear();
		stageNow = nullptr;
		lineStages.clear();
		hudStageValid = false;
		const StageSet& set = context.score.stages;
		stagesOn = set.enabled && !set.empty();
		if (!stagesOn)
			return;

		const auto toSeconds = [&context](double tick) {
			return static_cast<double>(accumulateDuration(static_cast<int>(std::floor(tick)), TICKS_PER_BEAT, context.score.tempoChanges));
		};
		const double tick = context.currentTick;
		const CameraState cam = camera(set, tick, toSeconds);
		const float noteDuration = Engine::getNoteDuration(config.pvNoteSpeed);
		const float bpm = getTempoAt(context.currentTick, context.score.tempoChanges).bpm;
		const bool mirror = config.pvMirrorScore;

		for (const Stage& s : set.stages)
		{
			if (hiddenStages.count(s.id) || (soloStage >= 0 && soloStage != s.id))
				continue;
			StageFrame f;
			f.id = s.id;
			f.state = evaluate(set, s.id, tick, toSeconds);
			if (!f.state.visible)
				continue;
			f.proj = StageProjection(f.state, cam);
			// the beat offset is in beats of scroll: beats -> seconds -> the share of a note's fall
			f.shift = f.state.yOffset + (bpm > 0 && noteDuration > 0 ? f.state.yBeatOffset * 60.0f / bpm / noteDuration : 0.0f);
			const float lo = f.state.maskLane - f.state.maskHalfWidth, hi = f.state.maskLane + f.state.maskHalfWidth;
			f.maskLo = mirror ? -hi : lo;
			f.maskHi = mirror ? -lo : hi;
			stageFrames.push_back(f);
		}
		// higher elevation sits further back, so it goes first; ids break ties
		std::stable_sort(stageFrames.begin(), stageFrames.end(), [](const StageFrame& a, const StageFrame& b) {
			if (a.state.elevation != b.state.elevation)
				return a.state.elevation > b.state.elevation;
			return a.id < b.id;
		});

		hudProjection = StageProjection(evaluate(set, 0, tick, toSeconds), cam);
		hudStageValid = true;

		if (config.pvSimultaneousLine)
			for (const auto& dn : context.scorePreviewDrawData.drawingNotes)
			{
				auto it = context.score.notes.find(dn.refID);
				if (it != context.score.notes.end())
					lineStages.emplace(it->second.tick, stageIdOf(context.score, it->second));
			}
	}

	void ScorePreviewWindow::useStageById(int stageId)
	{
		stageNow = nullptr;
		for (const StageFrame& f : stageFrames)
			if (f.id == stageId)
			{
				stageNow = &f;
				return;
			}
	}

	void ScorePreviewWindow::useStageOf(const ScoreContext& context, const Note& note)
	{
		if (stagesOn)
			useStageById(stageIdOf(context.score, note));
	}

	void ScorePreviewWindow::emitQuad(Renderer* renderer, const std::array<DirectX::XMFLOAT4, 4>& vPos, const DirectX::XMMATRIX& model,
		const Texture& tex, float x1, float x2, float y1, float y2, const Color& tint, int z)
	{
		if (!stagesOn)
		{
			renderer->drawQuad(vPos, model, tex, x1, x2, y1, y2, tint, z);
			return;
		}
		const StageFrame* frame = stageNow;
		if (!frame || frame->state.noteAlpha <= 0.002f)
			return;
		const auto& st = frame->state;

		// where the normal path would put the corners: world = (lane * depth, depth)
		float wx[4], wy[4];
		for (int i = 0; i < 4; ++i)
		{
			const DirectX::XMVECTOR v = DirectX::XMVector2Transform(DirectX::XMLoadFloat4(&vPos[i]), model);
			wx[i] = DirectX::XMVectorGetX(v);
			wy[i] = DirectX::XMVectorGetY(v);
		}

		const float tw = static_cast<float>(tex.getWidth()), th = static_cast<float>(tex.getHeight());
		const float ul = x1 / tw, ur = x2 / tw, vt = y1 / th, vb = y2 / th;
		std::array<DirectX::XMFLOAT4, 4> uv = {{ { ur, vt, 0, 0 }, { ur, vb, 0, 0 }, { ul, vb, 0, 0 }, { ul, vt, 0, 0 } }};
		std::array<DirectX::XMFLOAT4, 4> pos;
		const bool perspective = vPos[0].z > 0.0f;

		if (perspective)
		{
			// quads lying on the lane: the exact projection of every corner
			float lane[4], depth[4];
			for (int i = 0; i < 4; ++i)
			{
				depth[i] = std::max(wy[i], 1e-4f);
				lane[i] = wx[i] / depth[i];
			}
			if (st.maskNotes)
			{
				// clip to the lit lanes: pull each corner in along its row (0-3 / 1-2) and slide its u with it
				bool visible = false;
				const int partner[4] = { 3, 2, 1, 0 };
				const auto uvOld = uv;
				float newLane[4];
				for (int i = 0; i < 4; ++i)
				{
					const int j = partner[i];
					float c = std::clamp(lane[i], frame->maskLo, frame->maskHi);
					const float span = lane[j] - lane[i];
					if (std::abs(span) > 1e-6f)
					{
						const float s = (c - lane[i]) / span;
						uv[i].x = uvOld[i].x + (uvOld[j].x - uvOld[i].x) * s;
					}
					newLane[i] = c;
					if (lane[i] > frame->maskLo && lane[i] < frame->maskHi)
						visible = true;
				}
				// a quad can also straddle the mask with both corners of a row outside it
				for (int row = 0; row < 2 && !visible; ++row)
				{
					const int a = row == 0 ? 0 : 1, b = row == 0 ? 3 : 2;
					const float lo = std::min(lane[a], lane[b]), hi = std::max(lane[a], lane[b]);
					if (hi > frame->maskLo && lo < frame->maskHi)
						visible = true;
				}
				if (!visible)
					return;
				for (int i = 0; i < 4; ++i)
					lane[i] = newLane[i];
			}
			for (int i = 0; i < 4; ++i)
			{
				const auto w = pictureToWorld(frame->proj.apply(lane[i], depth[i] + frame->shift));
				pos[i] = { w.x, w.y, vPos[i].z, 1.0f };
			}
		}
		else
		{
			// flat sprites (diamonds, arrows, hold ticks): project the middle, carry the corners over with the
			// projection's local scale / turn (a fixed 2x2 matrix from two small steps), so they stay the
			// same sprite but follow the stage
			const float cx = (wx[0] + wx[1] + wx[2] + wx[3]) * 0.25f, cy = (wy[0] + wy[1] + wy[2] + wy[3]) * 0.25f;
			const float d = std::max(cy, 1e-4f), lane = cx / d;
			if (st.maskNotes && (lane < frame->maskLo || lane > frame->maskHi))
				return;
			const float dl = 0.01f, dd = d * 0.01f;
			const auto p0 = pictureToWorld(frame->proj.apply(lane, d + frame->shift));
			const auto pl = pictureToWorld(frame->proj.apply(lane + dl, d + frame->shift));
			const auto pd = pictureToWorld(frame->proj.apply(lane, d + frame->shift + dd));
			const float wlx = (pl.x - p0.x) / dl, wly = (pl.y - p0.y) / dl;
			const float wdx = (pd.x - p0.x) / dd, wdy = (pd.y - p0.y) / dd;
			// world offset (ox, oy) from the middle -> lane step ox / d, depth step oy - lane * ox / d
			for (int i = 0; i < 4; ++i)
			{
				const float ox = wx[i] - cx, oy = wy[i] - cy;
				const float al = ox / d, ad = oy - lane * ox / d;
				pos[i] = { p0.x + wlx * al + wdx * ad, p0.y + wly * al + wdy * ad, 0.0f, 1.0f };
			}
		}

		static const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
		const DirectX::XMFLOAT4 color{ tint.r, tint.g, tint.b, tint.a * st.noteAlpha };
		renderer->pushQuad(pos, uv, identity, color, tex.getID(), z);
	}

	// The lanes of every visible stage: fill, dividers, borders and the judge line, drawn as plain
	// patches in lane space so they follow the stage's mask, pivot and projection exactly.
	void ScorePreviewWindow::drawStagePlanes(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& tex = ResourceManager::textures[index];
		const float tw = static_cast<float>(tex.getWidth()), th = static_cast<float>(tex.getHeight());
		if (tw <= 0 || th <= 0)
			return;
		const float mirror = config.pvMirrorScore ? -1.0f : 1.0f;
		const float opacity = config.pvStageOpacity;

		// flat colours read out of the stage sheet: lane fill, divider line, judge line purple
		struct Texel { float u0, u1, v0, v1; };
		const auto texel = [&](float x, float y) { return Texel{ (x + 0.25f) / tw, (x + 0.75f) / tw, (y + 0.25f) / th, (y + 0.75f) / th }; };
		const Texel fillTex = texel(1018, 600), lineTex = texel(1023, 600), judgeTex = texel(1018, 835);

		constexpr float laneTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;	// where the sheet's lanes start
		constexpr float laneBottom = 1.45f;
		const float halfNote = Engine::getNoteHeight();

		static const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
		for (size_t idx = 0; idx < stageFrames.size(); ++idx)
		{
			const StageFrame& f = stageFrames[idx];
			const auto& st = f.state;
			const int zBase = static_cast<int>(idx) * 4;

			// a lane / depth rectangle, corners projected one by one
			const auto patch = [&](float l0, float l1, float d0, float d1, const Texel& t, const Color& tint, int z) {
				if (tint.a <= 0.002f || l1 <= l0 || d1 <= d0)
					return;
				const float ls[4] = { l1, l1, l0, l0 }, ds[4] = { d1, d0, d0, d1 };
				std::array<DirectX::XMFLOAT4, 4> pos;
				const std::array<DirectX::XMFLOAT4, 4> uv = {{ { t.u1, t.v0, 0, 0 }, { t.u1, t.v1, 0, 0 }, { t.u0, t.v1, 0, 0 }, { t.u0, t.v0, 0, 0 } }};
				for (int i = 0; i < 4; ++i)
				{
					const auto w = pictureToWorld(f.proj.apply(ls[i] * mirror, ds[i]));
					pos[i] = { w.x, w.y, 0.0f, 1.0f };
				}
				renderer->pushQuad(pos, uv, identity, DirectX::XMFLOAT4{ tint.r, tint.g, tint.b, tint.a }, tex.getID(), z);
			};

			const float lo = st.maskLane - st.maskHalfWidth, hi = st.maskLane + st.maskHalfWidth;
			if (hi <= lo)
				continue;
			patch(lo, hi, laneTop, laneBottom, fillTex, defaultTint.scaleAlpha(opacity * st.laneAlpha), zBase);

			// dividers: every `size` lanes from the pivot, half a division over for odd parity
			const auto dividers = [&](int size, int parity, float alpha) {
				if (alpha <= 0.002f)
					return;
				const float step = static_cast<float>(std::max(size, 1));
				for (int k = -64; k <= 64; ++k)
				{
					const float p = st.pivotLane + (k + 0.5f * (parity & 1)) * step;
					if (p <= lo + 0.02f || p >= hi - 0.02f)
						continue;
					patch(p - 0.012f, p + 0.012f, laneTop, laneBottom, lineTex, defaultTint.scaleAlpha(opacity * alpha), zBase + 1);
				}
			};
			if (st.divisionSize == st.divisionSizeTo && st.divisionParity == st.divisionParityTo)
				dividers(st.divisionSize, st.divisionParity, st.divisionLineAlpha);
			else
			{
				dividers(st.divisionSize, st.divisionParity, st.divisionLineAlpha * (1 - st.divisionProgress));
				dividers(st.divisionSizeTo, st.divisionParityTo, st.divisionLineAlpha * st.divisionProgress);
			}

			// borders: 0 default, 1 light, 2 disabled, 3 medium
			const auto borderAlpha = [](int style) { return style == 2 ? 0.0f : style == 1 ? 0.55f : style == 3 ? 0.8f : 1.0f; };
			const int leftStyle = st.styleProgress < 0.5f ? st.leftBorder : st.leftBorderTo;
			const int rightStyle = st.styleProgress < 0.5f ? st.rightBorder : st.rightBorderTo;
			patch(lo - 0.03f, lo + 0.03f, laneTop, laneBottom, lineTex, defaultTint.scaleAlpha(opacity * st.laneAlpha * borderAlpha(leftStyle)), zBase + 1);
			patch(hi - 0.03f, hi + 0.03f, laneTop, laneBottom, lineTex, defaultTint.scaleAlpha(opacity * st.laneAlpha * borderAlpha(rightStyle)), zBase + 1);

			// judge line: the box round where notes land (two bars), or one bar for the single line style
			const float judge = 1.0f + f.shift;
			const auto judgeLine = [&](int color, int style, float alpha) {
				if (alpha <= 0.002f)
					return;
				static const float tints[8][3] = { { 1, 1, 1 }, { 1.0f, 0.35f, 0.35f }, { 0.4f, 1.0f, 0.5f }, { 0.4f, 0.6f, 1.0f },
					{ 1.0f, 0.9f, 0.3f }, { 0.8f, 0.4f, 1.0f }, { 0.3f, 0.9f, 1.0f }, { 0.1f, 0.1f, 0.1f } };
				const int c = std::clamp(color, 0, 7);
				const Texel& t = c == 0 ? judgeTex : lineTex;
				const Color tint{ tints[c][0], tints[c][1], tints[c][2], opacity * alpha };
				const float bar = 0.008f;
				const float a0 = lo - 0.05f, a1 = hi + 0.05f;
				if (style == 1)
					patch(a0, a1, judge - bar, judge + bar, t, tint, zBase + 2);
				else
				{
					patch(a0, a1, judge - halfNote, judge - halfNote + bar, t, tint, zBase + 2);
					patch(a0, a1, judge + halfNote - bar, judge + halfNote, t, tint, zBase + 2);
				}
			};
			if (st.judgeLineColor == st.judgeLineColorTo && st.judgeLineStyle == st.judgeLineStyleTo)
				judgeLine(st.judgeLineColor, st.judgeLineStyle, st.judgeLineAlpha);
			else
			{
				judgeLine(st.judgeLineColor, st.judgeLineStyle, st.judgeLineAlpha * (1 - st.styleProgress));
				judgeLine(st.judgeLineColorTo, st.judgeLineStyleTo, st.judgeLineAlpha * st.styleProgress);
			}
		}
	}

	void ScorePreviewWindow::drawStage(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		constexpr float stageLeft  = -stageWidth / 2;
		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		constexpr float stageHeight = Engine::STAGE_TEX_HEIGHT / Engine::STAGE_LANE_HEIGHT;
		renderer->drawRectangle({stageLeft, stageTop}, {stageWidth, stageHeight}, stage, stageSprite.getX1(), stageSprite.getX2(), stageSprite.getY1(), stageSprite.getY2(), defaultTint.scaleAlpha(config.pvStageOpacity), -1);
	}

	void ScorePreviewWindow::drawStageCoverMask(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		constexpr float stageLeft = -stageWidth / 2, stageRight = stageWidth / 2;

		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = config.pvStageCover * (1 - stageTop);
		const float spriteHeight = config.pvStageCover * (Engine::STAGE_LANE_HEIGHT - Engine::STAGE_LANE_TOP);

		static auto model = DirectX::XMMatrixTranslation(0, 0, 1);
		renderer->drawQuad(
			Engine::quadvPos(stageLeft, stageRight, stageTop + stageHeight, 0), model, stage,
			0, 0, 1, 1, defaultTint.scaleAlpha(0), 0
		);
	}

	void ScorePreviewWindow::drawStageCover(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		const float stageLeft = -stageWidth / 2, stageRight = stageWidth / 2;

		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = config.pvStageCover * (1 - stageTop);
		const float spriteHeight = config.pvStageCover * (Engine::STAGE_LANE_HEIGHT - Engine::STAGE_LANE_TOP);

		static auto model = DirectX::XMMatrixTranslation(0, 0, 1);
		renderer->drawQuad(
			Engine::quadvPos(stageLeft, stageRight, stageTop + stageHeight, stageTop), model, stage,
			stageSprite.getX1(), stageSprite.getX2(), stageSprite.getY1(), stageSprite.getY1() + spriteHeight,
			Color{0.f, 0.f, 0.f, config.pvStageOpacity}, 0
		);
	}

	void MikuMikuWorld::ScorePreviewWindow::drawStageCoverDecoration(Renderer *renderer)
	{
		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const Texture& noteTex = getNoteTexture();
		size_t sprIndex = SPR_SIMULTANEOUS_CONNECTION;
		size_t transIndex = static_cast<size_t>(SpriteType::SimultaneousLine);
		if (!isArrayIndexInBounds(sprIndex, noteTex.sprites)) return;
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms)) return;
		const SpriteTransform& lineTransform = ResourceManager::spriteTransforms[transIndex];
		const Sprite& sprite = noteTex.sprites[sprIndex];
		float x = 0.12 * (1 - config.pvStageCover);
		auto vPos = lineTransform.apply(Engine::perspectiveQuadvPos(-6 - x, 6 + x, 1. + Engine::getNoteHeight(), 1. - Engine::getNoteHeight()));
		float y = stageTop + config.pvStageCover * (1 - stageTop);
		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		renderer->drawQuad(vPos, model, noteTex,
			sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(),
			defaultTint.scaleAlpha(config.pvStageOpacity), -1
		);
	}

	// Scroll position of every hi-speed layer at the current tick (index = layer)
	static std::vector<double> currentScaledTimes(const ScoreContext& context)
	{
		const auto& drawData = context.scorePreviewDrawData;
		std::vector<double> times(std::max<size_t>(1, drawData.layerHiSpeeds.size()));
		for (size_t layer = 0; layer < times.size(); ++layer)
			times[layer] = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, drawData.hiSpeedsFor(static_cast<int>(layer)));
		return times;
	}

	static double scaledTimeOf(const std::vector<double>& times, int layer)
	{
		return layer >= 0 && layer < static_cast<int>(times.size()) ? times[layer] : times[0];
	}

	void ScorePreviewWindow::drawNotes(const ScoreContext& context, Renderer *renderer)
	{
		double current_tm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const std::vector<double> scaledTimes = currentScaledTimes(context);
		const auto& drawData = context.scorePreviewDrawData;
	
		for (auto& note : drawData.drawingNotes)
		{
			const double scaled_tm = scaledTimeOf(scaledTimes, note.layer);
			if (scaled_tm < note.visualTime.min || scaled_tm > note.visualTime.max)
				continue;
			const Note& noteData = context.score.notes.at(note.refID);
			if (stagesOn)
			{
				useStageOf(context, noteData);
				if (!stageNow)
					continue;
			}
			double y = Engine::approach(note.visualTime.min, note.visualTime.max, scaled_tm);
			float l = Engine::laneToLeft(noteData.lane), r = Engine::laneToLeft(noteData.lane) + noteData.width;
			drawNoteBase(renderer, noteData, l, r, y);
			if (noteData.friction)
				drawTraceDiamond(renderer, noteData, l, r, y);
			if (noteData.isFlick()) 
				drawFlickArrow(renderer, noteData, y, current_tm);
		}
	}

	void ScorePreviewWindow::drawLines(const ScoreContext& context, Renderer* renderer)
	{
		if (!config.pvSimultaneousLine)
			return;

		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;
		const std::vector<double> scaledTimes = currentScaledTimes(context);
		const auto& drawData = context.scorePreviewDrawData.drawingLines;

		const Texture& texture = getNoteTexture();
		size_t sprIndex = SPR_SIMULTANEOUS_CONNECTION;
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& sprite = texture.sprites[sprIndex];
		size_t transIndex = static_cast<size_t>(SpriteType::SimultaneousLine);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& lineTransform = ResourceManager::spriteTransforms[transIndex];
		const float noteTop = 1. + Engine::getNoteHeight(), noteBottom = 1. - Engine::getNoteHeight();

		for (auto& line : drawData)
		{
			const double scaled_tm = scaledTimeOf(scaledTimes, line.layer);
			if (scaled_tm < line.visualTime.min || scaled_tm > line.visualTime.max)
				continue;
			if (stagesOn)
			{
				auto it = lineStages.find(line.tick);
				useStageById(it == lineStages.end() ? 0 : it->second);
				if (!stageNow)
					continue;
			}
			float noteLeft = line.xPos.min, noteRight = line.xPos.max;
			if (config.pvMirrorScore)
				std::swap(noteLeft *= -1, noteRight *= -1);
			float y = Engine::approach(line.visualTime.min, line.visualTime.max, scaled_tm);
			auto vPos = lineTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteRight, noteTop, noteBottom));
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);
			emitQuad(renderer, vPos, model, texture, sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(), defaultTint, Engine::getZIndex(SpriteLayer::UNDER_NOTE_EFFECT, 0, y));
		}
	}

	void ScorePreviewWindow::drawHoldTicks(const ScoreContext &context, Renderer *renderer)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;
		const std::vector<double> scaledTimes = currentScaledTimes(context);
		const float notesHeight = Engine::getNoteHeight() * 1.3f;
		const float w = notesHeight / scaledAspectRatio;
		const float noteTop = 1. + notesHeight, noteBottom = 1. - notesHeight;
		const Texture& texture = getNoteTexture();

		for (auto& tick : context.scorePreviewDrawData.drawingHoldTicks)
		{
			const double scaled_tm = scaledTimeOf(scaledTimes, tick.layer);
			if (scaled_tm < tick.visualTime.min || scaled_tm > tick.visualTime.max)
				continue;
			const Note& tickNote = context.score.notes.at(tick.refID);
			if (stagesOn)
			{
				useStageOf(context, tickNote);
				if (!stageNow)
					continue;
			}
			int sprIndex = getNoteSpriteIndex(tickNote);
			if (!isArrayIndexInBounds(sprIndex, texture.sprites))
				continue;
			const Sprite& sprite = texture.sprites[sprIndex];
			size_t transIndex = static_cast<size_t>(SpriteType::HoldTick);
			if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
				return;
			const SpriteTransform& transform = ResourceManager::spriteTransforms[transIndex];
			float y = Engine::approach(tick.visualTime.min, tick.visualTime.max, scaled_tm);
			const float tickCenter = tick.center * (config.pvMirrorScore ? -1 : 1);
			
			auto vPos = transform.apply(Engine::quadvPos(
				tickCenter - w, tickCenter + w,
				noteTop, noteBottom
			));
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);
			int zIndex = Engine::getZIndex(SpriteLayer::DIAMOND, tickCenter, y);
			emitQuad(renderer, vPos, model, texture, sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(), defaultTint, zIndex);
		}
	}

	void ScorePreviewWindow::drawHoldCurves(const ScoreContext& context, Renderer* renderer)
	{
		const float total_tm = accumulateDuration(context.scorePreviewDrawData.maxTicks, TICKS_PER_BEAT, context.score.tempoChanges);
		const double current_tm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const std::vector<double> scaledTimes = currentScaledTimes(context);
		const float mirror = config.pvMirrorScore ? -1 : 1;
		const auto& drawData = context.scorePreviewDrawData;
		static const int guideColorsTexture = ResourceManager::getTexture("guide_colors");

		for (auto& segment : drawData.drawingHoldSegments)
		{
			const double current_stm = scaledTimeOf(scaledTimes, segment.layer);
			const double visible_stm = current_stm + segment.visibleDuration;
			if ((std::min(segment.headTime, segment.tailTime) > visible_stm && segment.startTime > current_tm) || current_tm >= segment.endTime)
				continue;

			const Note& holdEnd = context.score.notes.at(segment.endID);
			const Note& holdStart = context.score.notes.at(holdEnd.parentID);
			if (stagesOn)
			{
				useStageOf(context, holdStart);
				if (!stageNow)
					continue;
			}
			float holdStartCenter = Engine::getNoteCenter(holdStart) * mirror;
			bool isHoldActivated = current_tm >= segment.activeTime;
			bool isSegmentActivated = current_tm >= segment.startTime;

			int textureID = noteSkins.getItemIndex(segment.isGuide ? NoteSkinItem::TouchLine : NoteSkinItem::LongNote);
			int sprIndex = (!holdStart.critical ? 1 : 3);
			bool colouredGuide = false;
			if (segment.isGuide)
			{
				sprIndex = segment.guideColor == GuideColor::Yellow ? 3 : 1;
				if (isExtendedGuideColor(segment.guideColor) && guideColorsTexture != -1)
				{
					textureID = guideColorsTexture;
					sprIndex = static_cast<int>(segment.guideColor);
					colouredGuide = true;
				}
			}
			if (textureID == -1)
				continue;
			const Texture& texture = ResourceManager::textures[textureID];
			if (!isArrayIndexInBounds(sprIndex, texture.sprites))
				continue;
			const Sprite& segmentSprite = texture.sprites[sprIndex];

			double segmentHead_stm = std::min(segment.headTime, segment.tailTime);
			double segmentTail_stm = std::max(segment.headTime, segment.tailTime);
			// Clamp the segment to be within the visible stage
			double segmentStart_stm = std::max(segmentHead_stm, current_stm);
			double segmentEnd_stm = std::min(segmentTail_stm, visible_stm);
			double segmentStartProgress, segmentEndProgress, holdStartProgress, holdEndProgress;

			if (!isSegmentActivated)
			{
				segmentStartProgress = 0;
				segmentEndProgress = unlerpD(segmentHead_stm, segmentTail_stm, segmentEnd_stm);
			}
			else
			{
				segmentStartProgress = unlerpD(segment.startTime, segment.endTime, current_tm);
				segmentEndProgress = lerpD(segmentStartProgress, 1.0, unlerpD(current_stm, segmentTail_stm, segmentEnd_stm));
			}

			const int steps = (segment.ease == EaseType::Linear ? 10 : 15)
				+ static_cast<int>(std::log(std::max((segmentEnd_stm - segmentStart_stm) / segment.visibleDuration, 4.5399e-5)) + 0.5); // Reduce steps if the segment is relatively small
			const auto ease = getEaseFunction(segment.ease);
			float startLeft = segment.headLeft;
			float startRight = segment.headRight;
			float endLeft = segment.tailLeft;
			float endRight = segment.tailRight;

			if (isSegmentActivated && context.score.holdNotes.at(holdStart.ID).startType == HoldNoteType::Normal)
			{
				float l = ease(startLeft, endLeft, segmentStartProgress), r = ease(startRight, endRight, segmentStartProgress);
				drawNoteBase(renderer, holdStart, l, r, 1, segment.activeTime / total_tm);
				if (holdStart.friction)
					drawTraceDiamond(renderer, holdStart, l, r, 1);
			}

			if (config.pvMirrorScore)
			{
				std::swap(startLeft *= -1, startRight *= -1);
				std::swap(endLeft *= -1, endRight *= -1);
			}

			if (segment.isGuide)
			{
				const HoldNote& hold = context.score.holdNotes.at(holdStart.ID);
				// We'll assume guide don't have HoldStepType::Skip
				double totalJoints = 1 + hold.steps.size();
				double headProgress = segment.tailStepIndex / totalJoints;
				double tailProgress = (segment.tailStepIndex + 1) / totalJoints;

				if (!isSegmentActivated)
				{
					holdStartProgress = headProgress;
					holdEndProgress = lerpD(headProgress, tailProgress, unlerpD(segmentHead_stm, segmentTail_stm, segmentEnd_stm));
				}
				else
				{
					holdStartProgress = lerpD(headProgress, tailProgress, unlerp(segment.startTime, segment.endTime, current_tm));
					holdEndProgress = lerpD(holdStartProgress, tailProgress, unlerpD(current_stm, segment.tailTime, segmentEnd_stm));
				}

				// the touch-line texture fades out along its height: pick which way we walk it
				if (segment.fadeType == FadeType::In)
				{
					holdStartProgress = 1.0 - holdStartProgress;
					holdEndProgress = 1.0 - holdEndProgress;
				}
				else if (segment.fadeType == FadeType::None)
				{
					holdStartProgress = holdEndProgress = 0.0;
				}
			}

			double from_percentage = 0;
			double stepStart_stm = segmentStart_stm;
			double stepTop = Engine::approach(stepStart_stm - segment.visibleDuration, stepStart_stm, current_stm);
			double stepStartProgress = segmentStartProgress;

			auto model = DirectX::XMMatrixIdentity();
			float alpha = segment.isGuide ? config.pvGuideAlpha : config.pvHoldAlpha;
			int zIndex = Engine::getZIndex(segment.isGuide ? SpriteLayer::GUIDE_PATH : SpriteLayer::HOLD_PATH, holdStartCenter, segment.activeTime / total_tm);

			for (int i = 0; i < steps; i++)
			{
				double to_percentage = double(i + 1) / steps;
				double stepEnd_stm = lerpD(segmentStart_stm, segmentEnd_stm, to_percentage);
				double stepBottom = Engine::approach(stepEnd_stm - segment.visibleDuration, stepEnd_stm, current_stm);
				double stepEndProgress = lerpD(segmentStartProgress, segmentEndProgress, to_percentage);

				float stepStartLeft = ease(startLeft, endLeft, stepStartProgress);
				float   stepEndLeft = ease(startLeft, endLeft, stepEndProgress);
				float stepStartRight = ease(startRight, endRight, stepStartProgress);
				float   stepEndRight = ease(startRight, endRight, stepEndProgress);

				auto vPos = Engine::perspectiveQuadvPos(stepStartLeft, stepEndLeft, stepStartRight, stepEndRight, stepTop, stepBottom);

				float spr_x1, spr_x2, spr_y1, spr_y2;
				if (segment.isGuide)
				{
					spr_x1 = segmentSprite.getX1() + GUIDE_XCUTOFF;
					spr_x2 = segmentSprite.getX2() - GUIDE_XCUTOFF;
					spr_y1 = lerp(segmentSprite.getY2() - GUIDE_Y_BOTTOM_CUTOFF, segmentSprite.getY1() + GUIDE_Y_TOP_CUTOFF, lerpD(holdStartProgress, holdEndProgress, from_percentage));
					spr_y2 = lerp(segmentSprite.getY2() - GUIDE_Y_BOTTOM_CUTOFF, segmentSprite.getY1() + GUIDE_Y_TOP_CUTOFF, lerpD(holdStartProgress, holdEndProgress, to_percentage));
				}
				else
				{
					spr_x1 = segmentSprite.getX1() + HOLD_XCUTOFF;
					spr_x2 = segmentSprite.getX2() - HOLD_XCUTOFF;
					spr_y1 = segmentSprite.getY1();
					spr_y2 = segmentSprite.getY2();
				}

				if (config.pvHoldAnimation && isHoldActivated && !colouredGuide && isArrayIndexInBounds(sprIndex - 1, texture.sprites))
				{
					const Sprite& activeSprite = texture.sprites[sprIndex - 1];
					const int norm2ActiveOffset = activeSprite.getY1() - segmentSprite.getY1();
					double delta_tm = current_tm - segment.activeTime;
					float normalAplha = (std::cos(delta_tm * NUM_PI * 2) + 2) / 3.;

					emitQuad(renderer, vPos, model, texture, spr_x1, spr_x2, spr_y1, spr_y2, defaultTint.scaleAlpha(alpha * normalAplha), zIndex);
					emitQuad(renderer, vPos, model, texture, spr_x1, spr_x2, spr_y1 + norm2ActiveOffset, spr_y2 + norm2ActiveOffset, defaultTint.scaleAlpha(alpha * (1.f - normalAplha)), zIndex);
				}
				else
					emitQuad(renderer, vPos, model, texture, spr_x1, spr_x2, spr_y1, spr_y2, defaultTint.scaleAlpha(alpha), zIndex);

				from_percentage = to_percentage;
				stepStart_stm = stepEnd_stm;
				stepTop = stepBottom;
				stepStartProgress = stepEndProgress;
			}
		}
	}

	void ScorePreviewWindow::drawNoteBase(Renderer* renderer, const Note& note, float noteLeft, float noteRight, float y, float zScalar)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;

		const Texture& texture = getNoteTexture();
		const int sprIndex = getNoteSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& sprite = texture.sprites[sprIndex];

		size_t transIndex = static_cast<size_t>(SpriteType::NoteMiddle);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& mTransform = ResourceManager::spriteTransforms[transIndex];

		transIndex = static_cast<size_t>(SpriteType::NoteLeft);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& lTransform = ResourceManager::spriteTransforms[transIndex];

		transIndex = static_cast<size_t>(SpriteType::NoteRight);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& rTransform = ResourceManager::spriteTransforms[transIndex];

		const float noteHeight = Engine::getNoteHeight();
		const float noteTop = 1. - noteHeight, noteBottom = 1. + noteHeight;
		if (config.pvMirrorScore)
			std::swap(noteLeft *= -1, noteRight *= -1);
		int zIndex = Engine::getZIndex(
			!note.friction ? SpriteLayer::BASE_NOTE : SpriteLayer::TICK_NOTE,
			noteLeft + (noteRight - noteLeft) / 2.f, y * zScalar
		);

		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		std::array<DirectX::XMFLOAT4, 4> vPos;
		// Middle
		vPos = mTransform.apply(Engine::perspectiveQuadvPos(noteLeft + 0.25f, noteRight - 0.3f, noteTop, noteBottom));
		emitQuad(renderer, vPos, model, texture,
			sprite.getX1() + NOTE_SIDE_WIDTH, sprite.getX2() - NOTE_SIDE_WIDTH, sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);

		// Left slice
		vPos = lTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteLeft + 0.25f, noteTop, noteBottom));
		emitQuad(renderer, vPos, model, texture,
			sprite.getX1() + NOTE_SIDE_PAD, sprite.getX1() + NOTE_SIDE_WIDTH, sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);
		
		// Right slice
		vPos = rTransform.apply(Engine::perspectiveQuadvPos(noteRight - 0.3, noteRight, noteTop, noteBottom));
		emitQuad(renderer, vPos, model, texture,
			sprite.getX2() - NOTE_SIDE_WIDTH, sprite.getX2() - NOTE_SIDE_PAD, sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);
	}

	void ScorePreviewWindow::drawTraceDiamond(Renderer *renderer, const Note &note, float noteLeft, float noteRight, float y)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;
			
		const Texture& texture = getNoteTexture();
		int frictionSprIndex = getFrictionSpriteIndex(note);
		if (!isArrayIndexInBounds(frictionSprIndex, texture.sprites))
			return;
		const Sprite& frictionSpr = texture.sprites[frictionSprIndex];
		size_t transIndex = static_cast<size_t>(SpriteType::TraceDiamond);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[transIndex];

		const float w = Engine::getNoteHeight() / scaledAspectRatio;
		const float noteTop = 1. + Engine::getNoteHeight(), noteBottom = 1. - Engine::getNoteHeight();
		if (config.pvMirrorScore)
			std::swap(noteLeft *= -1, noteRight *= -1);
		const float noteCenter = noteLeft + (noteRight - noteLeft) / 2;
		int zIndex = Engine::getZIndex(SpriteLayer::DIAMOND, noteCenter, y);

		auto vPos = transform.apply(Engine::quadvPos(
			noteCenter - w,
			noteCenter + w,
			noteTop, noteBottom)
		);
		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		
		emitQuad(renderer, vPos, model, texture, frictionSpr.getX1(), frictionSpr.getX2(), frictionSpr.getY1(), frictionSpr.getY2(), defaultTint, zIndex);
	}

	void ScorePreviewWindow::drawFlickArrow(Renderer *renderer, const Note &note, float y, double time)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;

		const Texture& texture = getNoteTexture();
		const int sprIndex = getFlickArrowSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& arrowSprite = texture.sprites[sprIndex];
		size_t flickTransformIdx = std::clamp(note.width, 1, MAX_FLICK_SPRITES) - 1 + static_cast<int>((note.flick == FlickType::Left || note.flick == FlickType::Right) ? SpriteType::FlickArrowLeft : SpriteType::FlickArrowUp);
		if (!isArrayIndexInBounds(flickTransformIdx, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[flickTransformIdx];

		const int mirror = config.pvMirrorScore ? -1 : 1;
		const int flickDirection = mirror * (note.flick == FlickType::Left ? -1 : (note.flick == FlickType::Right ? 1 : 0));
		const float center = Engine::getNoteCenter(note) * mirror;
		const float w = std::clamp(note.width, 0, MAX_FLICK_SPRITES) * (note.flick == FlickType::Right ? -1 : 1) * mirror / 4.f;
		
		auto vPos = transform.apply(
			Engine::quadvPos(
				center - w,
				center + w,
				1, 1 - 2 * std::abs(w) * scaledAspectRatio
			)
		);
		int zIndex = Engine::getZIndex(SpriteLayer::FLICK_ARROW, center, y);
		
		if (config.pvFlickAnimation)
		{
			double t = std::fmod(time, 0.5) / 0.5;
			const auto cubicEaseIn = [](double t) { return t * t * t; };
			auto animationVector = DirectX::XMVectorScale(DirectX::XMVectorSet(flickDirection, -2 * scaledAspectRatio, 0.f, 0.f), t);
			auto model = DirectX::XMMatrixTranslationFromVector(animationVector) * DirectX::XMMatrixScaling(y, y, 1.f);
			
			emitQuad(renderer, vPos, model, texture,
				arrowSprite.getX1(), arrowSprite.getX2(), arrowSprite.getY1(), arrowSprite.getY2(),
				defaultTint.scaleAlpha(1 - cubicEaseIn(t)), zIndex
			);
		}
		else
		{
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);

			emitQuad(renderer, vPos, model, texture,
				arrowSprite.getX1(), arrowSprite.getX2(), arrowSprite.getY1(), arrowSprite.getY2(),
				defaultTint, zIndex
			);
		}
	}

	void ScorePreviewWindow::updateToolbar(ScoreEditorTimeline &timeline, ScoreContext &context) const
	{
		static float lastHoveredTime = -1;
		constexpr float MAX_NO_HOVER_TIME = 1.5f;
		static float toolBarWidth = UI::btnNormal.x * 2;
		if (!config.pvDrawToolbar)
			return;
		ImGuiIO io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 previewSize = ImGui::GetContentRegionAvail() - ImVec2{ this->getScrollbarWidth(), 0.0f };
		const float previewUiScale = std::max(0.0f, Canvas::Preview::hudScale(previewSize.x, previewSize.y));
		const float toolbarHudAvoidanceY = config.pvDrawLifeHud ? (HUD_LIFE_Y + HUD_LIFE_HEIGHT + PREVIEW_TOOLBAR_HUD_PADDING) * previewUiScale : 0.0f;
		ImGui::SetNextWindowPos(ImGui::GetWindowPos() + ImVec2{
			ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 4 - toolBarWidth,
			style.WindowPadding.y * 5 + toolbarHudAvoidanceY
		});
		ImGui::SetNextWindowSizeConstraints({48, 0}, {120, FLT_MAX}, NULL);
		float childBgAlpha = std::clamp(easeInCubic(unlerp(MAX_NO_HOVER_TIME, 0, lastHoveredTime)), 0.25f, 1.f);
		// the controls themselves fade too (not just their background): over the combo they are a
		// faint outline until the mouse comes near the preview
		const bool previewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		const float controlsAlpha = std::max(childBgAlpha, previewHovered ? 0.55f : 0.12f);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, controlsAlpha);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.f);
		
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg, childBgAlpha));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		
		ImGui::Begin("###preview_toolbar", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_ChildWindow);
		toolBarWidth = ImGui::GetWindowWidth();
		float centeredXBtn = toolBarWidth / 2 - UI::btnNormal.x / 2;
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
			lastHoveredTime = 0;
		else
			lastHoveredTime = std::min(io.DeltaTime + lastHoveredTime, MAX_NO_HOVER_TIME);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOUBLE_UP, UI::btnNormal, true, context.currentTick < context.scorePreviewDrawData.maxTicks + TICKS_PER_BEAT))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick = timeline.nextGridTick(context.score, context.currentTick);
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_UP, UI::btnNormal, true, context.currentTick < context.scorePreviewDrawData.maxTicks + TICKS_PER_BEAT))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick++;
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_STOP, UI::btnNormal, false))
			timeline.stop(context);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(timeline.isPlaying() ? ICON_FA_PAUSE : ICON_FA_PLAY, UI::btnNormal))
			timeline.setPlaying(context, !timeline.isPlaying());

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOWN, UI::btnNormal, true, context.currentTick > 0))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick--;
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOUBLE_DOWN, UI::btnNormal, true, context.currentTick > 0))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick = timeline.previousGridTick(context.score, context.currentTick);
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(isFullWindow() ? ICON_FA_COMPRESS : ICON_FA_EXPAND))
			fullWindow = !isFullWindow();

		// 2DMV: lit while one plays, with a warning colour when one was found but cannot play
		ImGui::SetCursorPosX(centeredXBtn);
		const bool moviePlays = config.pvDrawMovie && !movieStatus.playing.empty();
		const bool movieBroken = config.pvDrawMovie && !movieStatus.error.empty();
		if (moviePlays || movieBroken)
			ImGui::PushStyleColor(ImGuiCol_Text, movieBroken ? ImVec4(1.0f, 0.65f, 0.25f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
		if (UI::transparentButton(ICON_FA_FILM, UI::btnNormal, false))
			ImGui::OpenPopup("###preview_2dmv");
		if (moviePlays || movieBroken)
			ImGui::PopStyleColor();
		UI::tooltip(getString("preview_movie_button"));
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		if (ImGui::BeginPopup("###preview_2dmv"))
		{
			movieMenu();
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_MINUS, UI::btnNormal, false, timeline.getPlaybackSpeed() > 0.25f))
			timeline.setPlaybackSpeed(context, timeline.getPlaybackSpeed() - 0.25f);

		const float playbackStrWidth = ImGui::CalcTextSize("0000%").x;
		ImGui::SetCursorPosX(toolBarWidth / 2 - playbackStrWidth / 2);
		UI::transparentButton(IO::formatString("%.0f%%", timeline.getPlaybackSpeed() * 100).c_str(), ImVec2{playbackStrWidth, UI::btnNormal.y }, false, false);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_PLUS, UI::btnNormal, false, timeline.getPlaybackSpeed() < 1.0f))
			timeline.setPlaybackSpeed(context, timeline.getPlaybackSpeed() + 0.25f);

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		float currentTm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const std::vector<HiSpeedChange> layerHiSpeeds = hiSpeedsOfLayer(context.score, context.currentLayer);
		double currentScaledTm = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, layerHiSpeeds);
		int currentMeasure = accumulateMeasures(context.currentTick, TICKS_PER_BEAT, context.score.timeSignatures);
		const TimeSignature& ts = context.score.timeSignatures[findTimeSignature(currentMeasure, context.score.timeSignatures)];
		const Tempo& tempo = getTempoAt(context.currentTick, context.score.tempoChanges);
		int hiSpeedIdx = findHighSpeedChange(context.currentTick, layerHiSpeeds);
		float speed = (hiSpeedIdx == -1 ? 1.0f : layerHiSpeeds[hiSpeedIdx].speed);
		int seVolumeIdx = findSEVolumeChange(context.currentTick, context.score.seVolumeChanges);
		float seVolume = (seVolumeIdx == -1 ? 1.0f : context.score.seVolumeChanges[seVolumeIdx].volume);

		char rhythmString[256];
		snprintf(rhythmString, sizeof(rhythmString), "%02d:%02d:%02d|%.2fs|%d/%d|%g BPM|%sx|SE %s",
			static_cast<int>(currentTm / 60), static_cast<int>(std::fmod(currentTm, 60.f)), static_cast<int>(std::fmod(currentTm * 100, 100.f)),
			currentScaledTm,
			ts.numerator, ts.denominator,
			tempo.bpm,
			IO::formatFixedFloatTrimmed(speed).c_str(),
			IO::formatFixedFloatTrimmed(seVolume).c_str()
		);
		char* str = strtok(rhythmString, "|");
		ImGui::SetCursorPosX(toolBarWidth / 2 - ImGui::CalcTextSize(str).x / 2);
		ImGui::Text(str);
		for (auto&& col : {feverColor, timeColor, tempoColor, speedColor, seVolumeColor})
		{
			str = strtok(NULL, "|");
			ImGui::SetCursorPosX(toolBarWidth / 2 - ImGui::CalcTextSize(str).x / 2);
			ImGui::TextColored(ImColor(col), str);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	void ScorePreviewWindow::movieMenu()
	{
		const MovieStatus& status = movieStatus;
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
		ImGui::TextUnformatted(ICON_FA_FILM " 2DMV");
		ImGui::Separator();
		ImGui::Checkbox(getString("preview_draw_movie"), &config.pvDrawMovie);

		const std::string file = !config.pvMovieFile.empty() ? config.pvMovieFile : status.found;
		if (file.empty())
			ImGui::TextDisabled("%s", getString("preview_movie_none"));
		else
		{
			ImGui::TextUnformatted(IO::File::getFilename(file).c_str());
			if (config.pvMovieFile.empty())
				ImGui::TextDisabled("%s", getString("preview_movie_next_to_music"));
		}
		if (!status.error.empty())
			ImGui::TextColored({ 1.0f, 0.65f, 0.25f, 1.0f }, getString("preview_movie_error"), status.error.c_str());
		else if (!status.playing.empty() && config.pvDrawMovie)
			ImGui::TextDisabled(getString("preview_movie_playing"), status.width, status.height, status.decoder);

		if (ImGui::Button(IO::concat(ICON_FA_FOLDER_OPEN, getString("preview_movie_choose"), " ").c_str()))
		{
			IO::FileDialog fileDialog{};
			fileDialog.title = "Open 2DMV";
			fileDialog.filters = { IO::videoFilter, IO::allFilter };
			fileDialog.parentWindowHandle = Application::windowState.windowHandle;
			if (fileDialog.openFile() == IO::FileDialogResult::OK)
			{
				config.pvMovieFile = fileDialog.outputFilename;
				config.pvDrawMovie = true;
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(config.pvMovieFile.empty());
		if (ImGui::Button(getString("preview_movie_auto_button")))
			config.pvMovieFile.clear();
		ImGui::EndDisabled();
		UI::tooltip(getString("preview_movie_auto_tooltip"));

		ImGui::Separator();
		const float width = ImGui::GetFontSize() * 10.0f;
		ImGui::SetNextItemWidth(width);
		float brightness = config.pvBackgroundBrightness * 100.0f;
		if (ImGui::SliderFloat(getString("background_brightnes"), &brightness, 0.0f, 100.0f, "%.0f%%"))
			config.pvBackgroundBrightness = std::clamp(brightness / 100.0f, 0.0f, 1.0f);
		ImGui::SetNextItemWidth(width);
		float lanes = config.pvStageOpacity * 100.0f;
		if (ImGui::SliderFloat(getString("preview_lane_opacity"), &lanes, 0.0f, 100.0f, "%.0f%%"))
			config.pvStageOpacity = std::clamp(lanes / 100.0f, 0.0f, 1.0f);
		ImGui::SetNextItemWidth(width);
		ImGui::DragFloat(getString("preview_movie_offset"), &config.pvMovieOffset, 1.0f, -10000.0f, 10000.0f, "%.0f");
		ImGui::TextDisabled("%s", getString("preview_movie_drop_hint"));
		ImGui::PopTextWrapPos();
	}

	float ScorePreviewWindow::getScrollbarWidth() const
	{
		ImGuiStyle& style = ImGui::GetStyle();
		return style.ScrollbarSize + 4;
	}

    void ScorePreviewWindow::updateScrollbar(ScoreEditorTimeline &timeline, ScoreContext &context) const
	{
		constexpr float scrollpadY = 30.f;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 contentSize = ImGui::GetWindowContentRegionMax();
		ImVec2 cursorBegPos = ImGui::GetCursorStartPos();
		ImVec2 scrollbarSize = { getScrollbarWidth(), contentSize.y - cursorBegPos.y };
		
		ImGui::SetCursorPos(cursorBegPos + ImVec2{ contentSize.x - scrollbarSize.x - style.WindowPadding.x / 2, 0 });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarBg));
		ImGui::BeginChild("###scrollbar", scrollbarSize, false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleColor();

		ImVec2 scrollContentSize = ImGui::GetContentRegionAvail();
		ImVec2 scrollMaxSize = ImGui::GetWindowContentRegionMax();
		int maxTicks = std::max(context.scorePreviewDrawData.maxTicks, 1);
		float scrollRatio = std::min(Engine::getNoteDuration(config.pvNoteSpeed) / accumulateDuration(context.scorePreviewDrawData.maxTicks, TICKS_PER_BEAT, context.score.tempoChanges), 1.f);
		float progress = 1.f - std::min(float(context.currentTick) / maxTicks, 1.f);
		float handleHeight = std::max(20.f, scrollContentSize.y * scrollRatio);
		
		bool scrollbarActive = false;
		ImGui::BeginDisabled(timeline.isPlaying());
		ImGui::SetCursorPos(ImGui::GetCursorStartPos());
		ImGui::InvisibleButton("##scroll_bg", contentSize, ImGuiButtonFlags_NoNavFocus);
		scrollbarActive |= ImGui::IsItemActive();

		ImVec2 handleSize = {style.ScrollbarSize, handleHeight};
		ImVec2 handlePos = {scrollMaxSize.x / 2 - handleSize.x / 2, lerp(0, scrollMaxSize.y - handleHeight, progress)};
		ImVec2 absHandlePos = ImGui::GetWindowPos() + handlePos;

		ImGui::SetCursorPos(handlePos);
		ImGui::InvisibleButton("##scroll_handle", handleSize);
		scrollbarActive |= ImGui::IsItemActive();

		ImGuiCol_ handleColBase = scrollbarActive ? ImGuiCol_ScrollbarGrabActive : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
		
		ImGui::RenderFrame(absHandlePos, absHandlePos + ImGui::GetItemRectSize(), ImGui::GetColorU32(handleColBase), true, 3.f);
		ImGui::EndDisabled();

		if (scrollbarActive)
		{
			float absScrollStart = ImGui::GetWindowPos().y + handleSize.y / 2;
			float absScrollEnd = ImGui::GetWindowPos().y + scrollMaxSize.y - handleSize.y / 2;
			float mouseProgress = 1.f - std::clamp(unlerp(absScrollStart, absScrollEnd, io.MousePos.y), 0.f, 1.f);
			context.currentTick = std::round(lerp(0, maxTicks, mouseProgress));	
		}
		
		ImGui::EndChild();
	}
}
