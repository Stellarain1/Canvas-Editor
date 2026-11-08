#pragma once
#include "ScoreContext.h"
#include "ImGui/imgui_internal.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/Renderer.h"
#include "TimelineMode.h"
#include "Background.h"
#include "RenderDebugStats.h"
#include "Canvas/UI/ChartOverview.h"
#include <array>
#include <functional>

namespace MikuMikuWorld
{
	enum class StepDrawType
	{
		NormalStep,
		SkipStep,
		InvisibleHold,
		InvisibleHoldCritical,
		StepDrawTypeMax
	};

	constexpr std::array<ImU32, (int)StepDrawType::StepDrawTypeMax> stepDrawOutlineColors[] =
	{
		0xFFAAFFAA, 0xFFFFFFAA, 0xFFCCCCCC, 0xFFCCCCCC
	};

	constexpr std::array<ImU32, (int)StepDrawType::StepDrawTypeMax> stepDrawFillColors[] =
	{
		0x00FFFFFF, 0x00FFFFFF, 0xFF66B622, 0xFF15A0C9
	};

	class StepDrawData
	{
	public:
		int tick{};
		int lane{};
		int width{};
		StepDrawType type{};
		// overrides the type's fill colour when non-zero (coloured guide endpoints)
		ImU32 customFill{ 0 };

		StepDrawData() {}
		StepDrawData(int _tick, int _lane, int _width, StepDrawType _type, ImU32 _customFill = 0) :
			tick{ _tick }, lane{ _lane }, width{ _width }, type{ _type }, customFill{ _customFill }
		{
		}

		StepDrawData(const Note& n, StepDrawType _type) :
			tick{ n.tick }, lane{ n.lane }, width{ n.width }, type{ _type }
		{
		}

		inline constexpr ImU32 getFillColor() const { return customFill ? customFill : stepDrawFillColors->at((int)type); }
		inline constexpr ImU32 getOutlineColor() const { return stepDrawOutlineColors->at((int)type); }
	};

	struct EventControlDrawData
	{
		float timelineX;
		ImVec2 pos;
		ImVec2 size;
		ImVec2 txtSize;
		ImU32 color;
		std::string txt;
		bool highlight;
		bool selected;
		bool enabled;
	};

	// How a guide is drawn: colour and fade along its whole length
	struct GuideStyle
	{
		GuideColor color{ GuideColor::Green };
		FadeType fade{ FadeType::Out };
		int startTick{};
		int endTick{};
	};

	struct SpeedRatioLabelDrawData
	{
		ImVec2 pos;
		std::string text;
	};

	class ScoreEditorTimeline
	{
	private:
		int noteCutoffX		= 30;
		int noteSliceWidth	= 90;
		int noteOffsetX		= 5;
		int notesSliceSize	= 18;
		int holdCutoffX		= 33;
		int holdSliceWidth	= 10;
		int holdSliceSize	= 5;

		TimelineMode currentMode{ TimelineMode::Select };
		float laneOffset{};
		float maxOffset = 10000;
		float minOffset{};
		float offset{};
		float visualOffset{};
		float scrollStartY{};
		float zoom = 1.0f;

		static constexpr float unitHeight = 0.15f;
		static constexpr float scrollUnit = 50;
		static constexpr float minZoom = 0.25f;
		static constexpr float sliderMaxZoom = 30.0f;
		static constexpr float maxZoom = 100.0f;
		static constexpr double waveformSecondsPerPixel = 0.005;
		static constexpr float noteControlWidth = 12;
		static constexpr float eventControlPadding = 10;

		static constexpr float minPlaybackSpeed = 0.25f;
		static constexpr float maxPlaybackSpeed = 1.00f;

		float minNoteYDistance{};
		int hoverLane{};
		int hoverTick{};
		int hoveringNote{};
		int holdingNote{};
		int holdLane{};
		int holdTick{};
		int lastSelectedTick{};
		int division = 8;

		bool mouseInTimeline{ false };
		bool isHoveringNote{ false };
		bool isHoldingNote{ false };
		bool isMovingNote{ false };
		bool dragging{ false };
		bool insertingHold{ false };
		bool insertingFever{ false };

		float time{};
		float timeLastFrame{};
		float playStartTime{};
		float songPos{};
		float songPosLastFrame{};
		float playbackSpeed{ 1.0f };
		float stopTime{ -1.0f };
		bool playing{ false };

		std::unique_ptr<Framebuffer> slidePathFramebuffer;
		std::unique_ptr<Framebuffer> notesFramebuffer;
		ImVec2 size;
		ImVec2 position;
		ImVec2 prevPos;
		ImVec2 prevSize;
		ImRect boundaries;

		ImVec2 ctrlMousePos;
		ImVec2 dragStart;
		ImVec2 mousePos;

		Score prevUpdateScore;

