#pragma once
#include "ScoreContext.h"
#include "ScoreEditorTimeline.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/Renderer.h"
#include "Rendering/Camera.h"
#include "Canvas/Video/MoviePlayer.h"
#include "Canvas/Preview/LiveOverlay.h"
#include "Canvas/Core/DynamicStages.h"
#include <set>
#include <unordered_map>

struct ImDrawList;
struct ImVec2;

// Dynamic Stages preview switches (the stage window's eye / solo buttons set these; the preview
// reads them every frame). A hidden stage and its notes are not drawn; soloStage >= 0 draws only that one.
namespace Canvas::Stages
{
	inline std::set<int> hiddenStages;
	inline int soloStage = -1;
}

namespace MikuMikuWorld
{
	class ScorePreviewBackground
	{
		std::string backgroundFile;
		std::string jacketFile;
		Framebuffer frameBuffer;
		float brightness;
		bool init;
		
		struct DefaultJacket
		{
			static std::array<DirectX::XMFLOAT4, 4> getLeftUV();
			static std::array<DirectX::XMFLOAT4, 4> getRightUV();
			static std::array<DirectX::XMFLOAT4, 4> getLeftMirrorUV();
			static std::array<DirectX::XMFLOAT4, 4> getRightMirrorUV();
			static std::array<DirectX::XMFLOAT4, 4> getCenterUV();
			static std::array<DirectX::XMFLOAT4, 4> getMirrorCenterUV();
		};
		void updateDrawDefaultJacket(Renderer* renderer, const Jacket& jacket);
		public:
		ScorePreviewBackground();
		~ScorePreviewBackground();

		void setBrightness(float value);
		void update(Renderer* renderer, const Jacket& jacket);
		bool shouldUpdate(const Jacket& jacket) const;
		void draw(Renderer* renderer, float scrWidth, float scrHeight) const;		
	};

	// what the Score Preview's 2DMV menu and the settings show about the 2DMV
	struct PreviewMovieStatus
	{
		std::string found, error;	// the movie next to the music; why it does not play
		std::string playing;		// the movie on screen
		const char* decoder{ "" };
		int width{}, height{};
	};

	class ScorePreviewWindow
	{
		Framebuffer previewBuffer;
		ScorePreviewBackground background;
		Canvas::Video::MoviePlayer movie;
		bool movieCovers{ false };
		int lastPixelWidth{ 0 };	// renderFrame's target width in pixels	// the last frame had a movie picture over the whole preview (no background needed)
		// while paused the picture only changes with the tick, the chart or a preview setting: the
		// last one is reused until one of those moves (and at least 4 times a second, for late MV frames)
		size_t lastFrameKey{ 0 };
		double lastFrameAt{ -1.0 };
		// Preview quality "Auto": the share of the window's pixels the 3D preview is drawn at, stepped
		// down while playback runs slower than ~55 fps and back up when there is room
		float autoScale{ 1.0f };
		double autoWindowStart{ -1.0 };
		int autoFrames{ 0 };
		int autoSmoothSeconds{ 0 };	// seconds in a row at full frame rate (then quality goes back up a step)
		float renderScale() const;

