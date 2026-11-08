#include "StageEdit.h"
#include "../../Score.h"
#include <algorithm>
#include <set>
#include <set>

namespace Canvas::Stages
{
	using MikuMikuWorld::Score;

	uint32_t stageColor(int stageId)
	{
		// 0xAABBGGRR; stage 0 is a calm grey-blue, the rest cycle through a few clear hues
		static const uint32_t palette[] = {
			0xffd9a05b, 0xff5b8cf0, 0xff62c26b, 0xff4dc0f5, 0xffd666c9, 0xffd6c84a, 0xff6a6af0, 0xff9bd34f };
		if (stageId <= 0)
			return 0xffb0b0b0;
		return palette[(stageId - 1) % (sizeof(palette) / sizeof(palette[0]))];
	}

	std::vector<int> withHoldFamilies(const Score& score, const std::unordered_set<int>& selected)
	{
		std::set<int> out;
		for (int id : selected)
		{
			auto it = score.notes.find(id);
			if (it == score.notes.end())
				continue;
			out.insert(id);

			// a hold start is its own key in holdNotes; steps and ends point at it with parentID
			int holdId = id;
			if (score.holdNotes.find(holdId) == score.holdNotes.end())
				holdId = it->second.parentID;
			auto hold = score.holdNotes.find(holdId);
			if (hold == score.holdNotes.end())
				continue;
			out.insert(hold->second.start.ID);
			out.insert(hold->second.end);
			for (const auto& step : hold->second.steps)
				out.insert(step.ID);
		}

		std::vector<int> ids;
		for (int id : out)
			if (score.notes.find(id) != score.notes.end())
				ids.push_back(id);
		return ids;
	}

	void assignSelection(Score& score, const std::unordered_set<int>& selected, int stageId)
	{
		score.stages.assign(withHoldFamilies(score, selected), stageId);
	}

	std::vector<int> notesOfStage(const Score& score, int stageId)
	{
		std::vector<int> ids;
		for (const auto& [id, note] : score.notes)
			if (score.stages.stageOf(id) == stageId)
				ids.push_back(id);
		return ids;
	}

	void adoptNewNotes(Score& score, const std::vector<int>& ids, int likeNote)
	{
		if (!score.stages.enabled)
			return;
		int stage = likeNote >= 0 ? score.stages.stageOf(likeNote) : currentStage;
		if (stage == 0 || !score.stages.find(stage))
			return;
		score.stages.assign(ids, stage);
	}

	bool insertMaskAt(StageSet& set, int stageId, int tick)
	{
		if (!set.find(stageId))
			return false;
		const StageState now = evaluate(set, stageId, tick);
		MaskEvent e;
		e.tick = tick;
		e.lane = now.maskLane;
		e.size = now.maskHalfWidth;
		e.maskNotes = now.maskNotes;
		set.setMask(stageId, e);
		return true;
	}

	bool insertPivotAt(StageSet& set, int stageId, int tick)
	{
		if (!set.find(stageId))
			return false;
		const StageState now = evaluate(set, stageId, tick);
		PivotEvent e;
		e.tick = tick;
		e.lane = now.pivotLane;
		const bool second = now.divisionProgress > 0.5f;
		e.divisionSize = second ? now.divisionSizeTo : now.divisionSize;
		e.divisionParity = second ? now.divisionParityTo : now.divisionParity;
		e.yOffset = now.yOffset;
		e.yBeatOffset = now.yBeatOffset;
		set.setPivot(stageId, e);
		return true;
	}

	bool insertStyleAt(StageSet& set, int stageId, int tick)
	{
		if (!set.find(stageId))
			return false;
		const StageState now = evaluate(set, stageId, tick);
		const bool second = now.styleProgress > 0.5f;
		StyleEvent e;
		e.tick = tick;
		e.judgeLineColor = second ? now.judgeLineColorTo : now.judgeLineColor;
		e.judgeLineStyle = second ? now.judgeLineStyleTo : now.judgeLineStyle;
		e.leftBorder = second ? now.leftBorderTo : now.leftBorder;
		e.rightBorder = second ? now.rightBorderTo : now.rightBorder;
		e.fullWidth = now.fullWidth > 0.5f;
		e.noteAlpha = now.noteAlpha;
		e.laneAlpha = now.laneAlpha;
		e.judgeLineAlpha = now.judgeLineAlpha;
		e.divisionLineAlpha = now.divisionLineAlpha;
		set.setStyle(stageId, e);
		return true;
	}

	bool insertTransformAt(StageSet& set, int stageId, int tick)
	{
		if (!set.find(stageId))
			return false;
		const StageState now = evaluate(set, stageId, tick);
		TransformEvent e;
		e.tick = tick;
		e.rotate = now.rotate;
		e.x = now.x;
		e.y = now.y;
		e.elevation = now.elevation;
		e.anchor = now.centerWeight > 0.5f ? 1 : 0;
		set.setTransform(stageId, e);
		return true;
	}

	void insertCameraAt(StageSet& set, int tick)
	{
		const CameraState now = camera(set, tick);
		CameraEvent e;
		e.tick = tick;
		e.lane = now.lane;
		e.size = now.size;
		e.zoom = now.zoom;
		e.zoomTargetLane = now.zoomTargetLane;
		e.rotate = now.rotate;
		e.stageTilt = now.stageTilt;
		// the zoom target height / align are not in the blended state; take them from the joint before
		for (const CameraEvent& c : set.camera)
			if (c.tick <= tick)
			{
				e.zoomTargetY = c.zoomTargetY;
				e.zoomVerticalAlign = c.zoomVerticalAlign;
			}
		set.setCamera(e);
	}

	void fillDemo(Score& score)
	{
		StageSet& set = score.stages;
		set.enabled = true;
		if (set.stages.size() < 2)
			set.addStage("Side");
		const int side = set.stages.back().id;
		set.stages[0].name = "Main";
		set.find(side)->name = "Side";

		TransformEvent a, b, c;
		a.tick = 0;
		b.tick = 1920;
		b.rotate = 25;
		b.x = 3;
		b.y = 1;
		b.ease = EaseInOut;
		c.tick = 3840;
		c.rotate = -10;
		c.x = -2;
		set.setTransform(side, a);
		set.setTransform(side, b);
		set.setTransform(side, c);

		MaskEvent m0, m1;
		m0.tick = 0;
		m0.lane = 0;
		m0.size = 6;
		m1.tick = 1920;
		m1.lane = 2;
		m1.size = 3;
		set.setMask(side, m0);
		set.setMask(side, m1);

		// every other note moves to the side stage
		bool odd = false;
		for (const auto& [id, note] : score.notes)
		{
			if (odd && note.getType() == MikuMikuWorld::NoteType::Tap)
				set.assign(id, side);
			odd = !odd;
		}
		currentStage = side;
	}
}
