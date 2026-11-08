#include "DynamicStages.h"
#include "LevelDataSerializer.h"
#include "../../Constants.h"
#include "../../IO.h"
#include "../../Score.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace Canvas::Stages
{
	using json = nlohmann::json;
	using MikuMikuWorld::Score;

	namespace
	{
		constexpr double PI = 3.14159265358979323846;

		double lerp(double a, double b, double t) { return a + (b - a) * t; }
		double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

		// ---- the engine's layout constants (sekai/lib/layout.py), with the field 2 high and 16:9 wide
		constexpr double FIELD_H = 2.0;
		constexpr double FIELD_W = 2.0 * 16.0 / 9.0;
		constexpr double FIELD_T_FACTOR = 0.5 + 1.15875 * (47.0 / 1176.0);
		constexpr double FIELD_B_FACTOR = 0.5 - 1.15875 * (803.0 / 1176.0);
		constexpr double FIELD_W_FACTOR = (1.15875 * (1420.0 / 1176.0)) / (16.0 / 9.0) / 12.0;
		constexpr double APPROACH_TILT_LERP_MIN = 0.05;

		double approachScale() { return std::pow(1.06, -45.0); }
		double stageWidthMid() { return (approachScale() + 1.0) / 2.0; }

		// ---- easing (easings.net shapes, like sonolus.script.easing)
		double easeIn(int family, double x)
		{
			switch (family)
			{
			case 0: return x * x;
			case 1: return 1 - std::cos(x * PI / 2);
			case 2: return x * x * x;
			case 3: return x * x * x * x;
			case 4: return x * x * x * x * x;
			case 5: return x <= 0 ? 0 : std::pow(2.0, 10 * x - 10);
			case 6: return 1 - std::sqrt(std::max(0.0, 1 - x * x));
			case 7: { const double c1 = 1.70158, c3 = c1 + 1; return c3 * x * x * x - c1 * x * x; }
			default:
			{
				if (x <= 0) return 0;
				if (x >= 1) return 1;
				const double c4 = (2 * PI) / 3;
				return -std::pow(2.0, 10 * x - 10) * std::sin((x * 10 - 10.75) * c4);
			}
			}
		}

		double easeOut(int family, double x) { return 1 - easeIn(family, 1 - x); }

		double easeInOut(int family, double x)
		{
			if (family == 7)
			{
				const double c2 = 1.70158 * 1.525;
				return x < 0.5 ? (std::pow(2 * x, 2) * ((c2 + 1) * 2 * x - c2)) / 2
							   : (std::pow(2 * x - 2, 2) * ((c2 + 1) * (x * 2 - 2) + c2) + 2) / 2;
			}
			if (family == 8)
			{
				if (x <= 0) return 0;
				if (x >= 1) return 1;
				const double c5 = (2 * PI) / 4.5;
				return x < 0.5 ? -(std::pow(2.0, 20 * x - 10) * std::sin((20 * x - 11.125) * c5)) / 2
							   : (std::pow(2.0, -20 * x + 10) * std::sin((20 * x - 11.125) * c5)) / 2 + 1;
			}
			return x < 0.5 ? easeIn(family, 2 * x) / 2 : 1 - easeIn(family, 2 - 2 * x) / 2;
		}

		double easeOutIn(int family, double x)
		{
			return x < 0.5 ? easeOut(family, 2 * x) / 2 : easeIn(family, 2 * x - 1) / 2 + 0.5;
		}

		// ---- finding the two joints around a tick
		template <class E>
		struct Around
		{
			const E* a{};	// last joint at or before the tick
			const E* b{};	// the one after it
		};

		template <class E>
		Around<E> around(const std::vector<E>& v, double tick)
		{
			Around<E> r;
			auto it = std::upper_bound(v.begin(), v.end(), tick, [](double t, const E& e) { return t < e.tick; });
			if (it != v.begin())
				r.a = &*(it - 1);
			if (it != v.end())
				r.b = &*it;
			return r;
		}

		double progressOf(int easeId, double tick, int ta, int tb, const TickToSeconds& toSeconds)
		{
			double t = tick, a = ta, b = tb;
			if (toSeconds)
			{
				t = toSeconds(tick);
				a = toSeconds(ta);
				b = toSeconds(tb);
			}
			if (b - a <= 1e-9)
				return 1.0;
			return ease(easeId, static_cast<float>((t - a) / (b - a)));
		}

		template <class E>
		struct Segment
		{
			const E* a;
			const E* b;
			double p;
		};

		// the two joints to blend and how far along; a track without joints holds `base`
		template <class E>
		Segment<E> segmentOf(const std::vector<E>& v, const E& base, double tick, const TickToSeconds& toSeconds)
		{
			const auto br = around(v, tick);
			if (br.a && br.b)
				return { br.a, br.b, progressOf(br.a->ease, tick, br.a->tick, br.b->tick, toSeconds) };
			if (br.a)
				return { br.a, br.a, 0.0 };
			if (br.b)
				return { br.b, br.b, 0.0 };
			return { &base, &base, 0.0 };
		}

		template <class E>
		void upsert(std::vector<E>& v, const E& e)
		{
			auto it = std::lower_bound(v.begin(), v.end(), e.tick, [](const E& x, int tick) { return x.tick < tick; });
			if (it != v.end() && it->tick == e.tick)
				*it = e;
			else
				v.insert(it, e);
		}

		template <class E>
		void sortTrack(std::vector<E>& v)
		{
			std::stable_sort(v.begin(), v.end(), [](const E& a, const E& b) { return a.tick < b.tick; });
		}

		struct V2
		{
			double x, y;
		};

		// the engine's Vec2.rotate(-angle): the picture turns clockwise for a positive angle
		V2 rotateNeg(V2 v, double angle)
		{
			const double c = std::cos(angle), s = std::sin(angle);
			return { v.x * c + v.y * s, -v.x * s + v.y * c };
		}

		// derived camera values for one joint (layout.py: camera_zoom_target_at / camera_zoom_anchor)
		V2 zoomTarget(double lane, double size, double targetLane, double targetY, double tilt)
		{
			const double sizeZoom = 6.0 / size;
			const double w = FIELD_W * FIELD_W_FACTOR * sizeZoom;
			const double tTop = FIELD_H * FIELD_T_FACTOR;
			const double b = FIELD_H * FIELD_B_FACTOR;
			const double travel = approachDepth(static_cast<float>(1 - targetY), static_cast<float>(tilt));
			const double totalLane = lane + targetLane;
			const double widthFactor = tilt * travel + (1 - tilt) * stageWidthMid();
			return { totalLane * widthFactor * w - lane * w, travel * (b - tTop) + tTop };
		}

		double zoomAnchorY(int align) { return align == 1 ? 0.0 : FIELD_H * FIELD_B_FACTOR; }
	}

	// ======================================================================================= easing

	float ease(int id, float t)
	{
		double x = t;
		if (id <= EaseNone || id >= EaseCount)
			return id == EaseNone ? 0.0f : static_cast<float>(std::clamp(x, 0.0, 1.0));
		x = std::clamp(x, 0.0, 1.0);
		if (id == EaseLinear)
			return static_cast<float>(x);
		const int index = id - 2;
		const int family = index / 4, mode = index % 4;
		if (family == 9)
		{
			// steps keep the first value until the end, or jump at the start / middle
			switch (mode)
			{
			case 0: return x >= 1.0 ? 1.0f : 0.0f;
			case 1: return 1.0f;
			case 2: return x < 0.5 ? 0.0f : 1.0f;
			default: return 0.5f;
			}
		}
		switch (mode)
		{
		case 0: return static_cast<float>(easeIn(family, x));
		case 1: return static_cast<float>(easeOut(family, x));
		case 2: return static_cast<float>(easeInOut(family, x));
		default: return static_cast<float>(easeOutIn(family, x));
		}
	}

	const char* easeName(int id)
	{
		static const std::vector<std::string> names = []
		{
			static const char* families[] = { "quad", "sine", "cubic", "quart", "quint", "expo", "circ", "back", "elastic", "step" };
			static const char* modes[] = { "in", "out", "in-out", "out-in" };
			std::vector<std::string> out{ "none", "linear" };
			for (int f = 0; f < 10; ++f)
				for (int m = 0; m < 4; ++m)
					out.push_back(std::string(modes[m]) + " " + families[f]);
			return out;
		}();
		return id >= 0 && id < static_cast<int>(names.size()) ? names[id].c_str() : names[1].c_str();
	}

	// ======================================================================================= the set

	bool Stage::hasEvents() const
	{
		return !mask.empty() || !pivot.empty() || !style.empty() || !transform.empty();
	}

	StageSet::StageSet()
	{
		Stage main;
		main.id = 0;
		main.name = "Main";
		stages.push_back(main);
	}

	bool StageSet::empty() const
	{
		return stages.size() == 1 && !stages[0].hasEvents() && camera.empty() && noteStage.empty();
	}

	Stage* StageSet::find(int stageId)
	{
		for (Stage& s : stages)
			if (s.id == stageId)
				return &s;
		return nullptr;
	}

	const Stage* StageSet::find(int stageId) const
	{
		for (const Stage& s : stages)
			if (s.id == stageId)
				return &s;
		return nullptr;
	}

	int StageSet::addStage(const std::string& name)
	{
		int id = 0;
		for (const Stage& s : stages)
			id = std::max(id, s.id + 1);
		Stage s;
		s.id = id;
		s.name = name.empty() ? "Stage " + std::to_string(id + 1) : name;
		stages.push_back(s);
		return id;
	}

	bool StageSet::removeStage(int stageId)
	{
		if (stageId == 0)
			return false;
		auto it = std::find_if(stages.begin(), stages.end(), [&](const Stage& s) { return s.id == stageId; });
		if (it == stages.end())
			return false;
		stages.erase(it);
		for (auto n = noteStage.begin(); n != noteStage.end();)
			n = n->second == stageId ? noteStage.erase(n) : std::next(n);
		return true;
	}

	int StageSet::stageOf(int noteId) const
	{
		auto it = noteStage.find(noteId);
		return it == noteStage.end() ? 0 : it->second;
	}

	void StageSet::assign(int noteId, int stageId)
	{
		if (stageId == 0 || !find(stageId))
			noteStage.erase(noteId);
		else
			noteStage[noteId] = stageId;
	}

	void StageSet::assign(const std::vector<int>& noteIds, int stageId)
	{
		for (int id : noteIds)
			assign(id, stageId);
	}

	void StageSet::prune(const Score& score)
	{
		for (auto it = noteStage.begin(); it != noteStage.end();)
		{
			if (it->second == 0 || !find(it->second) || score.notes.find(it->first) == score.notes.end())
				it = noteStage.erase(it);
			else
				++it;
		}
	}

	void StageSet::remapNotes(const std::map<int, int>& oldToNew)
	{
		std::map<int, int> moved;
		for (const auto& [noteId, stageId] : noteStage)
		{
			auto it = oldToNew.find(noteId);
			if (it != oldToNew.end())
				moved[it->second] = stageId;
		}
		noteStage = std::move(moved);
	}

	void StageSet::sort()
	{
		std::stable_sort(stages.begin(), stages.end(), [](const Stage& a, const Stage& b) { return a.id < b.id; });
		if (stages.empty() || stages.front().id != 0)
		{
			Stage main;
			main.id = 0;
			main.name = "Main";
			stages.insert(stages.begin(), main);
		}
		for (Stage& s : stages)
		{
			sortTrack(s.mask);
			sortTrack(s.pivot);
			sortTrack(s.style);
			sortTrack(s.transform);
		}
		sortTrack(camera);
	}

	void StageSet::setMask(int id, const MaskEvent& e) { if (Stage* s = find(id)) upsert(s->mask, e); }
	void StageSet::setPivot(int id, const PivotEvent& e) { if (Stage* s = find(id)) upsert(s->pivot, e); }
	void StageSet::setStyle(int id, const StyleEvent& e) { if (Stage* s = find(id)) upsert(s->style, e); }
	void StageSet::setTransform(int id, const TransformEvent& e) { if (Stage* s = find(id)) upsert(s->transform, e); }
	void StageSet::setCamera(const CameraEvent& e) { upsert(camera, e); }

	// ======================================================================================= evaluation

	bool StageState::hasTransform() const
	{
		return rotate != 0.0f || x != 0.0f || y != 0.0f || centerWeight != 0.0f || elevation != 0.0f;
	}

	StageState evaluate(const StageSet& set, int stageId, double tick, const TickToSeconds& toSeconds)
	{
		StageState s;
		const Stage* stage = set.find(stageId);
		if (!stage)
		{
			s.visible = false;
			return s;
		}

		{
			const auto seg = segmentOf(stage->mask, stage->baseMask, tick, toSeconds);
			s.maskLane = static_cast<float>(lerp(seg.a->lane, seg.b->lane, seg.p));
			s.maskHalfWidth = static_cast<float>(std::max(0.0, lerp(seg.a->size, seg.b->size, seg.p)));
			s.maskNotes = seg.a->maskNotes;
		}
		{
			const auto seg = segmentOf(stage->pivot, stage->basePivot, tick, toSeconds);
			s.pivotLane = static_cast<float>(lerp(seg.a->lane, seg.b->lane, seg.p));
			s.yOffset = static_cast<float>(lerp(seg.a->yOffset, seg.b->yOffset, seg.p));
			s.yBeatOffset = static_cast<float>(lerp(seg.a->yBeatOffset, seg.b->yBeatOffset, seg.p));
			s.divisionSize = seg.a->divisionSize;
			s.divisionParity = seg.a->divisionParity;
			s.divisionSizeTo = seg.b->divisionSize;
			s.divisionParityTo = seg.b->divisionParity;
			s.divisionProgress = static_cast<float>(seg.p);
		}
		{
			const auto seg = segmentOf(stage->style, stage->baseStyle, tick, toSeconds);
			const StyleEvent &a = *seg.a, &b = *seg.b;
			s.judgeLineColor = a.judgeLineColor;
			s.judgeLineColorTo = b.judgeLineColor;
			s.judgeLineStyle = a.judgeLineStyle;
			s.judgeLineStyleTo = b.judgeLineStyle;
			s.leftBorder = a.leftBorder;
			s.leftBorderTo = b.leftBorder;
			s.rightBorder = a.rightBorder;
			s.rightBorderTo = b.rightBorder;
			s.styleProgress = static_cast<float>(seg.p);
			s.fullWidth = static_cast<float>(clamp01(lerp(a.fullWidth ? 1 : 0, b.fullWidth ? 1 : 0, seg.p)));
			s.noteAlpha = static_cast<float>(clamp01(lerp(a.noteAlpha, b.noteAlpha, seg.p)));
			s.laneAlpha = static_cast<float>(clamp01(lerp(a.laneAlpha, b.laneAlpha, seg.p)));
			s.judgeLineAlpha = static_cast<float>(clamp01(lerp(a.judgeLineAlpha, b.judgeLineAlpha, seg.p)));
			s.divisionLineAlpha = static_cast<float>(clamp01(lerp(a.divisionLineAlpha, b.divisionLineAlpha, seg.p)));
		}
		{
			const auto seg = segmentOf(stage->transform, stage->baseTransform, tick, toSeconds);
			s.rotate = static_cast<float>(lerp(seg.a->rotate, seg.b->rotate, seg.p));
			s.x = static_cast<float>(lerp(seg.a->x, seg.b->x, seg.p));
			s.y = static_cast<float>(lerp(seg.a->y, seg.b->y, seg.p));
			s.elevation = static_cast<float>(lerp(seg.a->elevation, seg.b->elevation, seg.p));
			s.centerWeight = static_cast<float>(lerp(seg.a->anchor == 1 ? 1 : 0, seg.b->anchor == 1 ? 1 : 0, seg.p));
		}

		// drawn from the first mask joint to the last, unless the stage is from start / until end.
		// (A stage with no mask joints exports one at tick 0, so that is what counts here.)
		const double inf = std::numeric_limits<double>::infinity();
		const double first = stage->mask.empty() ? 0.0 : stage->mask.front().tick;
		const double last = stage->mask.empty() ? 0.0 : stage->mask.back().tick;
		const double from = stage->fromStart ? -inf : first;
		const double until = stage->untilEnd ? inf : last;
		s.visible = tick >= from && tick <= until;
		return s;
	}

	CameraState camera(const StageSet& set, double tick, const TickToSeconds& toSeconds)
	{
		CameraState c;
		c.anchorY = static_cast<float>(0.0);
		if (set.camera.empty())
			return c;

		const CameraEvent none;
		const auto seg = segmentOf(set.camera, none, tick, toSeconds);
		const CameraEvent &a = *seg.a, &b = *seg.b;
		auto info = [](const CameraEvent& e)
		{
			const double size = std::max(0.01f, e.size);
			const double tilt = clamp01(e.stageTilt);
			return std::pair<V2, double>{ zoomTarget(e.lane, size, e.zoomTargetLane, e.zoomTargetY, tilt), zoomAnchorY(e.zoomVerticalAlign) };
		};
		const auto ia = info(a), ib = info(b);
		c.lane = static_cast<float>(lerp(a.lane, b.lane, seg.p));
		c.size = static_cast<float>(std::max(0.01, lerp(a.size, b.size, seg.p)));
		c.zoom = static_cast<float>(std::max(0.01, lerp(a.zoom, b.zoom, seg.p)));
		c.zoomTargetLane = static_cast<float>(lerp(a.zoomTargetLane, b.zoomTargetLane, seg.p));
		c.rotate = static_cast<float>(lerp(a.rotate, b.rotate, seg.p));
		c.stageTilt = static_cast<float>(lerp(a.stageTilt, b.stageTilt, seg.p));
		c.targetX = static_cast<float>(lerp(ia.first.x, ib.first.x, seg.p));
		c.targetY = static_cast<float>(lerp(ia.first.y, ib.first.y, seg.p));
		c.anchorY = static_cast<float>(lerp(ia.second, ib.second, seg.p));
		return c;
	}

	// ======================================================================================= projection

	float approachDepth(float progressIn, float stageTilt)
	{
		const double progress = progressIn;
		const double tilt = clamp01(stageTilt);
		const double AS = approachScale();
		auto base = [&](double x) { return std::pow(AS, 1 - x); };
		if (tilt >= 1.0)
			return static_cast<float>(base(progress));
		const double spawn = AS;
		if (tilt <= 0.0)
			return static_cast<float>(lerp(spawn, 1.0, progress));

		auto slice = [&](double tl)
		{
			const double wJudge = tl + (1 - tl) * stageWidthMid();
			const double sliceSpawn = 1.0 - tl * (1.0 - spawn) / wJudge;
			const double start = 1.0 - std::log(sliceSpawn) / std::log(AS);
			const double travel = base(lerp(start, 1.0, progress));
			return spawn + (travel - sliceSpawn) / (1.0 - sliceSpawn) * (1.0 - spawn);
		};
		if (tilt < APPROACH_TILT_LERP_MIN)
			return static_cast<float>(lerp(lerp(spawn, 1.0, progress), slice(APPROACH_TILT_LERP_MIN), tilt / APPROACH_TILT_LERP_MIN));
		return static_cast<float>(slice(tilt));
	}

	StageProjection::StageProjection(const StageState& stage, const CameraState& cam)
	{
		tilt = clamp01(cam.stageTilt);
		const double mid = stageWidthMid();

		// the camera: base layout, then the zoom around its target (layout.py base_ / zoomed_layout_transform)
		const double sizeZoom = 6.0 / std::max(0.01f, cam.size);
		const double t0 = FIELD_H * FIELD_T_FACTOR;
		const double w0 = FIELD_W * FIELD_W_FACTOR * sizeZoom;
		const double h0 = FIELD_H * FIELD_B_FACTOR - t0;
		const double x0 = -cam.lane * w0;
		const double zoom = std::max(0.01f, cam.zoom);
		t = zoom * (t0 - cam.targetY) + cam.anchorY;
		wScale = zoom * w0;
		hScale = zoom * h0;
		xTranslate = zoom * (x0 - cam.targetX);
		rotation = cam.rotate * PI / 180.0;

		// the stage on top of that (layout.py compute_stage_transform + StageTransform.to_screen_transform)
		const double travel = approachDepth(1.0f, static_cast<float>(tilt));
		const double widthAtJudge = tilt * travel + (1 - tilt) * mid;

		// elevation projection (the newest engine revision: the rise does not shrink with tilt)
		double amount = stage.elevation * wScale;
		if (tilt > 0)
			amount = std::min(amount, -hScale / tilt);
		const double scaleDelta = amount * tilt / hScale;
		const double shift = amount * ((1 - tilt) * mid - tilt * t / hScale);
		const double cr = std::cos(rotation), sr = std::sin(rotation);
		const double p00 = 1 + scaleDelta * sr * sr, p01 = scaleDelta * sr * cr, p02 = shift * sr;
		const double p10 = scaleDelta * sr * cr, p11 = 1 + scaleDelta * cr * cr, p12 = shift * cr;
		auto project = [&](V2 v) { return V2{ p00 * v.x + p01 * v.y + p02, p10 * v.x + p11 * v.y + p12 }; };

		V2 judgeCenter = rotateNeg({ stage.maskLane * widthAtJudge * wScale + xTranslate, travel * hScale + t }, rotation);
		judgeCenter = project(judgeCenter);
		const double baseT = FIELD_H * FIELD_T_FACTOR;
		const double baseH = FIELD_H * FIELD_B_FACTOR - baseT;
		const double centerJudgeY = hScale * (travel + baseT / baseH);
		const V2 offset = rotateNeg({ stage.x * wScale, stage.y * wScale - stage.centerWeight * centerJudgeY }, rotation);
		const V2 pivot = project(rotateNeg({ xTranslate, travel * (1 - tilt) * hScale + t }, rotation));

		const double sa = stage.rotate * PI / 180.0;
		const double cs = std::cos(sa), sn = std::sin(sa);
		const double dx = judgeCenter.x - pivot.x, dy = judgeCenter.y - pivot.y;
		const double tx = offset.x + (1 - cs) * dx - sn * dy;
		const double ty = offset.y + (1 - cs) * dy + sn * dx;

		a00 = cs * p00 + sn * p10;
		a01 = cs * p01 + sn * p11;
		a02 = cs * p02 + sn * p12 + pivot.x * (1 - cs) - sn * pivot.y + tx;
		a10 = -sn * p00 + cs * p10;
		a11 = -sn * p01 + cs * p11;
		a12 = -sn * p02 + cs * p12 + pivot.y * (1 - cs) + sn * pivot.x + ty;
	}

	Point StageProjection::applyEngine(float lane, float depth) const
	{
		const double width = tilt * depth + (1 - tilt) * stageWidthMid();
		const V2 v = rotateNeg({ lane * width * wScale + xTranslate, depth * hScale + t }, rotation);
		return { static_cast<float>(a00 * v.x + a01 * v.y + a02), static_cast<float>(a10 * v.x + a11 * v.y + a12) };
	}

	Point StageProjection::apply(float lane, float depth) const
	{
		const Point e = applyEngine(lane, depth);
		return { static_cast<float>(e.x / FIELD_W + 0.5), static_cast<float>(0.5 - e.y / FIELD_H) };
	}

	Point projectLane(const StageState& stage, const CameraState& cam, float lane, float depth)
	{
		return StageProjection(stage, cam).apply(lane, depth);
	}

	// ======================================================================================= JSON

	namespace
	{
		double numberOf(const json& o, const char* key, double fallback)
		{
			return o.is_object() && o.contains(key) && o[key].is_number() ? o[key].get<double>() : fallback;
		}

		int intOf(const json& o, const char* key, int fallback)
		{
			return o.is_object() && o.contains(key) && o[key].is_number() ? static_cast<int>(std::lround(o[key].get<double>())) : fallback;
		}

		bool boolOf(const json& o, const char* key, bool fallback)
		{
			if (!o.is_object() || !o.contains(key))
				return fallback;
			if (o[key].is_boolean())
				return o[key].get<bool>();
			if (o[key].is_number())
				return o[key].get<double>() != 0.0;
			return fallback;
		}

		int easeOf(const json& o) { return std::clamp(intOf(o, "ease", EaseLinear), 0, EaseCount - 1); }

		json toJ(const MaskEvent& e)
		{
			return { { "tick", e.tick }, { "lane", e.lane }, { "size", e.size }, { "maskNotes", e.maskNotes }, { "ease", e.ease } };
		}
		json toJ(const PivotEvent& e)
		{
			return { { "tick", e.tick }, { "lane", e.lane }, { "divisionSize", e.divisionSize }, { "divisionParity", e.divisionParity },
				{ "yOffset", e.yOffset }, { "yBeatOffset", e.yBeatOffset }, { "ease", e.ease } };
		}
		json toJ(const StyleEvent& e)
		{
			return { { "tick", e.tick }, { "judgeLineColor", e.judgeLineColor }, { "judgeLineStyle", e.judgeLineStyle }, { "leftBorder", e.leftBorder },
				{ "rightBorder", e.rightBorder }, { "fullWidth", e.fullWidth }, { "noteAlpha", e.noteAlpha }, { "laneAlpha", e.laneAlpha },
				{ "judgeLineAlpha", e.judgeLineAlpha }, { "divisionLineAlpha", e.divisionLineAlpha }, { "editorLane", e.editorLane }, { "ease", e.ease } };
		}
		json toJ(const TransformEvent& e)
		{
			return { { "tick", e.tick }, { "rotate", e.rotate }, { "x", e.x }, { "y", e.y }, { "elevation", e.elevation }, { "anchor", e.anchor }, { "ease", e.ease } };
		}
		json toJ(const CameraEvent& e)
		{
			return { { "tick", e.tick }, { "lane", e.lane }, { "size", e.size }, { "zoom", e.zoom }, { "zoomTargetLane", e.zoomTargetLane },
				{ "zoomTargetY", e.zoomTargetY }, { "zoomVerticalAlign", e.zoomVerticalAlign }, { "rotate", e.rotate }, { "stageTilt", e.stageTilt },
				{ "ease", e.ease } };
		}

		void fromJ(const json& o, MaskEvent& e)
		{
			e.tick = intOf(o, "tick", 0);
			e.lane = static_cast<float>(numberOf(o, "lane", 0));
			e.size = static_cast<float>(std::max(0.0, numberOf(o, "size", 6)));
			e.maskNotes = boolOf(o, "maskNotes", false);
			e.ease = easeOf(o);
		}
		void fromJ(const json& o, PivotEvent& e)
		{
			e.tick = intOf(o, "tick", 0);
			e.lane = static_cast<float>(numberOf(o, "lane", 0));
			e.divisionSize = std::max(1, intOf(o, "divisionSize", 2));
			e.divisionParity = std::clamp(intOf(o, "divisionParity", 0), 0, 1);
			e.yOffset = static_cast<float>(numberOf(o, "yOffset", 0));
			e.yBeatOffset = static_cast<float>(numberOf(o, "yBeatOffset", 0));
			e.ease = easeOf(o);
		}
		void fromJ(const json& o, StyleEvent& e)
		{
			e.tick = intOf(o, "tick", 0);
			e.judgeLineColor = std::clamp(intOf(o, "judgeLineColor", 0), 0, 7);
			e.judgeLineStyle = std::clamp(intOf(o, "judgeLineStyle", 0), 0, 1);
			e.leftBorder = std::clamp(intOf(o, "leftBorder", 0), 0, 3);
			e.rightBorder = std::clamp(intOf(o, "rightBorder", 0), 0, 3);
			e.fullWidth = boolOf(o, "fullWidth", false);
			e.noteAlpha = static_cast<float>(clamp01(numberOf(o, "noteAlpha", 1)));
			e.laneAlpha = static_cast<float>(clamp01(numberOf(o, "laneAlpha", 1)));
			e.judgeLineAlpha = static_cast<float>(clamp01(numberOf(o, "judgeLineAlpha", 1)));
			e.divisionLineAlpha = static_cast<float>(clamp01(numberOf(o, "divisionLineAlpha", 1)));
			e.editorLane = intOf(o, "editorLane", 0);
			e.ease = easeOf(o);
		}
		void fromJ(const json& o, TransformEvent& e)
		{
			e.tick = intOf(o, "tick", 0);
			e.rotate = static_cast<float>(numberOf(o, "rotate", 0));
			e.x = static_cast<float>(numberOf(o, "x", 0));
			e.y = static_cast<float>(numberOf(o, "y", 0));
			e.elevation = static_cast<float>(numberOf(o, "elevation", 0));
			e.anchor = std::clamp(intOf(o, "anchor", 0), 0, 1);
			e.ease = easeOf(o);
		}
		void fromJ(const json& o, CameraEvent& e)
		{
			e.tick = intOf(o, "tick", 0);
			e.lane = static_cast<float>(numberOf(o, "lane", 0));
			e.size = static_cast<float>(std::max(0.01, numberOf(o, "size", 6)));
			e.zoom = static_cast<float>(std::max(0.01, numberOf(o, "zoom", 1)));
			e.zoomTargetLane = static_cast<float>(numberOf(o, "zoomTargetLane", 0));
			e.zoomTargetY = static_cast<float>(std::clamp(numberOf(o, "zoomTargetY", 0), -1.0, 1.0));
			e.zoomVerticalAlign = std::clamp(intOf(o, "zoomVerticalAlign", 0), 0, 1);
			e.rotate = static_cast<float>(numberOf(o, "rotate", 0));
			e.stageTilt = static_cast<float>(clamp01(numberOf(o, "stageTilt", 1)));
			e.ease = easeOf(o);
		}

		template <class E>
		json trackToJ(const std::vector<E>& v)
		{
			json a = json::array();
			for (const E& e : v)
				a.push_back(toJ(e));
			return a;
		}

		template <class E>
		void trackFromJ(const json& o, const char* key, std::vector<E>& v)
		{
			v.clear();
			if (!o.is_object() || !o.contains(key) || !o[key].is_array())
				return;
			for (const json& item : o[key])
			{
				if (!item.is_object())
					continue;
				E e;
				fromJ(item, e);
				v.push_back(e);
			}
			sortTrack(v);
		}
	}

	std::string toJson(const StageSet& set)
	{
		json stages = json::array();
		for (const Stage& s : set.stages)
		{
			json base = { { "mask", toJ(s.baseMask) }, { "pivot", toJ(s.basePivot) }, { "style", toJ(s.baseStyle) }, { "transform", toJ(s.baseTransform) } };
			stages.push_back({ { "id", s.id }, { "name", s.name }, { "laneCount", s.laneCount }, { "fromStart", s.fromStart }, { "untilEnd", s.untilEnd },
				{ "isolatedSimLines", s.isolatedSimLines }, { "base", base }, { "mask", trackToJ(s.mask) }, { "pivot", trackToJ(s.pivot) },
				{ "style", trackToJ(s.style) }, { "transform", trackToJ(s.transform) } });
		}
		json notes = json::array();
		for (const auto& [noteId, stageId] : set.noteStage)
			notes.push_back({ noteId, stageId });
		return json{ { "version", 1 }, { "enabled", set.enabled }, { "stages", stages }, { "camera", trackToJ(set.camera) }, { "notes", notes } }.dump();
	}

	bool fromJson(const std::string& text, StageSet& set)
	{
		const json root = json::parse(text, nullptr, false);
		if (!root.is_object())
			return false;

		set = StageSet();
		set.stages.clear();
		set.enabled = boolOf(root, "enabled", false);
		if (root.contains("stages") && root["stages"].is_array())
		{
			for (const json& o : root["stages"])
			{
				if (!o.is_object())
					continue;
				Stage s;
				s.id = intOf(o, "id", static_cast<int>(set.stages.size()));
				if (set.find(s.id))
					continue;	// a repeated id: the first one wins
				s.name = o.contains("name") && o["name"].is_string() ? o["name"].get<std::string>() : "Stage";
				s.laneCount = std::clamp(intOf(o, "laneCount", 12), 1, 24);
				s.fromStart = boolOf(o, "fromStart", true);
				s.untilEnd = boolOf(o, "untilEnd", true);
				s.isolatedSimLines = boolOf(o, "isolatedSimLines", false);
				if (o.contains("base") && o["base"].is_object())
				{
					const json& b = o["base"];
					if (b.contains("mask")) fromJ(b["mask"], s.baseMask);
					if (b.contains("pivot")) fromJ(b["pivot"], s.basePivot);
					if (b.contains("style")) fromJ(b["style"], s.baseStyle);
					if (b.contains("transform")) fromJ(b["transform"], s.baseTransform);
				}
				trackFromJ(o, "mask", s.mask);
				trackFromJ(o, "pivot", s.pivot);
				trackFromJ(o, "style", s.style);
				trackFromJ(o, "transform", s.transform);
				set.stages.push_back(std::move(s));
			}
		}
		trackFromJ(root, "camera", set.camera);
		if (root.contains("notes") && root["notes"].is_array())
		{
			for (const json& pair : root["notes"])
				if (pair.is_array() && pair.size() == 2 && pair[0].is_number() && pair[1].is_number())
					set.noteStage[pair[0].get<int>()] = pair[1].get<int>();
		}
		set.sort();	// puts the default stage back if the file had none
		for (auto it = set.noteStage.begin(); it != set.noteStage.end();)
			it = (it->second == 0 || !set.find(it->second)) ? set.noteStage.erase(it) : std::next(it);
		return true;
	}

	// ======================================================================================= LevelData: export

	namespace
	{
		using MikuMikuWorld::TICKS_PER_BEAT;

		double tickToBeat(int tick) { return tick / static_cast<double>(TICKS_PER_BEAT); }
		int beatToTick(double beat) { return static_cast<int>(std::lround(beat * TICKS_PER_BEAT)); }

		bool endsWith(const std::string& s, const char* tail)
		{
			const size_t n = std::char_traits<char>::length(tail);
			return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
		}
		bool startsWith(const std::string& s, const char* head) { return s.rfind(head, 0) == 0; }
		bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

		json valueData(const char* name, double v) { return { { "name", name }, { "value", v } }; }
		json refData(const char* name, const std::string& r) { return { { "name", name }, { "ref", r } }; }

		// note entities in the order LevelDataSerializer::exportNextSekai creates them (it skips nothing
		// and adds the transient / connector / sim line entities between them, which we filter by archetype)
		std::vector<int> exporterNoteOrder(const Score& score)
		{
			using namespace MikuMikuWorld;
			std::vector<int> order;
			for (const auto& [id, note] : score.notes)
				if (note.getType() == NoteType::Tap)
					order.push_back(id);
			for (int pass = 0; pass < 2; ++pass)
			{
				// slides first, then guides
				for (const auto& [id, hold] : score.holdNotes)
				{
					if (hold.isGuide() != (pass == 1))
						continue;
					order.push_back(hold.start.ID);
					for (const auto& step : hold.steps)
						order.push_back(step.ID);
					order.push_back(hold.end);
				}
			}
			return order;
		}

		class Namer
		{
		public:
			explicit Namer(const json& entities)
			{
				for (const json& e : entities)
					if (e.contains("name") && e["name"].is_string())
						used.insert(e["name"].get<std::string>());
			}

			std::string next(const char* prefix)
			{
				std::string name;
				do
					name = std::string(prefix) + std::to_string(counter++);
				while (used.count(name));
				used.insert(name);
				return name;
			}

		private:
			std::set<std::string> used;
			int counter = 0;
		};

		// one linked chain of joints; returns the first one's name (empty for an empty track)
		template <class E, class Fields>
		std::string emitChain(std::vector<json>& out, Namer& namer, const std::vector<E>& track, const char* archetype, const char* prefix, Fields fields)
		{
			std::vector<size_t> at;
			for (const E& e : track)
			{
				json entity = { { "name", namer.next(prefix) }, { "archetype", archetype }, { "data", json::array() } };
				entity["data"].push_back(valueData("#BEAT", tickToBeat(e.tick)));
				fields(entity["data"], e);
				at.push_back(out.size());
				out.push_back(std::move(entity));
			}
			for (size_t i = 0; i + 1 < at.size(); ++i)
				out[at[i]]["data"].push_back(refData("next", out[at[i + 1]]["name"].get<std::string>()));
			return at.empty() ? std::string() : out[at.front()]["name"].get<std::string>();
		}
	}

	bool exportLevelData(const Score& score, const StageSet& setIn, std::string& outJson, std::string& error, std::vector<std::string>* warnings)
	{
		try
		{
			StageSet set = setIn;
			set.sort();

			for (const auto& [noteId, stageId] : set.noteStage)
				if (!set.find(stageId) && score.notes.count(noteId))
				{
					error = "Note " + std::to_string(noteId) + " is on stage " + std::to_string(stageId) + ", which does not exist.";
					return false;
				}

			MikuMikuWorld::LevelDataSerializer serializer(false);
			json level = serializer.exportNextSekai(score);
			if (warnings)
				warnings->insert(warnings->end(), serializer.warnings.begin(), serializer.warnings.end());
			if (!level.contains("entities") || !level["entities"].is_array() || level["entities"].empty() || level["entities"][0].value("archetype", "") != "Initialization")
			{
				error = "The normal LevelData export has an unexpected shape (no Initialization entity first).";
				return false;
			}

			json& entities = level["entities"];
			Namer namer(entities);

			// ---- every note entity gets its stage
			const std::vector<int> order = exporterNoteOrder(score);
			std::unordered_map<std::string, int> stageOfEntity;
			size_t index = 0;
			for (json& e : entities)
			{
				const std::string archetype = e.value("archetype", "");
				if (!endsWith(archetype, "Note") || archetype == "TransientHiddenTickNote")
					continue;
				if (index >= order.size())
				{
					error = "Internal: the export has more note entities than the chart has notes.";
					return false;
				}
				const int noteId = order[index++];
				const auto note = score.notes.find(noteId);
				double beat = 0;
				for (const json& d : e["data"])
					if (d.value("name", "") == "#BEAT" && d.contains("value"))
						beat = d["value"].get<double>();
				if (note == score.notes.end() || std::abs(beat * TICKS_PER_BEAT - note->second.tick) > 0.51)
				{
					error = "Internal: could not line the exported notes up with the chart's notes (note " + std::to_string(noteId) + ").";
					return false;
				}
				const int stageId = set.stageOf(noteId);
				e["data"].push_back(refData("stage", "stage" + std::to_string(stageId)));
				stageOfEntity[e.value("name", "")] = stageId;
			}
			if (index != order.size())
			{
				error = "Internal: the export has fewer note entities than the chart has notes.";
				return false;
			}

			// ---- sim lines never cross into an isolated stage
			{
				json kept = json::array();
				int dropped = 0;
				for (json& e : entities)
				{
					if (e.value("archetype", "") == "SimLine")
					{
						int l = -1, r = -1;
						for (const json& d : e["data"])
						{
							const auto it = d.contains("ref") ? stageOfEntity.find(d["ref"].get<std::string>()) : stageOfEntity.end();
							if (it == stageOfEntity.end())
								continue;
							(d["name"] == "left" ? l : r) = it->second;
						}
						const Stage* sl = set.find(l);
						const Stage* sr = set.find(r);
						if (l != r && ((sl && sl->isolatedSimLines) || (sr && sr->isolatedSimLines)))
						{
							++dropped;
							continue;
						}
					}
					kept.push_back(std::move(e));
				}
				entities = std::move(kept);
				if (dropped && warnings)
					warnings->push_back(std::to_string(dropped) + " simultaneous line(s) between an isolated stage and another stage were left out.");
			}

			// ---- stages and their joints
			std::vector<json> added;
			for (const Stage& stage : set.stages)
			{
				std::string stageName = "stage" + std::to_string(stage.id);
				json stageEntity = { { "name", stageName }, { "archetype", "Stage" }, { "data", json::array() } };
				auto& data = stageEntity["data"];
				data.push_back(refData("editorName", stage.name));
				data.push_back(valueData("fromStart", stage.fromStart));
				data.push_back(valueData("untilEnd", stage.untilEnd));
				data.push_back(valueData("generateSimLines", stage.isolatedSimLines ? 1 : 0));

				// a track with no joints writes its base values as one joint at tick 0
				auto orBase = [](const auto& track, auto base)
				{
					if (!track.empty())
						return track;
					base.tick = 0;
					return std::decay_t<decltype(track)>{ base };
				};

				if (!stage.fromStart && !stage.untilEnd && stage.mask.empty() && warnings)
					warnings->push_back("Stage \"" + stage.name + "\" has no mask joints and is not from start / until end: it shows for one instant only.");

				std::vector<json> chains;
				std::string first = emitChain(chains, namer, orBase(stage.mask, stage.baseMask), "StageMaskChange", "mask", [](json& d, const MaskEvent& e)
				{
					d.push_back(valueData("lane", e.lane));
					d.push_back(valueData("size", e.size));
					d.push_back(valueData("maskNotes", e.maskNotes ? 1 : 0));
					d.push_back(valueData("ease", e.ease));
				});
				if (!first.empty())
					data.push_back(refData("firstMaskChange", first));

				first = emitChain(chains, namer, orBase(stage.pivot, stage.basePivot), "StagePivotChange", "pivot", [](json& d, const PivotEvent& e)
				{
					d.push_back(valueData("lane", e.lane));
					d.push_back(valueData("divisionSize", e.divisionSize));
					d.push_back(valueData("divisionParity", e.divisionParity));
					d.push_back(valueData("yOffset", e.yOffset));
					d.push_back(valueData("yBeatOffset", e.yBeatOffset));
					d.push_back(valueData("ease", e.ease));
				});
				if (!first.empty())
					data.push_back(refData("firstPivotChange", first));

				first = emitChain(chains, namer, orBase(stage.style, stage.baseStyle), "StageStyleChange", "style", [](json& d, const StyleEvent& e)
				{
					d.push_back(valueData("editorLane", e.editorLane));
					d.push_back(valueData("judgeLineColor", e.judgeLineColor));
					d.push_back(valueData("judgeLineStyle", e.judgeLineStyle));
					d.push_back(valueData("leftBorderStyle", e.leftBorder));
					d.push_back(valueData("rightBorderStyle", e.rightBorder));
					d.push_back(valueData("fullWidth", e.fullWidth ? 1 : 0));
					d.push_back(valueData("noteAlpha", e.noteAlpha));
					d.push_back(valueData("laneAlpha", e.laneAlpha));
					d.push_back(valueData("judgeLineAlpha", e.judgeLineAlpha));
					d.push_back(valueData("divisionLineAlpha", e.divisionLineAlpha));
					d.push_back(valueData("ease", e.ease));
				});
				if (!first.empty())
					data.push_back(refData("firstStyleChange", first));

				// no joints and a flat base: the engine's default (no transform) is already that
				const TransformEvent& bt = stage.baseTransform;
				const bool baseIdentity = bt.rotate == 0 && bt.x == 0 && bt.y == 0 && bt.elevation == 0 && bt.anchor == 0;
				if (!stage.transform.empty() || !baseIdentity)
				{
					first = emitChain(chains, namer, orBase(stage.transform, stage.baseTransform), "StageTransformChange", "xform", [](json& d, const TransformEvent& e)
					{
						d.push_back(valueData("rotate", e.rotate));
						d.push_back(valueData("xLaneTranslate", e.x));
						d.push_back(valueData("yLaneTranslate", e.y));
						d.push_back(valueData("elevation", e.elevation));
						d.push_back(valueData("anchor", e.anchor));
						d.push_back(valueData("ease", e.ease));
					});
					if (!first.empty())
						data.push_back(refData("firstTransformChange", first));
				}

				added.push_back(std::move(stageEntity));
				for (json& c : chains)
					added.push_back(std::move(c));
			}

			// ---- camera, hooked into the Initialization entity
			std::vector<json> cameraChain;
			const std::string firstCamera = emitChain(cameraChain, namer, set.camera, "CameraChange", "cam", [](json& d, const CameraEvent& e)
			{
				d.push_back(valueData("lane", e.lane));
				d.push_back(valueData("size", e.size));
				d.push_back(valueData("zoom", e.zoom));
				d.push_back(valueData("zoomTargetLane", e.zoomTargetLane));
				d.push_back(valueData("zoomTargetY", e.zoomTargetY));
				d.push_back(valueData("zoomVerticalAlign", e.zoomVerticalAlign));
				d.push_back(valueData("rotate", e.rotate));
				d.push_back(valueData("stageTilt", e.stageTilt));
				d.push_back(valueData("ease", e.ease));
			});
			if (!firstCamera.empty())
				entities[0]["data"].push_back(refData("firstCamera", firstCamera));
			for (json& c : cameraChain)
				added.push_back(std::move(c));

			// stages right behind Initialization, the rest as it was
			json merged = json::array();
			merged.push_back(std::move(entities[0]));
			for (json& a : added)
				merged.push_back(std::move(a));
			for (size_t i = 1; i < entities.size(); ++i)
				merged.push_back(std::move(entities[i]));
			entities = std::move(merged);

			outJson = level.dump();
			return true;
		}
		catch (const std::exception& e)
		{
			error = e.what();
			return false;
		}
	}

	// ======================================================================================= LevelData: import

	namespace
	{
		struct Raw
		{
			std::string name, archetype;
			std::unordered_map<std::string, double> values;
			std::unordered_map<std::string, std::string> refs;

			bool has(const std::string& key) const { return values.count(key) || refs.count(key); }
			double num(const std::string& key, double fallback = 0.0) const
			{
				auto it = values.find(key);
				return it == values.end() ? fallback : it->second;
			}
			std::string ref(const std::string& key) const
			{
				auto it = refs.find(key);
				return it == refs.end() ? std::string() : it->second;
			}
		};

		Raw readRaw(const json& e)
		{
			Raw raw;
			raw.name = e.value("name", "");
			raw.archetype = e.value("archetype", "");
			if (e.contains("data") && e["data"].is_array())
			{
				for (const json& d : e["data"])
				{
					if (!d.is_object() || !d.contains("name") || !d["name"].is_string())
						continue;
					const std::string key = d["name"].get<std::string>();
					if (d.contains("ref") && d["ref"].is_string())
						raw.refs[key] = d["ref"].get<std::string>();
					else if (d.contains("value") && d["value"].is_number())
						raw.values[key] = d["value"].get<double>();
				}
			}
			return raw;
		}

		bool isStageArchetype(const std::string& a)
		{
			return a == "Stage" || a == "StageMaskChange" || a == "StagePivotChange" || a == "StageStyleChange" || a == "StageTransformChange" || a == "CameraChange";
		}

		// The notes LevelDataSerializer::importNextSekai makes, in the order it makes them (kept in
		// step with that function: singles first, then every chain; fakes, damage and invisible
		// attached ticks do not become notes). Each entry is the entity the note came from.
		std::vector<const Raw*> importerNoteOrder(const std::vector<Raw>& entities, const std::unordered_map<std::string, const Raw*>& byName)
		{
			std::vector<const Raw*> order;
			std::unordered_set<std::string> referenced;
			for (const Raw& e : entities)
			{
				const std::string next = e.ref("next");
				if (!next.empty() && e.archetype != "#TIMESCALE_CHANGE")
					referenced.insert(next);
			}

			for (const Raw& e : entities)
			{
				const std::string& a = e.archetype;
				if (!contains(a, "Note") || a == "AnchorNote" || a == "TransientHiddenTickNote" || a == "FakeAnchorNote" || a == "FakeTransientHiddenTickNote")
					continue;
				if (e.has("activeHead") || contains(a, "Head") || contains(a, "Tail") || contains(a, "Tick"))
					continue;
				if (referenced.count(e.name) || !e.ref("next").empty())
					continue;
				if (startsWith(a, "Fake") || startsWith(a, "Damage"))
					continue;
				order.push_back(&e);
			}

			for (const Raw& e : entities)
			{
				if (e.name.empty() || referenced.count(e.name) || e.ref("next").empty() || e.archetype == "#TIMESCALE_CHANGE" || e.archetype == "#TIMESCALE_GROUP")
					continue;

				std::vector<const Raw*> chain;
				const Raw* current = &e;
				int guard = 0;
				while (current && guard++ < 100000)
				{
					chain.push_back(current);
					auto it = byName.find(current->ref("next"));
					current = it == byName.end() ? nullptr : it->second;
				}
				if (chain.size() < 2)
					continue;

				const int kind = static_cast<int>(e.num("segmentKind", 1));
				if ((kind >= 50 && kind < 100) || startsWith(e.archetype, "Fake"))
					continue;

				const bool allAnchors = std::all_of(chain.begin(), chain.end(), [](const Raw* n) { return n->archetype == "AnchorNote"; });
				const bool guide = allAnchors && (e.has("segmentKind") ? kind >= 100 : !e.has("activeHead"));
				for (size_t i = 0; i < chain.size(); ++i)
				{
					if (!guide && i > 0 && i + 1 < chain.size())
					{
						const Raw& n = *chain[i];
						const std::string a = startsWith(n.archetype, "Fake") ? n.archetype.substr(4) : n.archetype;
						if (static_cast<int>(n.num("isAttached")) == 1 && startsWith(a, "TransientHiddenTick"))
							continue;
					}
					order.push_back(chain[i]);
				}
			}
			return order;
		}

		// follow a `next` chain from `first`; stops (with a note) at a missing entity
		template <class Fn>
		void walkChain(const std::unordered_map<std::string, const Raw*>& byName, const std::string& first, size_t limit, std::vector<std::string>& problems, Fn visit)
		{
			std::string name = first;
			size_t guard = 0;
			while (!name.empty() && guard++ <= limit)
			{
				auto it = byName.find(name);
				if (it == byName.end())
				{
					problems.push_back("A joint chain points at \"" + name + "\", which is not in the file; the chain stops there.");
					return;
				}
				visit(*it->second);
				name = it->second->ref("next");
			}
		}

		int easeOfRaw(const Raw& r) { return std::clamp(static_cast<int>(std::lround(r.num("ease", EaseLinear))), 0, EaseCount - 1); }
	}

	bool importLevelData(const std::string& bytesIn, Score& outScore, StageSet& outStages, std::string& error, std::vector<std::string>* warnings)
	{
		try
		{
			std::vector<uint8_t> bytes(bytesIn.begin(), bytesIn.end());
			if (IO::isGzipCompressed(bytes))
				bytes = IO::inflateGzip(bytes);

			const json root = json::parse(bytes.begin(), bytes.end(), nullptr, false);
			if (!root.is_object() || !root.contains("entities") || !root["entities"].is_array())
			{
				error = "Not a Sonolus LevelData file (no entities).";
				return false;
			}

			std::vector<std::string> problems;
			std::vector<Raw> all;
			for (const json& e : root["entities"])
				all.push_back(readRaw(e));
			std::unordered_map<std::string, const Raw*> allByName;
			for (const Raw& r : all)
				if (!r.name.empty())
					allByName[r.name] = &r;

			StageSet set;
			set.stages.clear();
			std::unordered_map<std::string, int> stageIds;

			// ---- stages and their joint chains
			for (const Raw& e : all)
			{
				if (e.archetype != "Stage")
					continue;
				Stage s;
				s.id = static_cast<int>(set.stages.size());
				s.name = e.ref("editorName").empty() ? "Stage " + std::to_string(s.id + 1) : e.ref("editorName");
				s.fromStart = e.num("fromStart", 0) != 0;
				s.untilEnd = e.num("untilEnd", 0) != 0;
				s.isolatedSimLines = e.num("generateSimLines", 0) == 1;
				if (!e.name.empty())
					stageIds[e.name] = s.id;
				const size_t limit = all.size();

				walkChain(allByName, e.ref("firstMaskChange"), limit, problems, [&](const Raw& r)
				{
					MaskEvent m;
					m.tick = beatToTick(r.num("#BEAT"));
					m.lane = static_cast<float>(r.num("lane"));
					m.size = static_cast<float>(std::max(0.0, r.num("size")));
					m.maskNotes = r.num("maskNotes", 0) != 0;
					m.ease = easeOfRaw(r);
					s.mask.push_back(m);
				});
				walkChain(allByName, e.ref("firstPivotChange"), limit, problems, [&](const Raw& r)
				{
					PivotEvent p;
					p.tick = beatToTick(r.num("#BEAT"));
					p.lane = static_cast<float>(r.num("lane"));
					p.divisionSize = std::max(1, static_cast<int>(std::lround(r.num("divisionSize", 1))));
					p.divisionParity = r.num("divisionParity", 0) == 1 ? 1 : 0;
					p.yOffset = static_cast<float>(r.num("yOffset"));
					p.yBeatOffset = static_cast<float>(r.num("yBeatOffset"));
					p.ease = easeOfRaw(r);
					s.pivot.push_back(p);
				});
				walkChain(allByName, e.ref("firstStyleChange"), limit, problems, [&](const Raw& r)
				{
					StyleEvent st;
					st.tick = beatToTick(r.num("#BEAT"));
					st.editorLane = static_cast<int>(std::lround(r.num("editorLane", 0)));
					st.judgeLineColor = std::clamp(static_cast<int>(std::lround(r.num("judgeLineColor"))), 0, 7);
					st.judgeLineStyle = std::clamp(static_cast<int>(std::lround(r.num("judgeLineStyle"))), 0, 1);
					st.leftBorder = std::clamp(static_cast<int>(std::lround(r.num("leftBorderStyle"))), 0, 3);
					st.rightBorder = std::clamp(static_cast<int>(std::lround(r.num("rightBorderStyle"))), 0, 3);
					st.fullWidth = r.num("fullWidth", 0) != 0;
					const double alpha = r.num("alpha", 1);	// deprecated, multiplies the two below
					st.noteAlpha = static_cast<float>(clamp01(r.num("noteAlpha", 1)));
					st.laneAlpha = static_cast<float>(clamp01(r.num("laneAlpha", 1) * alpha));
					st.judgeLineAlpha = static_cast<float>(clamp01(r.num("judgeLineAlpha", 1) * alpha));
					st.divisionLineAlpha = static_cast<float>(clamp01(r.num("divisionLineAlpha", 1)));
					st.ease = easeOfRaw(r);
					s.style.push_back(st);
				});
				walkChain(allByName, e.ref("firstTransformChange"), limit, problems, [&](const Raw& r)
				{
					TransformEvent t;
					t.tick = beatToTick(r.num("#BEAT"));
					t.rotate = static_cast<float>(r.num("rotate"));
					t.x = static_cast<float>(r.num("xLaneTranslate"));
					t.y = static_cast<float>(r.num("yLaneTranslate"));
					t.elevation = static_cast<float>(r.num("elevation"));
					t.anchor = r.num("anchor", 0) == 1 ? 1 : 0;
					t.ease = easeOfRaw(r);
					s.transform.push_back(t);
				});
				set.stages.push_back(std::move(s));
			}
			const bool dynamic = !set.stages.empty();
			if (!dynamic)
			{
				// a normal chart: just the default stage
				Stage main;
				main.name = "Main";
				set.stages.push_back(main);
			}

			// ---- camera
			for (const Raw& e : all)
			{
				if (e.archetype != "Initialization")
					continue;
				walkChain(allByName, e.ref("firstCamera"), all.size(), problems, [&](const Raw& r)
				{
					CameraEvent c;
					c.tick = beatToTick(r.num("#BEAT"));
					const double size = r.num("size", 6);
					c.lane = static_cast<float>(r.num("lane"));
					c.size = static_cast<float>(std::max(0.01, size));
					c.zoom = static_cast<float>(std::max(0.01, r.num("zoom", 1)));
					c.zoomTargetLane = static_cast<float>(r.num("zoomTargetLane"));
					c.zoomTargetY = static_cast<float>(std::clamp(r.num("zoomTargetY"), -1.0, 1.0));
					c.zoomVerticalAlign = r.num("zoomVerticalAlign", 0) == 1 ? 1 : 0;
					c.rotate = static_cast<float>(r.num("rotate"));
					c.stageTilt = static_cast<float>(clamp01(r.num("stageTilt", 1)));
					c.ease = easeOfRaw(r);
					set.camera.push_back(c);
				});
				break;
			}
			set.enabled = dynamic || !set.camera.empty();

			// ---- the notes: hand the normal chart to LevelDataSerializer (it knows nothing about stages,
			// and would take the joint chains for slides), then match its notes back to their entities
			json notesOnly = root;
			json kept = json::array();
			for (const json& e : root["entities"])
				if (!e.is_object() || !isStageArchetype(e.value("archetype", "")))
					kept.push_back(e);
			notesOnly["entities"] = std::move(kept);

			std::vector<Raw> filtered;
			for (const json& e : notesOnly["entities"])
				filtered.push_back(readRaw(e));
			std::unordered_map<std::string, const Raw*> byName;
			for (const Raw& r : filtered)
				if (!r.name.empty())
					byName[r.name] = &r;

			MikuMikuWorld::LevelDataSerializer serializer(false);
			Score score = serializer.importLevelData(notesOnly);
			if (warnings)
				warnings->insert(warnings->end(), serializer.warnings.begin(), serializer.warnings.end());

			const std::vector<const Raw*> order = importerNoteOrder(filtered, byName);
			bool aligned = order.size() == score.notes.size();
			if (aligned)
			{
				size_t i = 0;
				for (const auto& [id, note] : score.notes)
				{
					if (std::abs(order[i++]->num("#BEAT") * TICKS_PER_BEAT - note.tick) > 0.51)
					{
						aligned = false;
						break;
					}
				}
			}
			if (!aligned)
			{
				problems.push_back("Could not match the imported notes to their stages; every note was put on the first stage.");
			}
			else if (dynamic)
			{
				size_t i = 0;
				int unknown = 0;
				for (const auto& [id, note] : score.notes)
				{
					const std::string stageRef = order[i++]->ref("stage");
					auto it = stageIds.find(stageRef);
					if (it == stageIds.end())
					{
						if (!stageRef.empty())
							++unknown;
						continue;
					}
					set.assign(id, it->second);
				}
				if (unknown)
					problems.push_back(std::to_string(unknown) + " note(s) pointed at a stage that is not in the file and went to the first stage.");
			}

			set.sort();
			if (warnings)
				warnings->insert(warnings->end(), problems.begin(), problems.end());
			outScore = std::move(score);
			outStages = std::move(set);
			return true;
		}
		catch (const std::exception& e)
		{
			error = e.what();
			return false;
		}
	}
}