		// ---- Dynamic Stages (only used while Score::stages is enabled and has data) ----
		struct StageFrame
		{
			int id{};
			Canvas::Stages::StageState state;
			Canvas::Stages::StageProjection proj;
			float shift{};				// judge line offset in depth units
			float maskLo{}, maskHi{};	// lit lanes (already mirrored when the preview is)
		};
		bool stagesOn{ false };
		std::vector<StageFrame> stageFrames;	// visible stages, back to front
		const StageFrame* stageNow{ nullptr };	// stage the quads being drawn belong to (null = skip them)
		std::unordered_map<int, int> lineStages;	// tick -> stage of the notes on it (simultaneous lines)
		bool hudStageValid{ false };
		Canvas::Stages::StageProjection hudProjection;	// stage 0 + camera, for the live overlay's lane queries
		void prepareStages(const ScoreContext& context);
		void useStageById(int stageId);
		void useStageOf(const ScoreContext& context, const Note& note);
		void drawStagePlanes(Renderer* renderer);
		// renderer->drawQuad for a normal chart; with stages every corner goes through the stage's projection
		void emitQuad(Renderer* renderer, const std::array<DirectX::XMFLOAT4, 4>& vPos, const DirectX::XMMATRIX& model, const Texture& tex,
			float x1, float x2, float y1, float y2, const Color& tint, int z);
		std::string movieMusic, movieFound;	// the music file last searched for a 2DMV, and what was found
		void drawMovie(ScoreContext& context, Renderer* renderer, float width, float height);
		float scaledAspectRatio;
		Camera noteEffectsCamera;
		// the last frame's size in stage units / pixels (for the overlay's lane geometry and MV lines)
		float lastTargetWidth{}, lastTargetHeight{}, lastPixelHeight{};

		mutable bool fullWindow{};

		const Texture& getNoteTexture();

		void drawNoteBase(Renderer* renderer, const Note& note, float left, float right, float y, float zScalar = 1);
		void drawTraceDiamond(Renderer* renderer, const Note& note, float left, float right, float y);
		void drawFlickArrow(Renderer* renderer, const Note& note, float y, double cur_time);

		void drawStageCoverMask(Renderer* renderer);
		void drawHudOverlay(const ScoreContext& context, ImDrawList* drawList, ImVec2 position, ImVec2 size);
	public:
		// what the live overlay shows (the preview's settings, or the video export's)
		static Canvas::Preview::OverlayOptions overlayOptions(bool forVideo);
		// lane coordinates -> pixels of a frame drawn at position/size (valid after renderFrame)
		Canvas::Preview::Projector projector(ImVec2 position, ImVec2 size) const;
	private:

		void updateHudControls(ScoreEditorTimeline& timeline, ScoreContext& context) const;
		void updateToolbar(ScoreEditorTimeline& timeline, ScoreContext& context) const;
		// the toolbar's 2DMV menu: which movie, why it does not play, brightness and lane opacity
		static void movieMenu();
		float getScrollbarWidth() const;
		void updateScrollbar(ScoreEditorTimeline& timeline, ScoreContext& context) const;
	public:
		ScorePreviewWindow(); 
		~ScorePreviewWindow();
		void update(ScoreContext& context, Renderer* renderer);
		// Draws the preview at context.currentTick into `target` (resized to fit). Used by the
		// preview window and by video export. Returns false when the shaders are missing.
		bool renderFrame(ScoreContext& context, Renderer* renderer, int pixelWidth, int pixelHeight, Framebuffer& target);
		static DirectX::XMMATRIX animationMatrix(const Canvas::Animation::Transform& t, float width, float height);
		ScorePreviewBackground& getBackground() { return background; }
		// The 2DMV shown behind the stage (config.pvMovieFile, or a movie next to the music).
		const Canvas::Video::MoviePlayer& getMovie() const { return movie; }
		const std::string& getFoundMovie() const { return movieFound; }
		// what the settings window shows about the 2DMV
		using MovieStatus = PreviewMovieStatus;
		static inline MovieStatus movieStatus{};
		// Video export waits for every movie frame instead of showing the newest decoded one.
		bool movieWaitsForFrames{};
		void updateUI(ScoreEditorTimeline& timeline, ScoreContext& context);

		void drawNotes(const ScoreContext& context, Renderer* renderer);
		void drawLines(const ScoreContext& context, Renderer* renderer);
		void drawHoldTicks(const ScoreContext& context, Renderer* renderer);
		void drawHoldCurves(const ScoreContext& context, Renderer* renderer);
 		
		void drawStage(Renderer* renderer);
		void drawStageCover(Renderer* renderer);
		void drawStageCoverDecoration(Renderer* renderer);

		void setFullWindow(bool fullScreen);
		
		inline bool isFullWindow() const { return fullWindow; };

		void loadNoteEffects(Effect::EffectView& effectView);
	};
}