		struct InputNotes
		{
			Note tap;
			Note holdStart;
			Note holdEnd;
			Note holdStep;
		} inputNotes{ Note(NoteType::Tap), Note(NoteType::Hold), Note(NoteType::HoldEnd), Note(NoteType::HoldMid) };

		Fever inputFever{ -1, -1 };

		struct NoteTransform
		{
			int tick{}, lane{}, width{};

			static NoteTransform fromNote(const Note& note)
			{
				return NoteTransform{ note.tick, note.lane, note.width };
			}

			bool isSame(const Note& note) const
			{
				return tick == note.tick && lane == note.lane && width == note.width;
			}

		} noteTransformOrigin;

		struct HiSpeedControlDrawData
		{
			int tick{};
			float minX{};
			float maxX{};
		};

		std::map<int, std::pair<float, float>> eventControlCursor;
		std::stack<EventControlDrawData> drawEvents;
		std::vector<StepDrawData> drawSteps;
		std::vector<SpeedRatioLabelDrawData> drawSpeedRatioLabels;
		std::vector<HiSpeedControlDrawData> hiSpeedControlRects;
		std::vector<HiSpeedControlDrawData> seVolumeControlRects;
		std::unordered_set<std::string> playingNoteSounds;
		std::array<char, 32> noteSpeedRatioInput{};
		std::unordered_set<int> selectedHiSpeedTicks;
		std::unordered_set<int> selectedSEVolumeTicks;
		static constexpr float audioLookAhead = 0.05f;

		Debug::DebugRenderStats renderStats;

		void updateScrollbar();
		void updateOverview(ScoreContext& context);
		float overviewReservedWidth() const;
		ChartOverview overview;
		void updateScrollingPosition();

		void drawWaveform(const ScoreContext& context);
		void drawFeverLine(const Fever& fever);

		void drawHoldCurve(const HoldNote& hold, const std::map<int, Note>& notes, Renderer* renderer, const Color& tint, const int offsetTick = 0, const int offsetLane = 0);
		void drawHoldCurvePart(const Note& n1, const Note& n2, EaseType ease, bool isGuide, Renderer* renderer, const Color& tint, const int offsetTick = 0, const int offsetLane = 0, const GuideStyle* guideStyle = nullptr);
		void drawHoldNote(const std::map<int, Note>& notes, const HoldNote& note, Renderer* renderer, const Color& tint, const int offsetTicks = 0, const int offsetLane = 0);
		void drawHoldMid(Note& note, HoldStepType type, Renderer* renderer, const Color& tint);
		void drawOutline(const StepDrawData& data);
		void drawStageTints(const ScoreContext& context);
		void drawQueuedSpeedRatioLabels();
		void drawFlickArrow(const Note& note, Renderer* renderer, const Color& tint, const int offsetTick = 0, const int offsetLane = 0);
		void drawSpeedRatioLabel(const Note& note, const int offsetTick = 0, const int offsetLane = 0);
		void drawNote(const Note& note, Renderer* renderer, const Color& tint, const int offsetTick = 0, const int offsetLane = 0);
		bool eventControl(int tick, ImU32 color, const char* txt, bool fromStart, bool enabled, bool selected = false);
		bool noteControl(ScoreContext& context, const ImVec2& pos, const ImVec2& sz, const char* id, ImGuiMouseCursor cursor);
		bool bpmControl(const Tempo& tempo);
		bool bpmControl(float bpm, int tick, bool enabled);
		bool timeSignatureControl(int numerator, int denominator, int tick, bool enabled);
		bool skillControl(const SkillTrigger& skill);
		bool skillControl(int tick, bool enabled);
		bool feverControl(const Fever& fever);
		bool feverControl(int tick, bool start, bool enabled);
		bool hiSpeedControl(ScoreContext& context, const HiSpeedChange& hiSpeed);
		bool hiSpeedControl(ScoreContext& context, int tick, float speed, bool enabled);
		bool seVolumeControl(ScoreContext& context, const SEVolumeChange& seVolume);
		bool seVolumeControl(ScoreContext& context, int tick, float volume, bool enabled);

		void drawInputNote(Renderer* renderer);
		void previewInput(EditArgs& edit, Renderer* renderer);
		void previewPaste(ScoreContext& context, Renderer* renderer);
		void executeInput(ScoreContext& context, EditArgs& edit);
		void eventEditor(ScoreContext& context);

		void insertNote(ScoreContext& context, EditArgs& edit);
		void insertHold(ScoreContext& context, EditArgs& edit);
		void insertHoldStep(ScoreContext& context, EditArgs& edit, int holdId);
		void insertEvent(ScoreContext& context, EditArgs& edit);

		void updateNoteSE(ScoreContext& context);
		void syncNoteSpeedRatioInput(const ScoreContext& context);
		bool applyNoteSpeedRatioInput(ScoreContext& context);

		void contextMenu(ScoreContext& context);
		std::vector<int> getSelectedHiSpeedIndices(const ScoreContext& context) const;
		std::vector<int> getSelectedSEVolumeIndices(const ScoreContext& context) const;

		int getStopTick(const Score& score) const;
		Color layerTint(const ScoreContext& context, int layer, const Color& base) const;
		float getStopTime(const ScoreContext& context);

	public:
		float laneWidth = 26;
		float notesHeight = 28;
		bool drawHoldStepOutlines = true;
		Background background;

		struct EventEditParams
		{
			EventType type = EventType::None;
			int editIndex = -1;
			int editTick = -1;

			float editBpm = 120.0f;
			int editTimeSignatureNumerator = 4;
			int editTimeSignatureDenominator = 4;
			std::string editHiSpeed = "1.00";
			std::string editSEVolume = "1.00";

			inline void setEditHispeed(float value) { editHiSpeed = IO::formatFixedFloatTrimmed(value); }
			inline void setEditSEVolume(float value) { editSEVolume = IO::formatFixedFloatTrimmed(value); }
		} eventEdit {};

		int snapTickFromPos(const Score& score, float posY) const;
		int positionToTick(float pos) const;
		float tickToPosition(int tick) const;
		float getNoteYPosFromTick(int tick) const;

		int laneFromCenterPosition(int lane, int width) const;
		int positionToLane(float pos) const;
		float laneToPosition(float lane) const;

		constexpr inline float getTimelineStartX() const { return position.x + laneOffset; }
		constexpr inline float getTimelineEndX() const { return getTimelineStartX() + (laneWidth * NUM_LANES); }

		constexpr inline float getZoom() const { return zoom; }
		void setZoom(float zoom);

		constexpr inline int getDivision() const { return division; }
		void setDivision(int div) { division = std::clamp(div, 4, 1920); }

		constexpr inline bool isMouseInTimeline() const { return mouseInTimeline; }
		bool isNoteVisible(const Note& note, int offsetTicks = 0) const;

		int findClosestHold(const ScoreContext& context, int lane, int tick);
		bool isMouseInHoldPath(const Note& n1, const Note& n2, EaseType ease, float x, float y);
		constexpr inline bool isPlaying() const { return playing; }
		void setPlaying(ScoreContext& context, bool state);
		void stop(ScoreContext& context);
		void calculateMaxOffsetFromScore(const Score& score);

		void update(ScoreContext& context, EditArgs& edit, Renderer* renderer);
		void updateNotes(ScoreContext& context, EditArgs& edit, Renderer* renderer);
		void updateNote(ScoreContext& context, EditArgs& edit, Note& note);
		void updateInputNotes(EditArgs& edit);
		void debug(ScoreContext& context);

		void previousTick(ScoreContext& context);
		void nextTick(ScoreContext& context);
		// The grid line after / before `tick` at the current division (restarts every measure)
		int nextGridTick(const Score& score, int tick) const;
		int previousGridTick(const Score& score, int tick) const;
		void focusCursor(int cursorTick, Direction direction);
		// Move the cursor to `tick` (pausing playback) and scroll so it sits mid-screen.
		void jumpToTick(ScoreContext& context, int tick);

		// Findings shown as marks in the overview strip (owned by the Issue List).
		const std::vector<Canvas::Issues::Issue>* overviewIssues{ nullptr };
		// Edit tools the right-click menu offers (set by the editor, which owns their dialogs)
		std::function<void()> onRepeatHoldMids;
		std::function<void()> onInterpolateHiSpeeds;

		constexpr inline TimelineMode getMode() const { return currentMode; }
		void changeMode(TimelineMode mode, EditArgs& edit);

		constexpr inline float getPlaybackSpeed() const { return playbackSpeed; }
		void setPlaybackSpeed(ScoreContext& context, float speed);

		float getWidth() const { return size.x; }
		float getHeight() const { return size.y; }

		void insertSkill(ScoreContext& context, int tick);
		void beginInsertFever(ScoreContext& context, int tick);
		void endInsertFever(ScoreContext& context, int tick);
		bool hasSelectedHiSpeed(const ScoreContext& context) const;
		const std::unordered_set<int>& selectedHiSpeeds() const { return selectedHiSpeedTicks; }
		bool hasSelectedSEVolume(const ScoreContext& context) const;
		bool copySelectedHiSpeed(const ScoreContext& context);
		bool copySelectedSEVolume(const ScoreContext& context);
		bool deleteSelectedHiSpeed(ScoreContext& context);
		bool deleteSelectedSEVolume(ScoreContext& context);
		bool cutSelectedHiSpeed(ScoreContext& context);
		bool cutSelectedSEVolume(ScoreContext& context);
		bool pasteHiSpeed(ScoreContext& context, bool flip);
		bool pasteSEVolume(ScoreContext& context, bool flip);

		ScoreEditorTimeline();
	};
}
