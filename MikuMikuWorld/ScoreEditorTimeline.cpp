#include "Canvas/Core/ChartTools.h"
#include "Canvas/Core/StageEdit.h"
#include "Application.h"
#include "ScoreEditorTimeline.h"
#include "Constants.h"
#include "UI.h"
#include "Colors.h"
#include "Canvas/UI/SekaiStyle.h"
#include "Canvas/Preview/LiveOverlay.h"
#include "Canvas/Sekai/SekaiAssets.h"
#include "ResourceManager.h"
#include "Tempo.h"
#include "Utilities.h"
#include "ApplicationConfiguration.h"
#include "Rendering/Camera.h"
#include "NoteSkin.h"
#include "Clipboard.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace MikuMikuWorld
{
	using json = nlohmann::json;

	static json hiSpeedSelectionToJson(const std::vector<HiSpeedChange>& hiSpeeds)
	{
		json data;
		data["hiSpeedChanges"] = json::array();
		if (hiSpeeds.empty())
			return data;

		const int baseTick = hiSpeeds.front().tick;
		for (const HiSpeedChange& hiSpeed : hiSpeeds)
			data["hiSpeedChanges"].push_back({ {"tick", hiSpeed.tick - baseTick}, {"speed", hiSpeed.speed} });
		return data;
	}

	static std::vector<HiSpeedChange> hiSpeedSelectionFromJson(std::string_view content)
	{
		std::vector<HiSpeedChange> hiSpeeds;
		if (content.empty())
			return hiSpeeds;

		const json data = json::parse(content, nullptr, false);
		if (data.is_discarded() || !jsonIO::arrayHasData(data, "hiSpeedChanges"))
			return hiSpeeds;

		for (const auto& entry : data["hiSpeedChanges"])
		{
			if (!entry.is_object())
				continue;

			const float speed = jsonIO::tryGetValue<float>(entry, "speed", 1.0f);
			if (!std::isfinite(speed))
				continue;

			hiSpeeds.push_back({ std::max(0, jsonIO::tryGetValue<int>(entry, "tick", 0)), speed });
		}

		return hiSpeeds;
	}

	static json seVolumeSelectionToJson(const std::vector<SEVolumeChange>& seVolumes)
	{
		json data;
		data["seVolumeChanges"] = json::array();
		if (seVolumes.empty())
			return data;

		const int baseTick = seVolumes.front().tick;
		for (const SEVolumeChange& seVolume : seVolumes)
			data["seVolumeChanges"].push_back({ {"tick", seVolume.tick - baseTick}, {"volume", seVolume.volume} });
		return data;
	}

	static std::vector<SEVolumeChange> seVolumeSelectionFromJson(std::string_view content)
	{
		std::vector<SEVolumeChange> seVolumes;
		if (content.empty())
			return seVolumes;

		const json data = json::parse(content, nullptr, false);
		if (data.is_discarded() || !jsonIO::arrayHasData(data, "seVolumeChanges"))
			return seVolumes;

		for (const auto& entry : data["seVolumeChanges"])
		{
			if (!entry.is_object())
				continue;

			const float volume = jsonIO::tryGetValue<float>(entry, "volume", 1.0f);
			if (!std::isfinite(volume))
				continue;

			seVolumes.push_back({ std::max(0, jsonIO::tryGetValue<int>(entry, "tick", 0)), volume });
		}

		return seVolumes;
	}

	static void drawEventControl(const EventControlDrawData& eventData)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (!drawList)
			return;
		const ImVec2 framePadding = { 1, 0 };
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, framePadding);
		const float frameHeight = ImGui::GetFrameHeightWithSpacing();
		ImGui::PopStyleVar();
		ImU32 col = eventData.color;
		ImU32 borderCol = eventData.color;

		if (!eventData.enabled)
		{
			col = (col & 0x00FFFFFF) | 0xAA << IM_COL32_A_SHIFT;
			borderCol = col;
		}
		else if (eventData.selected)
		{
			const ImVec4 baseCol = ImGui::ColorConvertU32ToFloat4(col);
			col = ImGui::ColorConvertFloat4ToU32(ImVec4(
				std::min(1.0f, baseCol.x + 0.22f),
				std::min(1.0f, baseCol.y + 0.22f),
				std::min(1.0f, baseCol.z + 0.22f),
				1.0f));
			borderCol = selectionColor2;
		}
		else if (eventData.highlight)
		{
			col = ImGui::ColorConvertFloat4ToU32(generateHighlightColor(ImGui::ColorConvertU32ToFloat4(col)));
			borderCol = col;
		}

		ImRect bound = { eventData.pos, eventData.pos + eventData.size };
		ImGui::RenderFrame(bound.Min, bound.Max, col, true, 2.0f);
		if (eventData.selected)
			drawList->AddRect(bound.Min, bound.Max, borderCol, 2.0f, ImDrawFlags_RoundCornersAll, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
		ImGui::RenderTextClipped(bound.Min + framePadding, bound.Max - framePadding, eventData.txt.c_str(), NULL, &eventData.txtSize, ImGui::GetStyle().ButtonTextAlign, &bound);
		ImGui::PopStyleColor();
		drawList->AddLine({ eventData.timelineX, eventData.pos.y + frameHeight }, { eventData.pos.x + (eventData.pos.x < eventData.timelineX ? 0 : eventData.size.x), eventData.pos.y + frameHeight }, borderCol, primaryLineThickness);
	}

	void ScoreEditorTimeline::drawFeverLine(const Fever& fever)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (!drawList)
			return;

		const float x = getTimelineStartX();
		const float y1 = position.y - tickToPosition(fever.chanceTick) + visualOffset;
		const float y2 = position.y - tickToPosition(fever.startTick) + visualOffset;

		drawList->AddLine({ x, y1 }, { x, y2 }, feverColor, primaryLineThickness);
	}

	bool ScoreEditorTimeline::eventControl(int tick, ImU32 color, const char* txt, bool fromStart, bool enabled, bool selected)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 1, 0 });
		const float frameHeight = ImGui::GetFrameHeightWithSpacing();
		ImGui::PopStyleVar();
		const float dpiScale = ImGui::GetMainViewport()->DpiScale;
		const ImVec2 txtSize = ImGui::CalcTextSize(txt);
		const ImVec2 itemSize = { std::max(txtSize.x + 5.0f, 30.0f), frameHeight };
		const float tickPos = tickToPosition(tick);
		float posX, posY = floorf(position.y - tickPos + visualOffset - frameHeight);
		float minCursor, maxCursor, timelineX;

		if (posY + itemSize.y < boundaries.Min.y || posY > boundaries.Max.y)
			return false;

		int tracks = std::floor(tickPos / 1.5f / frameHeight);
		auto it = eventControlCursor.find(tracks);
		if (it == eventControlCursor.end())
		{
			const float timelineMinX = getTimelineStartX(), timelineMaxX = getTimelineEndX();
			minCursor = getTimelineStartX() - 16;
			maxCursor = getTimelineEndX() + 16;
		}
		else
			std::tie(minCursor, maxCursor) = it->second;

		if (fromStart)
		{
			posX = floorf(minCursor - itemSize.x);
			minCursor = posX - eventControlPadding * dpiScale;
			timelineX = getTimelineStartX();
		}
		else
		{
			posX = floorf(maxCursor);
			maxCursor = posX + itemSize.x + eventControlPadding * dpiScale;
			timelineX = getTimelineEndX();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		ImGui::BeginDisabled(!enabled);
		ImGui::SetCursorScreenPos({posX, posY});
		ImGui::PushID(tick);
		ImGui::PushID(fromStart ? 1 : 0);
		ImGui::PushID(enabled ? 1 : 0);
		ImGui::PushID(txt);
		bool activated = ImGui::Button("##event_control", itemSize);
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);

		bool highlight = selected || activated || ImGui::IsItemHovered() || ImGui::IsItemActive();
		drawEvents.push({ timelineX, {posX, posY}, itemSize + ImVec2{1, 0}, txtSize, color, txt, highlight, selected, enabled });
		eventControlCursor[tracks] = std::make_pair(minCursor, maxCursor);

		return activated;
	}

	float ScoreEditorTimeline::getNoteYPosFromTick(int tick) const
	{
		return position.y + tickToPosition(tick) - visualOffset + size.y;
	}

	int ScoreEditorTimeline::positionToTick(float pos) const
	{
		return roundf(pos / (unitHeight * zoom));
	}

	float ScoreEditorTimeline::tickToPosition(int tick) const
	{
		return tick * unitHeight * zoom;
	}

	int ScoreEditorTimeline::positionToLane(float pos) const
	{
		return floor((pos - laneOffset) / laneWidth);
	}

	float ScoreEditorTimeline::laneToPosition(float lane) const
	{
		return laneOffset + (lane * laneWidth);
	}

	bool ScoreEditorTimeline::isNoteVisible(const Note& note, int offsetTicks) const
	{
		const float y = tickToPosition(note.tick + offsetTicks) - visualOffset + size.y;
		return y >= -100 && y <= size.y + 100;
	}

	void ScoreEditorTimeline::setZoom(float value)
	{
		int tick = positionToTick(offset - size.y);
		float x1 = position.y - tickToPosition(tick) + offset;

		zoom = std::clamp(value, minZoom, maxZoom);

		// Prevent jittery movement when zooming
		float x2 = position.y - tickToPosition(tick) + offset;
		visualOffset = offset = std::max(offset + x1 - x2, minOffset);
	}

	int ScoreEditorTimeline::snapTickFromPos(const Score& score, float posY) const
	{
		return Canvas::Tools::snapToDivision(positionToTick(posY), division, score);
	}

	int ScoreEditorTimeline::laneFromCenterPosition(int lane, int width) const
	{
		return std::clamp(lane - (width / 2), MIN_LANE, MAX_LANE - width + 1);
	}

	void ScoreEditorTimeline::focusCursor(int cursorTick, Direction direction)
	{
		float cursorY = tickToPosition(cursorTick);
		if (direction == Direction::Down)
		{
			float timelineOffset = size.y * (1.0f - config.cursorPositionThreshold);
			if (cursorY <= offset - timelineOffset)
				offset = cursorY + timelineOffset;
		}
		else
		{
			float timelineOffset = size.y * config.cursorPositionThreshold;
			if (cursorY >= offset - timelineOffset)
				offset = cursorY + timelineOffset;
		}
	}

	void ScoreEditorTimeline::jumpToTick(ScoreContext& context, int tick)
	{
		if (playing)
			setPlaying(context, false);

		lastSelectedTick = context.currentTick = std::max(0, tick);
		time = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		visualOffset = offset = std::max(minOffset, tickToPosition(context.currentTick) + size.y * 0.5f);
	}

	void ScoreEditorTimeline::updateScrollbar()
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (!drawList)
			return;

		float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;
		float paddingY = 30.0f;
		ImVec2 windowEndTop = ImGui::GetWindowPos() + ImVec2{ ImGui::GetWindowSize().x - scrollbarWidth - 4, paddingY };
		ImVec2 windowEndBottom = windowEndTop + ImVec2{ scrollbarWidth + 2, ImGui::GetWindowSize().y - (paddingY * 1.3f) - UI::footerHeight() - 5 };

		// calculate handle height
		float heightRatio = size.y / ((maxOffset - minOffset) * zoom);
		float handleHeight = std::max(20.0f, ((windowEndBottom.y - windowEndTop.y) * heightRatio) + 30.0f);
		float scrollHeight = windowEndBottom.y - windowEndTop.y - handleHeight;

		// calculate handle position
		float currentOffset = offset - minOffset;
		float positionRatio = std::min(1.0f, currentOffset / ((maxOffset * zoom) - minOffset));
		float handlePosition = windowEndBottom.y - (scrollHeight * positionRatio) - handleHeight;

		// make handle slightly narrower than the background
		ImVec2 scrollHandleMin = ImVec2{ windowEndTop.x + 2, handlePosition };
		ImVec2 scrollHandleMax = ImVec2{ windowEndTop.x + scrollbarWidth - 2, handlePosition + handleHeight };

		// handle "button"
		ImGuiCol handleColor = ImGuiCol_ScrollbarGrab;
		ImGui::SetCursorScreenPos(scrollHandleMin);
		ImGui::InvisibleButton("##scroll_handle", ImVec2{ scrollbarWidth, handleHeight });
		if (ImGui::IsItemHovered())
			handleColor = ImGuiCol_ScrollbarGrabHovered;

		if (ImGui::IsItemActivated())
			scrollStartY = ImGui::GetMousePos().y;

		if (ImGui::IsItemActive())
		{
			handleColor = ImGuiCol_ScrollbarGrabActive;
			float dragDeltaY = scrollStartY - ImGui::GetMousePos().y;
			if (abs(dragDeltaY) > 0)
			{
				// convert handle position to timeline offset
				handlePosition -= dragDeltaY;
				positionRatio = std::min(1.0f, 1 - ((handlePosition - windowEndTop.y) / scrollHeight));
				float newOffset = ((maxOffset * zoom) - minOffset) * positionRatio;

				offset = newOffset + minOffset;
				scrollStartY = ImGui::GetMousePos().y;
			}
		}

		if (!ImGui::IsItemActive() && positionRatio >= 0.92f)
			maxOffset += 2000;

		ImGui::SetCursorScreenPos(windowEndTop);
		ImGui::InvisibleButton("##scroll_background", ImVec2{ scrollbarWidth, scrollHeight + handleHeight }, ImGuiItemFlags_AllowOverlap);
		if (ImGui::IsItemActivated())
		{
			float yPos = std::clamp(ImGui::GetMousePos().y, windowEndTop.y, windowEndBottom.y - handleHeight);

			// convert handle position to timeline offset
			positionRatio = std::clamp(1 - ((yPos - windowEndTop.y)) / scrollHeight, 0.0f, 1.0f);
			float newOffset = ((maxOffset * zoom) - minOffset) * positionRatio;

			offset = newOffset + minOffset;
		}

		ImU32 scrollBgColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarBg));
		ImU32 scrollHandleColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(handleColor));
		drawList->AddRectFilled(windowEndTop, windowEndBottom, scrollBgColor, 0);
		drawList->AddRectFilled(scrollHandleMin, scrollHandleMax, scrollHandleColor, ImGui::GetStyle().ScrollbarRounding, ImDrawFlags_RoundCornersAll);
	}

	float ScoreEditorTimeline::overviewReservedWidth() const
	{
		return config.overviewEnabled ? static_cast<float>(config.overviewWidth) + 8.0f : ImGui::GetStyle().ScrollbarSize;
	}

	void ScoreEditorTimeline::updateOverview(ScoreContext& context)
	{
		const float reserved = overviewReservedWidth();
		const float x = config.overviewOnLeft ? position.x - reserved + 4.0f : position.x + size.x + 4.0f;
		const ImVec2 min{ x, position.y + 2.0f };
		const ImVec2 max{ x + static_cast<float>(config.overviewWidth), position.y + size.y - 2.0f };

		ChartOverview::View view;
		view.firstTick = std::max(0, positionToTick(visualOffset - size.y));
		view.lastTick = positionToTick(visualOffset);
		view.cursorTick = context.currentTick;

		const int target = overview.draw(context, overviewIssues, min, max, view);
		if (target >= 0)
		{
			visualOffset = offset = std::max(minOffset, tickToPosition(target) + size.y * 0.5f);
			maxOffset = std::max(maxOffset, offset);
		}
	}

	void ScoreEditorTimeline::calculateMaxOffsetFromScore(const Score& score)
	{
		int maxTick = 0;
		for (const auto& [id, note] : score.notes)
			maxTick = std::max(maxTick, note.tick);

		constexpr int extraOffset{ TICKS_PER_BEAT * 4 * 1 };
		// Current offset maybe greater than calculated offset from score
		maxOffset = std::max(offset, std::max(10000.0f, (maxTick + extraOffset) * unitHeight));
	}

	Color ScoreEditorTimeline::layerTint(const ScoreContext& context, int layer, const Color& base) const
	{
		if (!config.dimOtherLayers || context.score.layers.size() <= 1 || layer == context.currentLayer)
			return base;

		return Color{ base.r * otherLayerTint.r, base.g * otherLayerTint.g, base.b * otherLayerTint.b, base.a * otherLayerTint.a };
	}

	int ScoreEditorTimeline::getStopTick(const Score& score) const
	{
		int maxTick = 0;
		for (const auto& [id, note] : score.notes)
			maxTick = std::max(maxTick, note.tick);

		int maxMeasure = accumulateMeasures(maxTick, TICKS_PER_BEAT, score.timeSignatures) + 1;
		return measureToTicks(maxMeasure, TICKS_PER_BEAT, score.timeSignatures);
	}

	float ScoreEditorTimeline::getStopTime(const ScoreContext& context)
	{
		float end;
		if (context.score.metadata.endTick >= 0)
			end = accumulateDuration(context.score.metadata.endTick, TICKS_PER_BEAT, context.score.tempoChanges);
		else if (context.audio.isMusicInitialized())
			end = context.audio.getMusicEndTime();
		else
		{
			if (stopTime < 0.0f)
				stopTime = accumulateDuration(getStopTick(context.score), TICKS_PER_BEAT, context.score.tempoChanges);
			end = stopTime;
		}
		// the ALL PERFECT gets to finish, wherever the end is
		if (config.pvDrawAllPerfect && !context.score.notes.empty())
		{
			int last = 0;
			for (const auto& [id, note] : context.score.notes)
				last = std::max(last, note.tick);
			end = std::max(end, accumulateDuration(last, TICKS_PER_BEAT, context.score.tempoChanges) +
				static_cast<float>(Canvas::Preview::ALL_PERFECT_DELAY + Canvas::Preview::ALL_PERFECT_DURATION) + 0.5f);
		}
		return end;
	}

	void ScoreEditorTimeline::updateScrollingPosition()
	{
		if (config.useSmoothScrolling)
		{
			float scrollAmount = offset - visualOffset;
			float remainingScroll = abs(scrollAmount);
			float delta = scrollAmount / (config.smoothScrollingTime / (ImGui::GetIO().DeltaTime * 1000));

			visualOffset += std::min(remainingScroll, delta);
			remainingScroll = std::max(0.0f, remainingScroll - abs(delta));
		}
		else
		{
			visualOffset = offset;
		}
	}

	void ScoreEditorTimeline::contextMenu(ScoreContext& context)
	{
		if (ImGui::BeginPopupContextWindow(IMGUI_TITLE(ICON_FA_MUSIC, "notes_timeline")))
		{
			// where the menu was opened, for "Place End Here"
			static int menuTick = 0;
			if (ImGui::IsWindowAppearing())
			{
				syncNoteSpeedRatioInput(context);
				menuTick = hoverTick;
			}

			const bool hasHiSpeedSelection = this->hasSelectedHiSpeed(context);
			const bool hasSEVolumeSelection = this->hasSelectedSEVolume(context);
			const bool hasNoteSelection = !context.selectedNotes.empty();
			const bool hasAnySelection = hasHiSpeedSelection || hasSEVolumeSelection || hasNoteSelection;

			if (ImGui::MenuItem(getString("delete"), ToShortcutString(config.input.deleteSelection), false, hasAnySelection))
			{
				const bool deletedHiSpeed = deleteSelectedHiSpeed(context);
				const bool deletedSEVolume = deleteSelectedSEVolume(context);
				if (!deletedHiSpeed && !deletedSEVolume)
					context.deleteSelection();
			}

			// the chart's end (Canvas-only: exports never change, without one the end is worked out)
			ImGui::Separator();
			if (ImGui::MenuItem(getString("chart_end_place")))
			{
				Score prev = context.score;
				context.score.metadata.endTick = std::max(0, menuTick);
				context.pushHistory("Place chart end", prev, context.score);
			}
			if (context.score.metadata.endTick >= 0 && ImGui::MenuItem(getString("chart_end_remove")))
			{
				Score prev = context.score;
				context.score.metadata.endTick = -1;
				context.pushHistory("Remove chart end", prev, context.score);
			}

			ImGui::Separator();
			if (ImGui::MenuItem(getString("cut"), ToShortcutString(config.input.cutSelection), false, hasAnySelection))
			{
				if (!cutSelectedHiSpeed(context) && !cutSelectedSEVolume(context))
					context.cutSelection();
			}

			if (ImGui::MenuItem(getString("copy"), ToShortcutString(config.input.copySelection), false, hasAnySelection))
			{
				if (!copySelectedHiSpeed(context) && !copySelectedSEVolume(context))
					context.copySelection();
			}

			if (ImGui::MenuItem(getString("paste"), ToShortcutString(config.input.paste)))
			{
				if (!pasteHiSpeed(context, false) && !pasteSEVolume(context, false))
					context.paste(false);
			}

			if (ImGui::MenuItem(getString("flip_paste"), ToShortcutString(config.input.flipPaste)))
			{
				if (!pasteHiSpeed(context, true) && !pasteSEVolume(context, true))
					context.paste(true);
			}

			if (ImGui::MenuItem(getString("flip"), ToShortcutString(config.input.flip), false, !context.selectedNotes.empty()))
				context.flipSelection();

			ImGui::Separator();
			if (ImGui::BeginMenu(getString("ease_type"), context.selectionHasEase()))
			{
				for (int i = 0; i < arrayLength(easeTypes); ++i)
					if (ImGui::MenuItem(getString(easeTypes[i]))) context.setEase((EaseType)i);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("step_type"), context.selectionHasStep()))
			{
				for (int i = 0; i < arrayLength(stepTypes); ++i)
					if (ImGui::MenuItem(getString(stepTypes[i]))) context.setStep((HoldStepType)i);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("flick_type"), context.selectionHasFlickable()))
			{
				for (int i = 0; i < arrayLength(flickTypes); ++i)
					if (ImGui::MenuItem(getString(flickTypes[i]))) context.setFlick((FlickType)i);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("hold_type"), context.selectionCanChangeHoldType()))
			{
				/*
					* We won't show the option to change holds to guides and vice versa
					* at least for now since it will complicate changing hold types
				*/
				for (int i = 0; i < arrayLength(holdTypes) - 1; i++)
					if (ImGui::MenuItem(getString(holdTypes[i]))) context.setHoldType((HoldNoteType)i);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("guide_color"), context.selectionHasGuide()))
			{
				// Normal MMW: the two guide colours every MikuMikuWorld has
				const bool classic = config.chartMode == 1;
				for (int i = 0; i < (int)GuideColor::GuideColorCount; ++i)
				{
					if (classic && i != (int)GuideColor::Green && i != (int)GuideColor::Yellow)
						continue;
					ImGui::ColorButton(IO::concat("##guide_swatch_", guideColors[i]).c_str(), guideColorSwatch((GuideColor)i),
						ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
					ImGui::SameLine();
					if (ImGui::MenuItem(getString(guideColorNames[i])))
						context.setGuideColor((GuideColor)i);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("guide_fade"), context.selectionHasGuide()))
			{
				for (int i = 0; i < (int)FadeType::FadeTypeCount; ++i)
					if (ImGui::MenuItem(getString(fadeTypes[i])))
						context.setFadeType((FadeType)i);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("move_to_layer"), !context.selectedNotes.empty() && context.score.layers.size() > 1))
			{
				for (int i = 0; i < (int)context.score.layers.size(); ++i)
				{
					ImGui::PushID(i);
					if (ImGui::MenuItem(context.score.layers[i].name.c_str(), nullptr, i == context.currentLayer))
						context.moveSelectionToLayer(i);
					ImGui::PopID();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(getString("note_speed_ratio"), context.selectionHasEditableSpeedRatio()))
			{
				const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsScientific | ImGuiInputTextFlags_EnterReturnsTrue;
				ImGui::SetNextItemWidth(140.0f);
				const bool submitted = ImGui::InputText("##note_speed_ratio", noteSpeedRatioInput.data(), noteSpeedRatioInput.size(), flags);
				if ((submitted || ImGui::Button(getString("confirm"), ImVec2(-1, 0))) && applyNoteSpeedRatioInput(context))
				{
					syncNoteSpeedRatioInput(context);
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndMenu();
			}

			ImGui::Separator();
			if (ImGui::MenuItem(getString("shrink_up"), NULL, false, context.selectedNotes.size() > 1))
				context.shrinkSelection(Direction::Up);

			if (ImGui::MenuItem(getString("shrink_down"), NULL, false, context.selectedNotes.size() > 1))
				context.shrinkSelection(Direction::Down);

			ImGui::Separator();
			if (ImGui::MenuItem(getString("connect_holds"), NULL, false, context.selectionCanConnect()))
				context.connectHoldsInSelection();

			if (ImGui::MenuItem(getString("split_hold"), NULL, false, context.selectionHasAnyStep() && context.selectedNotes.size() == 1))
				context.splitHoldInSelection();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("repeat_hold_mids"), NULL, false, !context.selectedNotes.empty() && onRepeatHoldMids))
				onRepeatHoldMids();
			UI::tooltip(getString("repeat_hold_mids_tooltip"));
			if (ImGui::MenuItem(getString("interpolate_hispeed"), NULL, false, static_cast<bool>(onInterpolateHiSpeeds)))
				onInterpolateHiSpeeds();
			UI::tooltip(getString("interpolate_hispeed_tooltip"));

			ImGui::EndPopup();
		}
	}

	void ScoreEditorTimeline::update(ScoreContext& context, EditArgs& edit, Renderer* renderer)
	{
		prevSize = size;
		prevPos = position;

		// Make space for the scrollbar (or the overview strip) and the status bar
		const float reserved = overviewReservedWidth();
		size = ImGui::GetContentRegionAvail() - ImVec2{ reserved, UI::footerHeight() };
		position = ImGui::GetCursorScreenPos();
		if (config.overviewEnabled && config.overviewOnLeft)
			position.x += reserved;
		boundaries = ImRect(position, position + size);
		mouseInTimeline = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(position, position + size);

		laneOffset = (size.x * 0.5f) - ((NUM_LANES * laneWidth) * 0.5f);
		minOffset = size.y - 50;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(boundaries.Min, boundaries.Max, true);
		drawList->AddRectFilled(boundaries.Min, boundaries.Max, 0xff202020);

		if (background.isDirty())
		{
			background.resize({ size.x, size.y });
			background.process(renderer);
		}
		else if (prevSize.x != size.x || prevSize.y != size.y)
		{
			background.resize({ size.x, size.y });
		}

		bool isWindowActive = !ImGui::IsWindowDocked() || ImGui::GetCurrentWindow()->TabId == ImGui::GetWindowDockNode()->SelectedTabId;
		if (isWindowActive)
		{
			// a SEKAI-Stories folder with backgrounds takes over (SEKAI Mode shuffles them)
			const bool storyBackgrounds = config.sekaiMode && config.sekaiBackgrounds && !Canvas::Sekai::assets().backgrounds().empty();
			if (config.drawBackground && Canvas::SekaiStyle::active() && config.backgroundImage.empty() && !storyBackgrounds)
			{
				// the SEKAI interface: the game's SEKAI sky with its floating shapes
				Canvas::SekaiStyle::drawBackdrop(drawList, position, position + size, config.neoAnimatedBackdrop ? ImGui::GetTime() : 0.0,
					getTimelineStartX() - 30.0f, getTimelineEndX() + 30.0f);
				const float dim = 1.0f - config.backgroundBrightness;
				if (dim > 0.0f)
					drawList->AddRectFilled(position, position + size, IM_COL32(0, 0, 0, static_cast<int>(std::clamp(dim, 0.0f, 1.0f) * 200)));
			}
			else if (config.drawBackground && background.isLoaded())
			{
				const float bgWidth = static_cast<float>(background.getWidth());
				const float bgHeight = static_cast<float>(background.getHeight());
				ImVec2 bgPos{ position.x - (abs(bgWidth - size.x) / 2.0f), position.y - (abs(bgHeight - size.y) / 2.0f) };
				drawList->AddImage((ImTextureID)(size_t)background.getTextureID(), bgPos, bgPos + ImVec2{ bgWidth, bgHeight });
			}

			// Remember whether the last mouse click was in the timeline or not
			static bool clickedOnTimeline = false;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				clickedOnTimeline = mouseInTimeline;

			const bool pasting = context.pasteData.pasting;
			const ImGuiIO& io = ImGui::GetIO();

			// Get mouse position relative to timeline
			mousePos = io.MousePos - position;
			mousePos.y -= offset;
			hoverTick = snapTickFromPos(context.score, -mousePos.y);
			hoverLane = positionToLane(mousePos.x);

			if (mouseInTimeline && !UI::isAnyPopupOpen())
			{
				if (io.KeyCtrl)
				{
					setZoom(zoom + (io.MouseWheel * 0.1f));
				}
				else
				{
					float scrollAmount = io.MouseWheel * scrollUnit * static_cast<int>(!ImGui::IsMouseDown(ImGuiMouseButton_Middle));
					offset += scrollAmount * (io.KeyShift ? config.scrollSpeedShift : config.scrollSpeedNormal);
				}

				if (!isHoveringNote && !isHoldingNote && !insertingHold && !pasting && currentMode == TimelineMode::Select)
				{
					// Clicked inside timeline, the current mouse position is the first drag point
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						dragStart = mousePos;
						if (!io.KeyCtrl && !io.KeyAlt && !ImGui::IsPopupOpen(IMGUI_TITLE(ICON_FA_MUSIC, "notes_timeline")))
						{
							context.selectedNotes.clear();
							selectedHiSpeedTicks.clear();
							selectedSEVolumeTicks.clear();
						}
					}

					if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
					{
						ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
						offset += io.MouseDelta.y;
					}

					// Clicked and dragging inside the timeline
					if (clickedOnTimeline && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, 10.0f) && !playing)
						dragging = true;
				}

				if (ImGui::IsAnyPressed(config.input.cancelPaste) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					context.cancelPaste();
					insertingHold = insertingFever = false;
				}
			}

			offset = std::max(offset, minOffset);
			updateScrollingPosition();

			// Selection rectangle
			// Draw selection rectangle after notes are rendered
			if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !pasting)
			{
				float left = std::min(dragStart.x, mousePos.x);
				float right = std::max(dragStart.x, mousePos.x);

				int startTick = positionToTick(-std::max(dragStart.y, mousePos.y));
				int endTick = positionToTick(-std::min(dragStart.y, mousePos.y));

				if (!io.KeyAlt && !io.KeyCtrl)
				{
					context.selectedNotes.clear();
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
				}

				for (const auto& [id, note] : context.score.notes)
				{
					const int noteLayer = note.getType() == NoteType::Tap || note.getType() == NoteType::Hold ?
						note.layer : context.score.notes.at(note.parentID).layer;
					if (context.isLayerHidden(noteLayer))
						continue;

					float x1 = laneToPosition(note.lane);
					float x2 = laneToPosition(note.lane + note.width);

					if (right > x1 && left < x2 && isWithinRange(note.tick, startTick, endTick))
					{
						if (io.KeyAlt)
							context.selectedNotes.erase(id);
						else
							context.selectedNotes.insert(id);
					}
				}

				if (context.selectedNotes.empty())
				{
					for (const HiSpeedControlDrawData& hiSpeedRect : hiSpeedControlRects)
					{
						if (right > hiSpeedRect.minX && left < hiSpeedRect.maxX && isWithinRange(hiSpeedRect.tick, startTick, endTick))
						{
							if (io.KeyAlt)
								selectedHiSpeedTicks.erase(hiSpeedRect.tick);
							else
								selectedHiSpeedTicks.insert(hiSpeedRect.tick);
						}
					}

					for (const HiSpeedControlDrawData& seVolumeRect : seVolumeControlRects)
					{
						if (right > seVolumeRect.minX && left < seVolumeRect.maxX && isWithinRange(seVolumeRect.tick, startTick, endTick))
						{
							if (io.KeyAlt)
								selectedSEVolumeTicks.erase(seVolumeRect.tick);
							else
								selectedSEVolumeTicks.insert(seVolumeRect.tick);
						}
					}
				}
				else
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
				}

				dragging = false;
			}

			const float x1 = getTimelineStartX();
			const float x2 = getTimelineEndX();

			// Draw solid background color
			drawList->AddRectFilled(
				{ x1, position.y },
				{ x2, position.y + size.y },
				Canvas::SekaiStyle::active()
					// the game's 2D-mode lane window is a deep navy
					? Color::abgrToInt(std::clamp(static_cast<int>((0.55f + config.laneOpacity * 0.45f) * 255), 0, 255), 0x50, 0x2A, 0x2F)
					: Color::abgrToInt(std::clamp(static_cast<int>(config.laneOpacity * 255), 0, 255), 0x1E, 0x1E, 0x1E)
			);

			if (config.drawWaveform)
				drawWaveform(context);

			// Draw lanes
			for (int l = 0; l <= NUM_LANES; ++l)
			{
				const int x = position.x + laneToPosition(l);
				const bool boldLane = !(l & 1);
				drawList->AddLine(ImVec2(x, position.y), ImVec2(x, position.y + size.y), boldLane ? divColor1 : divColor2, boldLane ? primaryLineThickness : secondaryLineThickness);
			}

			// Draw measures
			int firstTick = std::max(0, positionToTick(visualOffset - size.y));
			int lastTick = positionToTick(visualOffset);
			int measure = accumulateMeasures(firstTick, TICKS_PER_BEAT, context.score.timeSignatures);
			firstTick = measureToTicks(measure, TICKS_PER_BEAT, context.score.timeSignatures);

			int tsIndex = findTimeSignature(measure, context.score.timeSignatures);
			int ticksPerMeasure = beatsPerMeasure(context.score.timeSignatures[tsIndex]) * TICKS_PER_BEAT;

			// The grid restarts at every measure, so any division (up to 1920, even ones that do not
			// divide a beat into whole ticks) lines up with the measure it is in. Lines that would sit
			// only a few pixels apart fade out instead of turning the lanes into a smear.
			const double gridStep = Canvas::Tools::divisionStep(division);
			const float stepPixels = (tickToPosition(TICKS_PER_BEAT) - tickToPosition(0)) * static_cast<float>(gridStep / TICKS_PER_BEAT);
			// too dense for the zoom: draw every few grid lines (still real grid positions)
			const long long stride = std::max<long long>(1, static_cast<long long>(std::ceil(6.0f / std::max(stepPixels, 0.01f))));
			const float fineAlpha = std::clamp((stepPixels * static_cast<float>(stride) - 4.0f) / 6.0f, 0.0f, 1.0f);
			const ImU32 fineColor = (divColor2 & 0x00FFFFFFu) | (static_cast<ImU32>(fineAlpha * ((divColor2 >> 24) & 0xFFu)) << 24);

			for (int m = measure, tick = firstTick; tick <= lastTick; ++m)
			{
				const int index = findTimeSignature(m, context.score.timeSignatures);
				const TimeSignature& ts = context.score.timeSignatures[index];
				const int measureTicks = beatsPerMeasure(ts) * TICKS_PER_BEAT;
				const int beatTicks = std::max(1, measureTicks / std::max(1, ts.numerator));

				if (fineAlpha > 0.0f)
				{
					for (long long k = stride;; k += stride)
					{
						const int offset = static_cast<int>(std::llround(static_cast<double>(k) * gridStep));
						if (offset >= measureTicks || tick + offset > lastTick)
							break;
						const float y = position.y - tickToPosition(tick + offset) + visualOffset;
						drawList->AddLine(ImVec2(x1, y), ImVec2(x2, y), fineColor, secondaryLineThickness);
					}
				}

				// beats are always drawn, whatever the division
				for (int offset = beatTicks; offset < measureTicks; offset += beatTicks)
				{
					if (tick + offset > lastTick)
						break;
					const float y = position.y - tickToPosition(tick + offset) + visualOffset;
					drawList->AddLine(ImVec2(x1, y), ImVec2(x2, y), divColor1, primaryLineThickness);
				}

				tick += measureTicks;
			}

			tsIndex = findTimeSignature(measure, context.score.timeSignatures);
			ticksPerMeasure = beatsPerMeasure(context.score.timeSignatures[tsIndex]) * TICKS_PER_BEAT;

			// Overdraw one measure to make sure the measure string is always visible
			for (int tick = firstTick; tick < lastTick + ticksPerMeasure; tick += ticksPerMeasure)
			{
				if (context.score.timeSignatures.find(measure) != context.score.timeSignatures.end())
				{
					tsIndex = measure;
					ticksPerMeasure = beatsPerMeasure(context.score.timeSignatures[tsIndex]) * TICKS_PER_BEAT;
				}

				std::string measureStr = std::to_string(measure);
				const float txtPos = x1 - MEASURE_WIDTH - (ImGui::CalcTextSize(measureStr.c_str()).x * 0.5f);
				const int y = position.y - tickToPosition(tick) + visualOffset;

				drawList->AddLine(ImVec2(x1 - MEASURE_WIDTH, y), ImVec2(x2 + MEASURE_WIDTH, y), measureColor, primaryLineThickness);
				drawShadedText(drawList, ImVec2(txtPos, y), 26, measureTxtColor, measureStr.c_str());

				++measure;
			}

			hoveringNote = -1;
			isHoveringNote = false;

			// Draw cursor behind notes
			constexpr int triPtOffset = 6;
			constexpr ImVec2 cursorButtonSize{ triPtOffset * 2.55f, triPtOffset * 2.55f };

			const float y = position.y - tickToPosition(context.currentTick) + visualOffset;
			const int triXPos = x1 - (triPtOffset * 2);
			drawList->AddTriangleFilled(ImVec2(triXPos, y - triPtOffset), ImVec2(triXPos, y + triPtOffset), ImVec2(triXPos + (triPtOffset * 2), y), cursorColor);
			drawList->AddLine(ImVec2(x1, y), ImVec2(x2, y), cursorColor, primaryLineThickness + 1.0f);

			ImGui::SetCursorScreenPos({ static_cast<float>(triXPos - 3), y - triPtOffset - 3 });
			ImGui::InvisibleButton("cursor", cursorButtonSize);
			if (ImGui::IsItemActive())
			{
				dragging = false;
				lastSelectedTick = context.currentTick = std::max(0, positionToTick(-mousePos.y));
			}

			contextMenu(context);

			eventControlCursor.clear();
			hiSpeedControlRects.clear();
			seVolumeControlRects.clear();

			// Update bpm changes
			for (int index = 0; index < context.score.tempoChanges.size(); ++index)
			{
				Tempo& tempo = context.score.tempoChanges[index];
				if (bpmControl(tempo))
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					eventEdit.editIndex = index;
					eventEdit.editBpm = tempo.bpm;
					eventEdit.type = EventType::Bpm;
					ImGui::OpenPopup("edit_event");
				}
			}

			// Update time signature changes
			for (auto& [measure, ts] : context.score.timeSignatures)
			{
				if (timeSignatureControl(ts.numerator, ts.denominator, measureToTicks(ts.measure, TICKS_PER_BEAT, context.score.timeSignatures), !playing))
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					eventEdit.editIndex = measure;
					eventEdit.editTimeSignatureNumerator = ts.numerator;
					eventEdit.editTimeSignatureDenominator = ts.denominator;
					eventEdit.type = EventType::TimeSignature;
					ImGui::OpenPopup("edit_event");
				}
			}

			// Update hi-speed changes (only the layer being edited)
			for (int index = 0; index < context.score.hiSpeedChanges.size(); ++index)
			{
				HiSpeedChange& hiSpeed = context.score.hiSpeedChanges[index];
				if (hiSpeed.layer != context.currentLayer)
					continue;

				if (hiSpeedControl(context, hiSpeed))
				{
					selectedSEVolumeTicks.clear();
					eventEdit.editIndex = index;
					eventEdit.editTick = hiSpeed.tick;
					eventEdit.setEditHispeed(hiSpeed.speed);
					eventEdit.type = EventType::HiSpeed;
					ImGui::OpenPopup("edit_event");
				}
			}

			// Update SE volume changes
			for (int index = 0; index < context.score.seVolumeChanges.size(); ++index)
			{
				SEVolumeChange& seVolume = context.score.seVolumeChanges[index];
				if (seVolumeControl(context, seVolume))
				{
					selectedHiSpeedTicks.clear();
					eventEdit.editIndex = index;
					eventEdit.editTick = seVolume.tick;
					eventEdit.setEditSEVolume(seVolume.volume);
					eventEdit.type = EventType::SEVolume;
					ImGui::OpenPopup("edit_event");
				}
			}

			// Update song boundaries
			if (context.audio.isMusicInitialized())
			{
				int startTick = accumulateTicks(context.workingData.musicOffset / 1000, TICKS_PER_BEAT, context.score.tempoChanges);
				int endTick = accumulateTicks(context.audio.getMusicEndTime(), TICKS_PER_BEAT, context.score.tempoChanges);

				float x = getTimelineEndX();
				float y1 = position.y - tickToPosition(startTick) + visualOffset;
				float y2 = position.y - tickToPosition(endTick) + visualOffset;

				drawList->AddTriangleFilled({ x, y1 }, { x + 10, y1 }, { x + 10, y1 - 10 }, 0xFFCCCCCC);
				drawList->AddTriangleFilled({ x, y2 }, { x + 10, y2 }, { x + 10, y2 + 10 }, 0xFFCCCCCC);
			}

			// the chart's end, once the charter placed one (right-click > Place End Here): drag it
			// anywhere while the button is held; double-click removes it
			{
				static Score beforeDrag;
				static bool draggingEnd = false;
				const int endTick = context.score.metadata.endTick;
				if (endTick >= 0)
				{
					eventControl(endTick, IM_COL32(0x8a, 0x8a, 0xa6, 255), "END " ICON_FA_FLAG_CHECKERED, false, !playing);
					if (ImGui::IsItemHovered())
						UI::tooltip(getString("chart_end_tooltip"));
					if (!playing && ImGui::IsItemClicked(ImGuiMouseButton_Left))
					{
						beforeDrag = context.score;
						draggingEnd = true;
					}
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						draggingEnd = false;
						Score prev = context.score;
						context.score.metadata.endTick = -1;
						context.pushHistory("Remove chart end", prev, context.score);
					}
				}
				if (draggingEnd)
				{
					if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
					{
						const float y = ImGui::GetIO().MousePos.y - position.y - visualOffset;
						context.score.metadata.endTick = std::max(0, snapTickFromPos(context.score, -y));
					}
					else
					{
						draggingEnd = false;
						if (beforeDrag.metadata.endTick != context.score.metadata.endTick)
							context.pushHistory("Move chart end", beforeDrag, context.score);
					}
				}
			}

			drawFeverLine(context.score.fever);
			if (feverControl(context.score.fever))
			{
				selectedHiSpeedTicks.clear();
				selectedSEVolumeTicks.clear();
				eventEdit.editIndex = -1;
				eventEdit.type = EventType::Fever;
				ImGui::OpenPopup("edit_event");
			}

			// Update skill triggers
			for (size_t index = 0; index < context.score.skills.size(); ++index)
			{
				if (skillControl(context.score.skills.at(index)))
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					eventEdit.editIndex = index;
					eventEdit.type = EventType::Skill;
					ImGui::OpenPopup("edit_event");
				}
			}

			eventEditor(context);
			updateNotes(context, edit, renderer);

			while (!drawEvents.empty())
			{
				drawEventControl(drawEvents.top());
				drawEvents.pop();
			}

			// Update cursor tick after determining whether a note is hovered
			// The cursor tick should not change if a note is hovered
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isHoveringNote && mouseInTimeline && !playing && !pasting &&
				!UI::isAnyPopupOpen() && currentMode == TimelineMode::Select && ImGui::IsWindowFocused())
			{
				if (!ImGui::IsAnyItemHovered())
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
				}
				context.currentTick = hoverTick;
				lastSelectedTick = context.currentTick;
			}

			// Selection boxes
			for (int id : context.selectedNotes)
			{
				const Note& note = context.score.notes.at(id);
				if (!isNoteVisible(note, 0))
					continue;

				float x = position.x;
				float y = position.y - tickToPosition(note.tick) + visualOffset;

				ImVec2 p1{ x + laneToPosition(note.lane) - 3, y - (notesHeight * 0.5f) };
				ImVec2 p2{ x + laneToPosition(note.lane + note.width) + 3, y + (notesHeight * 0.5f) };

				drawList->AddRectFilled(p1, p2, 0x20f4f4f4, 2.0f, ImDrawFlags_RoundCornersAll);
				drawList->AddRect(p1, p2, 0xcccccccc, 2.0f, ImDrawFlags_RoundCornersAll, 2.0f);
			}

			if (dragging && !pasting)
			{
				float startX = std::min(position.x + dragStart.x, position.x + mousePos.x);
				float endX = std::max(position.x + dragStart.x, position.x + mousePos.x);
				float startY = std::min(position.y + dragStart.y, position.y + mousePos.y) + visualOffset;
				float endY = std::max(position.y + dragStart.y, position.y + mousePos.y) + visualOffset;
				ImVec2 start{ startX, startY };
				ImVec2 end{ endX, endY };

				drawList->AddRectFilled(start, end, selectionColor1);
				drawList->AddRect(start, end, 0xbbcccccc, 0.2f, ImDrawFlags_RoundCornersAll, 1.0f);

				ImVec2 iconPos = ImVec2(position + dragStart);
				iconPos.y += visualOffset;
				if (io.KeyCtrl)
				{
					drawList->AddText(ImGui::GetFont(), 12, iconPos, 0xdddddddd, ICON_FA_PLUS_CIRCLE);
				}
				else if (io.KeyAlt)
				{
					drawList->AddText(ImGui::GetFont(), 12, iconPos, 0xdddddddd, ICON_FA_MINUS_CIRCLE);
				}
			}

			drawQueuedSpeedRatioLabels();
			drawList->PopClipRect();

			// Status bar: playback controls, division, zoom, current time and rhythm
			ImGui::SetCursorPos(ImVec2{ ImGui::GetStyle().WindowPadding.x, size.y + UI::footerHeight() + 4 + ImGui::GetStyle().WindowPadding.y });
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });

			if (UI::transparentButton(ICON_FA_BACKWARD, UI::btnSmall, true, context.currentTick > 0 && !playing))
				previousTick(context);

			ImGui::SameLine();
			if (UI::transparentButton(ICON_FA_STOP, UI::btnSmall, false))
				stop(context);

			ImGui::SameLine();
			if (UI::transparentButton(playing ? ICON_FA_PAUSE : ICON_FA_PLAY, UI::btnSmall))
				setPlaying(context, !playing);

			ImGui::SameLine();
			if (UI::transparentButton(ICON_FA_FORWARD, UI::btnSmall, true, !playing))
				nextTick(context);

			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();
			UI::divisionSelect(getString("division"), division, divisions, sizeof(divisions) / sizeof(int));

			static int gotoMeasure = 0;
			bool activated = false;

			ImGui::SameLine();
			ImGui::SetNextItemWidth(50);
			ImGui::InputInt("##goto_measure", &gotoMeasure, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
			activated |= ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
			UI::tooltip(getString("goto_measure"));

			ImGui::SameLine();
			activated |= UI::transparentButton(ICON_FA_ARROW_RIGHT, UI::btnSmall);

			if (activated)
			{
				gotoMeasure = std::clamp(gotoMeasure, 0, 999);
				context.currentTick = measureToTicks(gotoMeasure, TICKS_PER_BEAT, context.score.timeSignatures);
				offset = std::max(minOffset, tickToPosition(context.currentTick) + (size.y * (1.0f - config.cursorPositionThreshold)));
			}

			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();
			if (UI::transparentButton(ICON_FA_MINUS, UI::btnSmall, false, playbackSpeed > minPlaybackSpeed))
				setPlaybackSpeed(context, playbackSpeed - 0.25f);

			ImGui::SameLine();
			UI::transparentButton(IO::formatString("%.0f%%", playbackSpeed * 100).c_str(), ImVec2{ ImGui::CalcTextSize("0000%").x, UI::btnSmall.y }, false, false);

			ImGui::SameLine();
			if (UI::transparentButton(ICON_FA_PLUS, UI::btnSmall, false, playbackSpeed < maxPlaybackSpeed))
				setPlaybackSpeed(context, playbackSpeed + 0.25f);

			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();

			int currentMeasure = accumulateMeasures(context.currentTick, TICKS_PER_BEAT, context.score.timeSignatures);
			const TimeSignature& ts = context.score.timeSignatures[findTimeSignature(currentMeasure, context.score.timeSignatures)];
			const Tempo& tempo = getTempoAt(context.currentTick, context.score.tempoChanges);

			const std::vector<HiSpeedChange> layerHiSpeeds = hiSpeedsOfLayer(context.score, context.currentLayer);
			int hiSpeed = findHighSpeedChange(context.currentTick, layerHiSpeeds);
			float speed = (hiSpeed == -1 ? 1.0f : layerHiSpeeds[hiSpeed].speed);
			int seVolumeIndex = findSEVolumeChange(context.currentTick, context.score.seVolumeChanges);
			float seVolume = (seVolumeIndex == -1 ? 1.0f : context.score.seVolumeChanges[seVolumeIndex].volume);

			static char rhythmString[256];
			snprintf(rhythmString, 256, "  %02d:%02d:%02d  |  %d/%d  |  %g BPM  |  %sx  |  SE %s",
				(int)time / 60, (int)time % 60, (int)((time - (int)time) * 100),
				ts.numerator, ts.denominator,
				tempo.bpm,
				IO::formatFixedFloatTrimmed(speed).c_str(),
				IO::formatFixedFloatTrimmed(seVolume).c_str()
			);

			float _zoom = zoom;
			int controlWidth = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("99:59:59  |  64/64  |  999 BPM  |  10000x  |  SE 10000").x - (UI::btnSmall.x * 3);
			if (UI::zoomControl("zoom", _zoom, minZoom, sliderMaxZoom, std::clamp(controlWidth, 120, 320)))
				setZoom(_zoom);

			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();
			ImGui::Text(rhythmString);

			if (config.overviewEnabled)
				updateOverview(context);
			else
				updateScrollbar();
		}

		updateNoteSE(context);

		timeLastFrame = time;
		if (playing)
		{
			time += ImGui::GetIO().DeltaTime * playbackSpeed;
			const float stopTime = getStopTime(context);
			const bool playbackStartedPastEnd = playStartTime > stopTime;
			if (config.stopAtEnd && !playbackStartedPastEnd && time >= stopTime)
			{
				time = std::min(time, stopTime);
				bool prevPauseBehaviour = config.returnToLastSelectedTickOnPause;
				config.returnToLastSelectedTickOnPause = false;
				setPlaying(context, false);
				config.returnToLastSelectedTickOnPause = prevPauseBehaviour;
			}

			context.currentTick = accumulateTicks(time, TICKS_PER_BEAT, context.score.tempoChanges);

			float cursorY = tickToPosition(context.currentTick);
			if (config.followCursorInPlayback)
			{
				float timelineOffset = size.y * (1.0f - config.cursorPositionThreshold);
				if (cursorY >= offset - timelineOffset)
					visualOffset = offset = cursorY + timelineOffset;
			}
			else if (cursorY > offset)
			{
				visualOffset = offset = cursorY + size.y;	
			}
		}
		else
		{
			time = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		}
	}

	void ScoreEditorTimeline::updateNotes(ScoreContext& context, EditArgs& edit, Renderer* renderer)
	{
		// directxmath dies
		if (size.y < 10 || size.x < 10) return;
		drawSpeedRatioLabels.clear();

		Shader* shader = ResourceManager::shaders[0];
		shader->use();
		shader->setMatrix4("projection", Camera::getOffCenterOrthographicProjection(0, size.x, position.y + size.y, position.y));

		slidePathFramebuffer->bind();
		slidePathFramebuffer->clear(0, 0, 0, 0);

		glDisable(GL_DEPTH_TEST);

		std::vector<HoldNote> updateHolds;
		std::vector<int> updateNoteIDs;
		int startTick = positionToTick(visualOffset - size.y);
		int endTick = positionToTick(visualOffset);

		renderStats.clear();
		Stopwatch renderTimer{};
		renderTimer.reset();

		std::for_each(context.score.notes.begin(), context.score.notes.end(), [&](const std::pair<int, Note>& pair)
		{
			const Note& note = pair.second;
			if (note.getType() == NoteType::HoldEnd)
			{
				const Note& start = context.score.notes.at(note.parentID);
				if (context.isLayerHidden(start.layer))
					return;

				if (start.tick <= endTick && (note.tick >= startTick || isNoteVisible(note)))
					updateHolds.push_back(context.score.holdNotes.at(start.ID));
			}
			else if (note.getType() == NoteType::Tap && isNoteVisible(note) && !context.isLayerHidden(note.layer))
			{
				updateNoteIDs.push_back(note.ID);
			}
		});

		std::sort(updateHolds.begin(), updateHolds.end(), [&context](const HoldNote& a, const HoldNote& b) -> bool
		{
			const Note& a1 = context.score.notes.at(a.start.ID);
			const Note& a2 = context.score.notes.at(a.end);
			const Note& b1 = context.score.notes.at(b.start.ID);
			const Note& b2 = context.score.notes.at(b.end);
			if (a1.tick == b1.tick)
			{
				return a2.tick > b2.tick;
			}

			return a1.tick < b1.tick;
		});

		std::sort(updateNoteIDs.begin(), updateNoteIDs.end(), [&context](int a, int b)
		{
			const Note& n1 = context.score.notes.at(a);
			const Note& n2 = context.score.notes.at(b);
			if (n1.tick == n2.tick)
			{
				return n1.lane == n2.lane ? n1.ID < n2.ID : n1.lane < n2.lane;
			}

			return n1.tick < n2.tick;
		});

		renderer->beginBatch();
		for (const auto& hold : updateHolds)
		{
			drawHoldCurve(hold, context.score.notes, renderer, layerTint(context, context.score.notes.at(hold.start.ID).layer, noteTint));
		}
		renderer->endBatch();
		renderStats.addStats(renderer);

		ImGui::GetWindowDrawList()->AddImage((ImTextureID)(size_t)slidePathFramebuffer->getTexture(), position, position + size);

		notesFramebuffer->bind();
		notesFramebuffer->clear(0, 0, 0, 0);
		renderer->beginBatch();

		minNoteYDistance = INT_MAX;
		for (auto& hold : updateHolds)
		{
			Note& start = context.score.notes.at(hold.start.ID);
			Note& end = context.score.notes.at(hold.end);

			if (isNoteVisible(start)) updateNote(context, edit, start);
			if (isNoteVisible(end)) updateNote(context, edit, end);

			for (const auto& step : hold.steps)
			{
				Note& mid = context.score.notes.at(step.ID);
				if (isNoteVisible(mid)) updateNote(context, edit, mid);
			}

			drawHoldNote(context.score.notes, hold, renderer, layerTint(context, start.layer, noteTint));
		}

		for (const auto& data : drawSteps)
			drawOutline(data);

		drawSteps.clear();

		for (auto id : updateNoteIDs)
		{
			Note& note = context.score.notes.at(id);
			updateNote(context, edit, note);
			drawNote(note, renderer, layerTint(context, note.layer, noteTint));
		}

		renderer->endBatch();
		renderStats.addStats(renderer);

		const bool pasting = context.pasteData.pasting;
		if (pasting && mouseInTimeline && !playing)
		{
			context.pasteData.offsetLane = std::clamp(
				hoverLane - context.pasteData.midLane,
				context.pasteData.minLaneOffset,
				context.pasteData.maxLaneOffset
			);

			context.pasteData.offsetTicks = hoverTick + context.pasteData.minTick;
			previewPaste(context, renderer);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				context.confirmPaste();
		}

		if (mouseInTimeline && !isHoldingNote && (currentMode != TimelineMode::Select || insertingFever) &&
			!pasting && !playing && !UI::isAnyPopupOpen())
		{
			renderer->beginBatch();
			previewInput(edit, renderer);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverTick >= 0 && !isHoveringNote)
				executeInput(context, edit);

			if (insertingHold && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				insertHold(context, edit);
				insertingHold = false;
			}

			renderer->endBatch();
			renderStats.addStats(renderer);
		}
		else
		{
			insertingHold = false;
		}

		ImGui::GetWindowDrawList()->AddImage((ImTextureID)(size_t)notesFramebuffer->getTexture(), position, position + size);
		drawStageTints(context);
		renderStats.renderCpuTime = renderTimer.elapsed();

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDisable(GL_DEPTH_TEST);
	}

	void ScoreEditorTimeline::previewPaste(ScoreContext& context, Renderer* renderer)
	{
		std::vector<HoldNote> updateHolds;
		const int startTick = positionToTick(visualOffset - size.y);
		const int endTick = positionToTick(visualOffset);
		const int offsetTicks = context.pasteData.offsetTicks;
		std::for_each(context.pasteData.holds.begin(), context.pasteData.holds.end(), [&](const std::pair<int, HoldNote>& pair)
		{
			const HoldNote& hold = pair.second;
			const Note& start = context.pasteData.notes.at(hold.start.ID);
			const Note& end = context.pasteData.notes.at(hold.end);

			if (start.tick + offsetTicks <= endTick && end.tick + offsetTicks >= startTick)
				updateHolds.push_back(hold);
		});

		std::sort(updateHolds.begin(), updateHolds.end(), [&context](const HoldNote& a, const HoldNote& b)
		{
			return context.pasteData.notes.at(a.start.ID).tick < context.pasteData.notes.at(b.start.ID).tick;
		});

		renderer->beginBatch();
		for (const auto& hold : updateHolds)
		{
			drawHoldCurve(hold, context.pasteData.notes, renderer, hoverTint, context.pasteData.offsetTicks, context.pasteData.offsetLane);
		}
		renderer->endBatch();

		for (const auto& step : drawSteps)
			drawOutline(step);

		drawSteps.clear();

		renderer->beginBatch();
		for (const auto& hold : updateHolds)
			drawHoldNote(context.pasteData.notes, hold, renderer, hoverTint, context.pasteData.offsetTicks, context.pasteData.offsetLane);

		for (const auto& [_, note] : context.pasteData.notes)
		{
			if (note.getType() == NoteType::Tap && isNoteVisible(note, hoverTick))
				drawNote(note, renderer, hoverTint, context.pasteData.offsetTicks, context.pasteData.offsetLane);
		}
		renderer->endBatch();
	}

	void ScoreEditorTimeline::updateInputNotes(EditArgs& edit)
	{
		int lane = laneFromCenterPosition(hoverLane, edit.noteWidth);
		int width = edit.noteWidth;
		int tick = hoverTick;

		inputNotes.tap.lane = lane;
		inputNotes.tap.width = width;
		inputNotes.tap.tick = tick;
		inputNotes.tap.flick = currentMode == TimelineMode::InsertFlick ? edit.flickType : FlickType::None;
		inputNotes.tap.critical = currentMode == TimelineMode::MakeCritical;
		inputNotes.tap.friction = currentMode == TimelineMode::MakeFriction;

		inputNotes.holdStep.lane = lane;
		inputNotes.holdStep.width = width;
		inputNotes.holdStep.tick = tick;

		if (insertingHold)
		{
			inputNotes.holdEnd.lane = lane;
			inputNotes.holdEnd.width = width;
			inputNotes.holdEnd.tick = tick;
		}
		else
		{
			inputNotes.holdStart.lane = lane;
			inputNotes.holdStart.width = width;
			inputNotes.holdStart.tick = tick;
		}
	}

	void ScoreEditorTimeline::insertEvent(ScoreContext& context, EditArgs& edit)
	{
		if (currentMode == TimelineMode::InsertBPM)
		{
			for (const auto& tempo : context.score.tempoChanges)
				if (tempo.tick == hoverTick)
					return;

			Score prev = context.score;
			context.score.tempoChanges.push_back({ hoverTick, edit.bpm });
			std::sort(context.score.tempoChanges.begin(), context.score.tempoChanges.end(),
				[](const auto& a, const auto& b) { return a.tick < b.tick; });
			context.pushHistory("Insert BPM change", prev, context.score);
		}
		else if (currentMode == TimelineMode::InsertTimeSign)
		{
			int measure = accumulateMeasures(hoverTick, TICKS_PER_BEAT, context.score.timeSignatures);
			if (context.score.timeSignatures.find(measure) != context.score.timeSignatures.end())
				return;

			Score prev = context.score;
			context.score.timeSignatures[measure] = { measure, edit.timeSignatureNumerator, edit.timeSignatureDenominator };
			context.pushHistory("Insert time signature", prev, context.score);
		}
		else if (currentMode == TimelineMode::InsertHiSpeed)
		{
			for (const auto& hs : context.score.hiSpeedChanges)
				if (hs.tick == hoverTick && hs.layer == context.currentLayer)
					return;

			Score prev = context.score;
			context.score.hiSpeedChanges.push_back({ hoverTick, edit.hiSpeed, context.currentLayer });
			std::sort(context.score.hiSpeedChanges.begin(), context.score.hiSpeedChanges.end(),
				[](const auto& a, const auto& b) { return a.tick < b.tick; });
			context.pushHistory("Insert hi-speed change", prev, context.score);
		}
		else if (currentMode == TimelineMode::InsertSEVolume)
		{
			for (const auto& seVolume : context.score.seVolumeChanges)
				if (seVolume.tick == hoverTick)
					return;

			Score prev = context.score;
			context.score.seVolumeChanges.push_back({ hoverTick, edit.seVolume });
			std::sort(context.score.seVolumeChanges.begin(), context.score.seVolumeChanges.end(),
				[](const auto& a, const auto& b) { return a.tick < b.tick; });
			context.pushHistory("Insert SE volume change", prev, context.score);
		}
	}

	void ScoreEditorTimeline::insertSkill(ScoreContext& context, int tick)
	{
		if (playing)
			return;

		for (const auto& skill : context.score.skills)
		{
			if (skill.tick == tick)
				return;
		}

		Score prev = context.score;
		context.score.skills.push_back({ nextSkillID++, tick });
		context.pushHistory("Insert skill trigger", prev, context.score);
	}

	void ScoreEditorTimeline::beginInsertFever(ScoreContext& context, int tick)
	{
		if (playing)
			return;

		// Cancel paste to avoid inserting a fever and pasting simultaneously
		context.pasteData.pasting = false;

		insertingHold = isMovingNote = isHoldingNote = false;
		inputFever.chanceTick = tick;
		inputFever.startTick = -1;
		insertingFever = true;
	}

	void ScoreEditorTimeline::endInsertFever(ScoreContext& context, int tick)
	{
		inputFever.startTick = tick;
		if (inputFever.chanceTick > inputFever.startTick)
		{
			std::swap(inputFever.chanceTick, inputFever.startTick);
		}
		else if (inputFever.chanceTick == inputFever.startTick)
		{
			int currentMeasure = accumulateMeasures(tick, TICKS_PER_BEAT, context.score.timeSignatures);
			const TimeSignature& ts = context.score.timeSignatures.at(findTimeSignature(currentMeasure, context.score.timeSignatures));

			int ticksPerMeasure = beatsPerMeasure(ts) * TICKS_PER_BEAT;
			inputFever.startTick += ticksPerMeasure;
		}

		Score prev = context.score;
		context.score.fever = inputFever;
		context.pushHistory("Insert Fever", prev, context.score);

		insertingFever = false;
	}

	void ScoreEditorTimeline::previewInput(EditArgs& edit, Renderer* renderer)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
		if (insertingFever)
		{
			inputFever.startTick = hoverTick;
			feverControl(inputFever.chanceTick, true, false);
			feverControl(inputFever.startTick, true, false);
			drawFeverLine(inputFever);

			ImGui::PopStyleVar();
			return;
		}

		updateInputNotes(edit);
		switch (currentMode)
		{
		case TimelineMode::InsertLong:
			if (insertingHold)
			{
				drawHoldCurvePart(inputNotes.holdStart, inputNotes.holdEnd, edit.easeType, false, renderer, hoverTint);
				drawNote(inputNotes.holdStart, renderer, hoverTint);
				drawNote(inputNotes.holdEnd, renderer, hoverTint);
			}
			else
			{
				drawNote(inputNotes.holdStart, renderer, hoverTint);
			}
			break;

		case TimelineMode::InsertLongMid:
			drawHoldMid(inputNotes.holdStep, edit.stepType, renderer, hoverTint);
			drawOutline(StepDrawData(inputNotes.holdStep, edit.stepType == HoldStepType::Skip ? StepDrawType::SkipStep : StepDrawType::NormalStep));
			break;

		case TimelineMode::InsertGuide:
			if (insertingHold)
			{
				drawHoldCurvePart(inputNotes.holdStart, inputNotes.holdEnd, edit.easeType, true, renderer, hoverTint);
				drawOutline(StepDrawData(inputNotes.holdStart, StepDrawType::InvisibleHold));
				drawOutline(StepDrawData(inputNotes.holdEnd, StepDrawType::InvisibleHold));
			}
			else
			{
				drawOutline(StepDrawData(inputNotes.holdStart, StepDrawType::InvisibleHold));
			}
			break;

		case TimelineMode::InsertBPM:
			bpmControl(edit.bpm, hoverTick, false);
			break;

		case TimelineMode::InsertTimeSign:
			timeSignatureControl(edit.timeSignatureNumerator, edit.timeSignatureDenominator, hoverTick, false);
			break;

		case TimelineMode::InsertHiSpeed:
			eventControl(hoverTick, speedColor, IO::formatString("%sx", IO::formatFixedFloatTrimmed(edit.hiSpeed)).c_str(), false, false);
			break;
		case TimelineMode::InsertSEVolume:
			eventControl(hoverTick, seVolumeColor, IO::formatString("SE %s", IO::formatFixedFloatTrimmed(edit.seVolume).c_str()).c_str(), true, false);
			break;
		default:
			drawNote(inputNotes.tap, renderer, hoverTint);
			break;
		}

		ImGui::PopStyleVar();
	}

	void ScoreEditorTimeline::executeInput(ScoreContext& context, EditArgs& edit)
	{
		if (insertingFever)
		{
			endInsertFever(context, hoverTick);
			return;
		}

		switch (currentMode)
		{
		case TimelineMode::InsertTap:
		case TimelineMode::MakeCritical:
		case TimelineMode::MakeFriction:
		case TimelineMode::InsertFlick:
			insertNote(context, edit);
			break;

		case TimelineMode::InsertLong:
		case TimelineMode::InsertGuide:
			insertingHold = true;
			break;

		case TimelineMode::InsertLongMid:
		{
			int id = findClosestHold(context, hoverLane, hoverTick);
			if (id != -1)
				insertHoldStep(context, edit, id);
		}
		break;

		case TimelineMode::InsertBPM:
		case TimelineMode::InsertTimeSign:
		case TimelineMode::InsertHiSpeed:
		case TimelineMode::InsertSEVolume:
			insertEvent(context, edit);
			break;

		default:
			break;
		}
	}

	void ScoreEditorTimeline::changeMode(TimelineMode mode, EditArgs& edit)
	{
		if (currentMode == mode)
		{
			if (mode == TimelineMode::InsertLongMid)
			{
				edit.stepType = (HoldStepType)(((int)edit.stepType + 1) % (int)HoldStepType::HoldStepTypeCount);
			}
			else if (mode == TimelineMode::InsertFlick)
			{
				edit.flickType = (FlickType)(((int)edit.flickType + 1) % (int)FlickType::FlickTypeCount);
				if (!(int)edit.flickType)
					edit.flickType = FlickType::Default;
			}
		}

		currentMode = mode;
	}

	int ScoreEditorTimeline::findClosestHold(const ScoreContext& context, int lane, int tick)
	{
		float xt = laneToPosition(lane);
		float yt = getNoteYPosFromTick(tick);

		for (const auto& [id, hold] : context.score.holdNotes)
		{
			const Note& start = context.score.notes.at(hold.start.ID);
			const Note& end = context.score.notes.at(hold.end);

			// No need to search holds outside the cursor's reach
			if (start.tick > tick || end.tick < tick)
				continue;

			// Do not take skip steps into account
			int s1{ -1 }, s2{ -1 };
			while (++s2 < hold.steps.size())
			{
				if (hold.steps[s2].type != HoldStepType::Skip)
					break;
			}

			if (isArrayIndexInBounds(s2, hold.steps))
			{
				// Getting here means we found a non-skip step
				if (isMouseInHoldPath(start, context.score.notes.at(hold.steps[s2].ID), hold.start.ease, xt, yt))
					return id;

				s1 = s2;
				while (++s2 < hold.steps.size())
				{
					if (hold.steps[s2].type != HoldStepType::Skip)
					{
						const Note& m1 = context.score.notes.at(hold.steps[s1].ID);
						const Note& m2 = context.score.notes.at(hold.steps[s2].ID);
						if (isMouseInHoldPath(m1, m2, hold.steps[s1].ease, xt, yt))
							return id;

						s1 = s2;
					}
				}

				if (isMouseInHoldPath(context.score.notes.at(hold.steps[s1].ID), end, hold.steps[s1].ease, xt, yt))
					return id;
			}
			else
			{
				// Hold consists of only skip steps or no steps at all
				if (isMouseInHoldPath(start, end, hold.start.ease, xt, yt))
					return id;
			}
		}

		return -1;
	}

	bool ScoreEditorTimeline::noteControl(ScoreContext& context, const ImVec2& pos, const ImVec2& sz, const char* id, ImGuiMouseCursor cursor)
	{
		// Do not process notes if the cursor is outside of the timeline
		// This fixes ui buttons conflicting with note "buttons"
		if (!mouseInTimeline && !isHoldingNote)
			return false;

		// Do not allow editing notes during playback
		if (playing)
			return false;

		ImGui::SetCursorScreenPos(pos);
		ImGui::InvisibleButton(id, sz);
		if (mouseInTimeline && ImGui::IsItemHovered() && !dragging)
			ImGui::SetMouseCursor(cursor);

		// Note clicked
		if (ImGui::IsItemActivated())
		{
			prevUpdateScore = context.score;
			ctrlMousePos = mousePos;
			holdLane = hoverLane;
			holdTick = hoverTick;
		}

		// Holding note
		if (ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(cursor);
			isHoldingNote = true;
			return true;
		}

		// Note released
		if (ImGui::IsItemDeactivated())
		{
			bool noChange = false;
			auto it = context.score.notes.find(holdingNote);
			if (it != context.score.notes.end())
				noChange = noteTransformOrigin.isSame(it->second);

			isHoldingNote = false;
			holdingNote = 0;

			if (!noChange)
			{
				for (int holdId : context.getHoldsFromSelection())
					sortHold(context.score, context.score.holdNotes.at(holdId));

				context.pushHistory("Update notes", prevUpdateScore, context.score);
			}
		}

		return false;
	}

	void ScoreEditorTimeline::updateNote(ScoreContext& context, EditArgs& edit, Note& note)
	{
		const float btnPosY = position.y - tickToPosition(note.tick) + visualOffset - (notesHeight * 0.5f);
		float btnPosX = laneToPosition(note.lane) + position.x - 2.0f;

		ImVec2 pos{ btnPosX, btnPosY };
		ImVec2 noteSz{ laneToPosition(note.lane + note.width) + position.x + 2.0f - btnPosX, notesHeight };
		ImVec2 sz{ noteControlWidth, notesHeight };

		const ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsMouseHoveringRect(pos, pos + noteSz, false) && mouseInTimeline)
		{
			isHoveringNote = true;

			float noteYDistance = std::abs((btnPosY + notesHeight / 2 - visualOffset - position.y) - mousePos.y);
			if (noteYDistance < minNoteYDistance || io.KeyCtrl)
			{
				minNoteYDistance = noteYDistance;
				hoveringNote = note.ID;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !UI::isAnyPopupOpen())
				{
					if (!io.KeyCtrl && !io.KeyAlt && !context.isNoteSelected(note))
						context.selectedNotes.clear();

					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					context.selectedNotes.insert(note.ID);

					if (io.KeyAlt && context.isNoteSelected(note))
						context.selectedNotes.erase(note.ID);

					if (context.isNoteSelected(note))
					{
						holdingNote = note.ID;
						noteTransformOrigin = NoteTransform::fromNote(note);
					}
				}
			}
		}

		// Left resize
		ImGui::PushID(note.ID);
		if (noteControl(context, pos, sz, "L", ImGuiMouseCursor_ResizeEW))
		{
			int curLane = std::clamp(positionToLane(mousePos.x), MIN_LANE, MAX_LANE);
			int grabLane = std::clamp(positionToLane(ctrlMousePos.x), MIN_LANE, MAX_LANE);
			int diff = curLane - grabLane;

			if (abs(diff) > 0)
			{
				bool canResize = !std::any_of(context.selectedNotes.begin(), context.selectedNotes.end(), [&context, diff](int id)
				{
					Note& n = context.score.notes.at(id);
					int newLane = n.lane + diff;
					int newWidth = n.width - diff;
					return (newLane < MIN_LANE || newLane + newWidth - 1 > MAX_LANE || !isWithinRange(newWidth, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH));
				});

				if (canResize)
				{
					ctrlMousePos.x = mousePos.x;
					for (int id : context.selectedNotes)
					{
						Note& n = context.score.notes.at(id);
						n.width = std::clamp(n.width - diff, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
						n.lane = std::clamp(n.lane + diff, MIN_LANE, MAX_LANE - n.width + 1);
					}
				}
			}
		}

		pos.x += noteControlWidth;
		sz.x = (laneWidth * note.width) + 4.0f - (noteControlWidth * 2.0f);

		// Move
		if (noteControl(context, pos, sz, "M", ImGuiMouseCursor_ResizeAll))
		{
			int curLane = std::clamp(positionToLane(mousePos.x), MIN_LANE, MAX_LANE);
			int grabLane = std::clamp(positionToLane(ctrlMousePos.x), MIN_LANE, MAX_LANE);
			int grabTick = snapTickFromPos(context.score, -ctrlMousePos.y);

			int diff = curLane - grabLane;
			if (abs(diff) > 0)
			{
				isMovingNote = true;
				ctrlMousePos.x = mousePos.x;
				bool canMove = !std::any_of(context.selectedNotes.begin(), context.selectedNotes.end(), [&context, diff](int id)
				{
					Note& n = context.score.notes.at(id);
					int newLane = n.lane + diff;
					return (newLane < MIN_LANE || newLane + n.width - 1 > MAX_LANE);
				});

				if (canMove)
				{
					for (int id : context.selectedNotes)
					{
						Note& n = context.score.notes.at(id);
						n.lane = std::clamp(n.lane + diff, MIN_LANE, MAX_LANE - n.width + 1);
					}
				}
			}

			diff = hoverTick - grabTick;
			if (abs(diff) > 0)
			{
				isMovingNote = true;
				ctrlMousePos.y = mousePos.y;

				bool canMove = !std::any_of(context.selectedNotes.begin(), context.selectedNotes.end(),
					[&context, diff](int id) { return context.score.notes.at(id).tick + diff < 0; });

				if (canMove)
				{
					for (int id : context.selectedNotes)
					{
						Note& n = context.score.notes.at(id);
						n.tick = std::max(n.tick + diff, 0);
					}
				}
			}
		}

		// Per note options here
		if (ImGui::IsItemDeactivated())
		{
			if (!isMovingNote && !context.selectedNotes.empty())
			{
				switch (currentMode)
				{
				case TimelineMode::InsertFlick:
					context.setFlick(FlickType::FlickTypeCount);
					break;

				case TimelineMode::MakeCritical:
					context.toggleCriticals();
					break;

				case TimelineMode::InsertLong:
					context.setEase(EaseType::EaseTypeCount);
					break;

				case TimelineMode::InsertLongMid:
					context.setStep(HoldStepType::HoldStepTypeCount);
					break;

				case TimelineMode::MakeFriction:
					context.toggleFriction();
					break;

				default:
					break;
				}
			}

			isMovingNote = false;
		}

		pos.x += sz.x;
		sz.x = noteControlWidth;

		// Right resize
		if (noteControl(context, pos, sz, "R", ImGuiMouseCursor_ResizeEW))
		{
			int grabLane = std::clamp(positionToLane(ctrlMousePos.x), MIN_LANE, MAX_LANE);
			int curLane = std::clamp(positionToLane(mousePos.x), MIN_LANE, MAX_LANE);

			int diff = curLane - grabLane;
			if (abs(diff) > 0)
			{
				bool canResize = !std::any_of(context.selectedNotes.begin(), context.selectedNotes.end(), [&context, diff](int id)
				{
					Note& n = context.score.notes.at(id);
					int newWidth = n.width + diff;
					return (newWidth < MIN_NOTE_WIDTH || n.lane + newWidth - 1 > MAX_LANE);
				});

				if (canResize)
				{
					ctrlMousePos.x = mousePos.x;
					for (int id : context.selectedNotes)
					{
						Note& n = context.score.notes.at(id);
						n.width = std::clamp(n.width + diff, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH - n.lane);
					}
				}
			}
		}

		ImGui::PopID();
	}

	void ScoreEditorTimeline::drawHoldCurve(const HoldNote& hold, const std::map<int, Note>& notes, Renderer* renderer, const Color& tint, const int offsetTicks, const int offsetLane)
	{
		const Note& start = notes.at(hold.start.ID);
		const Note& end = notes.at(hold.end);

		GuideStyle guideStyle{ hold.guideColor, hold.fadeType, start.tick, end.tick };
		const GuideStyle* style = hold.isGuide() ? &guideStyle : nullptr;

		if (hold.steps.empty())
		{
			drawHoldCurvePart(start, end, hold.start.ease, hold.isGuide(), renderer, tint, offsetTicks, offsetLane, style);
			return;
		}

		int s1{ -1 }, s2{ -1 };
		for (int i = 0; i < hold.steps.size(); ++i)
		{
			if (hold.steps[i].type == HoldStepType::Skip)
				continue;

			s2 = i;
			const Note& n1 = s1 == -1 ? start : notes.at(hold.steps[s1].ID);
			const Note& n2 = s2 == -1 ? end : notes.at(hold.steps[s2].ID);
			const EaseType ease = s1 == -1 ? hold.start.ease : hold.steps[s1].ease;
			drawHoldCurvePart(n1, n2, ease, hold.isGuide(), renderer, tint, offsetTicks, offsetLane, style);

			s1 = s2;
		}

		const Note& n1 = s1 == -1 ? start : notes.at(hold.steps[s1].ID);
		const EaseType ease = s1 == -1 ? hold.start.ease : hold.steps[s1].ease;
		drawHoldCurvePart(n1, end, ease, hold.isGuide(), renderer, tint, offsetTicks, offsetLane, style);
	}

	void ScoreEditorTimeline::drawHoldCurvePart(const Note& n1, const Note& n2, EaseType ease, bool isGuide, Renderer* renderer, const Color& tint, const int offsetTick, const int offsetLane, const GuideStyle* guideStyle)
	{
		int texIndex{ noteSkins.getItemIndex(isGuide ? NoteSkinItem::TouchLine : NoteSkinItem::LongNote) };
		int sprIndex = n1.critical ? 3 : 1; // 0, 1 -> normal, 2, 3 -> critical

		if (isGuide && guideStyle)
		{
			// green / yellow come from the note skin, the other colours from our own colour sheet
			sprIndex = guideStyle->color == GuideColor::Yellow ? 3 : 1;
			static const int guideColorsTexture = ResourceManager::getTexture("guide_colors");
			if (isExtendedGuideColor(guideStyle->color) && guideColorsTexture != -1)
			{
				texIndex = guideColorsTexture;
				sprIndex = static_cast<int>(guideStyle->color);
			}
		}

		if (texIndex == -1)
			return;

		const Texture& pathTex = ResourceManager::textures[texIndex];
		if (!isArrayIndexInBounds(sprIndex, pathTex.sprites))
			return;

		const Sprite& spr = pathTex.sprites[sprIndex];

		float startX1 = laneToPosition(n1.lane + offsetLane);
		float startX2 = laneToPosition(n1.lane + n1.width + offsetLane);
		float startY = getNoteYPosFromTick(n1.tick + offsetTick);

		float endX1 = laneToPosition(n2.lane + offsetLane);
		float endX2 = laneToPosition(n2.lane + n2.width + offsetLane);
		float endY = getNoteYPosFromTick(n2.tick + offsetTick);

		int left = spr.getX1() + holdCutoffX;
		int right = spr.getX1() + spr.getWidth() - holdCutoffX;

		const float limitY2 = getNoteYPosFromTick(positionToTick(visualOffset - size.y));
		const float limitY1 = getNoteYPosFromTick(positionToTick(visualOffset));

		auto easeFunc = getEaseFunction(ease);
		float steps = ease == EaseType::Linear ? 1 : std::max(5.0f, std::ceil(abs((endY - startY)) / 10));

		const bool fades = guideStyle && guideStyle->fade != FadeType::None && guideStyle->endTick > guideStyle->startTick;
		if (fades)
			steps = std::max(steps, std::max(4.0f, std::ceil(abs(endY - startY) / 16)));

		const Color guideTint = isGuide ? tint.scaleAlpha(0.67f) : tint;
		Color appliedTint = guideTint;

		for (int y = 0; y < steps; ++y)
		{
			const float percent1 = y / steps;
			const float percent2 = (y + 1) / steps;

			if (fades)
			{
				// same shape as the Sonolus exporter: alpha goes 1 -> 0.2 along the guide
				const float midTick = n1.tick + (n2.tick - n1.tick) * ((percent1 + percent2) * 0.5f);
				const float progress = std::clamp((midTick - guideStyle->startTick) / float(guideStyle->endTick - guideStyle->startTick), 0.0f, 1.0f);
				const float alpha = guideStyle->fade == FadeType::Out ? 1.0f - 0.8f * progress : 0.2f + 0.8f * progress;
				appliedTint = guideTint.scaleAlpha(alpha);
			}

			float xl1 = easeFunc(startX1, endX1, percent1) - 2;
			float xr1 = easeFunc(startX2, endX2, percent1) + 2;
			float y1 = lerp(startY, endY, percent1);
			float y2 = lerp(startY, endY, percent2);
			float xl2 = easeFunc(startX1, endX1, percent2) - 2;
			float xr2 = easeFunc(startX2, endX2, percent2) + 2;

			if (y2 < limitY2)
			{
				// Below bottom boundary
				continue;
			}
			else if (y1 > limitY1)
			{
				// Above top boundary
				break;
			}

			Vector2 p1{ xl1, y1 };
			Vector2 p2{ xl1 + holdSliceSize, y1 };
			Vector2 p3{ xl2, y2 };
			Vector2 p4{ xl2 + holdSliceSize, y2 };
			renderer->drawQuad(p1, p2, p3, p4, pathTex, left, left + holdSliceWidth, spr.getY1(), spr.getY2(), appliedTint, (int)ZIndex::HoldLine);

			p1.x = xl1 + holdSliceSize;
			p2.x = xr1 - holdSliceSize;
			p3.x = xl2 + holdSliceSize;
			p4.x = xr2 - holdSliceSize;
			renderer->drawQuad(p1, p2, p3, p4, pathTex, left + holdSliceWidth, right - holdSliceWidth, spr.getY1(), spr.getY2(), appliedTint, (int)ZIndex::HoldLine);

			p1.x = xr1 - holdSliceSize;
			p2.x = xr1;
			p3.x = xr2 - holdSliceSize;
			p4.x = xr2;
			renderer->drawQuad(p1, p2, p3, p4, pathTex, right - holdSliceWidth, right, spr.getY1(), spr.getY2(), appliedTint, (int)ZIndex::HoldLine);
		}
	}

	void ScoreEditorTimeline::drawInputNote(Renderer* renderer)
	{
		if (insertingHold)
		{
			drawHoldCurvePart(inputNotes.holdStart, inputNotes.holdEnd, EaseType::Linear, false, renderer, noteTint);
			drawNote(inputNotes.holdStart, renderer, noteTint);
			drawNote(inputNotes.holdEnd, renderer, noteTint);
		}
		else
		{
			drawNote(currentMode == TimelineMode::InsertLong ? inputNotes.holdStart : inputNotes.tap, renderer, hoverTint);
		}
	}

	void ScoreEditorTimeline::drawHoldNote(const std::map<int, Note>& notes, const HoldNote& note, Renderer* renderer,
		const Color& tint, const int offsetTicks, const int offsetLane)
	{
		const Note& start = notes.at(note.start.ID);
		const Note& end = notes.at(note.end);
		const bool isDrawSteps = playing ? !config.hideStepOutlinesInPlayback : drawHoldStepOutlines;

		if (note.steps.size())
		{
			int s1 = -1;
			int s2 = 1;

			int texIndex = noteSkins.getItemIndex(NoteSkinItem::Notes);
			if (texIndex == -1)
				return;

			static constexpr auto isSkipStep = [](const HoldStep& step) { return step.type == HoldStepType::Skip; };

			const Texture& tex = ResourceManager::textures[texIndex];
			const Vector2 nodeSz{ notesHeight - 5, notesHeight - 5 };
			for (int i = 0; i < note.steps.size(); ++i)
			{
				const Note& n3 = notes.at(note.steps[i].ID);

				// find first non-skip step
				s2 = std::distance(note.steps.cbegin(), std::find_if(note.steps.cbegin() + std::max(s2, i), note.steps.cend(),
					std::not_fn(isSkipStep)));

				if (isNoteVisible(n3, offsetTicks))
				{
					if (isDrawSteps)
						drawSteps.emplace_back(StepDrawData
							{
								n3.tick + offsetTicks,
								n3.lane + offsetLane,
								n3.width,
								isSkipStep(note.steps[i]) ? StepDrawType::SkipStep : StepDrawType::NormalStep
							});

					if (note.steps[i].type != HoldStepType::Hidden)
					{
						int sprIndex = getNoteSpriteIndex(n3);
						if (sprIndex > -1 && sprIndex < tex.sprites.size())
						{
							const Sprite& s = tex.sprites[sprIndex];
							Vector2 pos{ laneToPosition(n3.lane + offsetLane + (n3.width / 2.0f)), getNoteYPosFromTick(n3.tick + offsetTicks) };

							if (isSkipStep(note.steps[i]))
							{
								// find the note before and after the skip step
								const Note& n1 = s1 == -1 ? start : notes.at(note.steps[s1].ID);
								const Note& n2 = s2 >= note.steps.size() ? end : notes.at(note.steps[s2].ID);

								// calculate the interpolation ratio based on the distance between n1 and n2
								float ratio = (float)(n3.tick - n1.tick) / (float)(n2.tick - n1.tick);
								const EaseType rEase = s1 == -1 ? note.start.ease : note.steps[s1].ease;

								auto easeFunc = getEaseFunction(rEase);

								// interpolate the step's position
								float x1 = easeFunc(laneToPosition(n1.lane + offsetLane), laneToPosition(n2.lane + offsetLane), ratio);
								float x2 = easeFunc(laneToPosition(n1.lane + offsetLane + n1.width), laneToPosition(n2.lane + offsetLane + n2.width), ratio);
								pos.x = midpoint(x1, x2);
							}

							renderer->drawSprite(pos, 0.0f, nodeSz, AnchorType::MiddleCenter, tex, s.getX1(), s.getX1() + s.getWidth(),
								s.getY1(), s.getY1() + s.getHeight(), tint, 1);
						}
					}
				}

				// update first interpolation point
				if (!isSkipStep(note.steps[i]))
					s1 = i;
			}
		}

		const ImU32 guideFill = note.isGuide() ? ImGui::ColorConvertFloat4ToU32(guideColorSwatch(note.guideColor)) : 0;

		if (note.startType == HoldNoteType::Normal)
		{
			drawNote(start, renderer, tint, offsetTicks, offsetLane);
		}
		else
		{
			if (isDrawSteps)
			{
				drawSteps.push_back({
					start.tick + offsetTicks,
					start.lane + offsetLane,
					start.width,
					start.critical ? StepDrawType::InvisibleHoldCritical : StepDrawType::InvisibleHold,
					guideFill
				});
			}

			drawSpeedRatioLabel(start, offsetTicks, offsetLane);
		}

		if (note.endType == HoldNoteType::Normal)
		{
			drawNote(end, renderer, tint, offsetTicks, offsetLane);
		}
		else if (isDrawSteps)
		{
			drawSteps.push_back({
				end.tick + offsetTicks,
				end.lane + offsetLane,
				end.width,
				start.critical ? StepDrawType::InvisibleHoldCritical : StepDrawType::InvisibleHold,
				guideFill
			});
		}
	}

	void ScoreEditorTimeline::drawHoldMid(Note& note, HoldStepType type, Renderer* renderer, const Color& tint)
	{
		int texIndex = noteSkins.getItemIndex(NoteSkinItem::Notes);
		if (type == HoldStepType::Hidden || texIndex == -1)
			return;

		const Texture& tex = ResourceManager::textures[texIndex];
		int sprIndex = getNoteSpriteIndex(note);
		if (sprIndex < 0 || sprIndex >= tex.sprites.size())
			return;

		const Sprite& s = tex.sprites[sprIndex];

		// Center diamond
		Vector2 pos{ laneToPosition(note.lane + (note.width / 2.0f)), getNoteYPosFromTick(note.tick) };
		Vector2 nodeSz{ notesHeight - 5, notesHeight - 5 };

		renderer->drawSprite(pos, 0.0f, nodeSz, AnchorType::MiddleCenter,
			tex, s.getX1(), s.getX1() + s.getWidth(), s.getY1(), s.getY1() + s.getHeight(), tint, 1);
	}

	void ScoreEditorTimeline::drawSpeedRatioLabel(const Note& note, const int offsetTick, const int offsetLane)
	{
		if ((note.getType() != NoteType::Tap && note.getType() != NoteType::Hold)
			|| std::abs(note.speedRatio - 1.0f) <= 0.0001f)
		{
			return;
		}

		const std::string text = IO::formatString("%sx", IO::formatFixedFloatTrimmed(note.speedRatio).c_str());
		const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
		const float x1 = position.x + laneToPosition(note.lane + offsetLane);
		const float x2 = position.x + laneToPosition(note.lane + note.width + offsetLane);
		const float labelGap = 0.5f;
		const float y = position.y - tickToPosition(note.tick + offsetTick) + visualOffset - (notesHeight * 0.35f) - labelGap;
		drawSpeedRatioLabels.push_back({
			{ midpoint(x1, x2) - (textSize.x * 0.5f), y - textSize.y },
			text
		});
	}

	void ScoreEditorTimeline::drawQueuedSpeedRatioLabels()
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (!drawList)
			return;

		for (const auto& label : drawSpeedRatioLabels)
			drawShadedText(drawList, label.pos, 16.0f, 0xFFF8F8F8, label.text.c_str());

		drawSpeedRatioLabels.clear();
	}

	// a thin coloured bar on the left of every note that sits on a non-main stage (only the assigned ones are walked)
	void ScoreEditorTimeline::drawStageTints(const ScoreContext& context)
	{
		const auto& stages = context.score.stages;
		if (!stages.enabled || stages.noteStage.empty())
			return;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (const auto& [id, stageId] : stages.noteStage)
		{
			auto it = context.score.notes.find(id);
			if (stageId == 0 || it == context.score.notes.end() || !isNoteVisible(it->second))
				continue;

			const Note& note = it->second;
			const float y = position.y - tickToPosition(note.tick) + visualOffset;
			const float x = position.x + laneToPosition(note.lane);
			const float half = notesHeight * 0.5f;
			drawList->AddRectFilled({ x - 4.0f, y - half }, { x - 1.0f, y + half }, Canvas::Stages::stageColor(stageId), 1.0f);
		}
	}

	void ScoreEditorTimeline::drawOutline(const StepDrawData& data)
	{
		float x = position.x;
		float y = position.y - tickToPosition(data.tick) + visualOffset;

		ImVec2 p1{ x + laneToPosition(data.lane), y - (notesHeight * 0.15f) };
		ImVec2 p2{ x + laneToPosition(data.lane + data.width), y + (notesHeight * 0.15f) };
		ImGui::GetWindowDrawList()->AddRectFilled(p1, p2, data.getFillColor(), 1.0f, ImDrawFlags_RoundCornersAll);
		ImGui::GetWindowDrawList()->AddRect(p1, p2, data.getOutlineColor(), 1, ImDrawFlags_RoundCornersAll, 2);
	}

	void ScoreEditorTimeline::drawFlickArrow(const Note& note, Renderer* renderer, const Color& tint, const int offsetTick, const int offsetLane)
	{
		int texIndex = noteSkins.getItemIndex(NoteSkinItem::Notes);
		if (texIndex == -1)
			return;

		const Texture& tex = ResourceManager::textures[texIndex];
		const int sprIndex = getFlickArrowSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, tex.sprites))
			return;

		const Sprite& arrowS = tex.sprites[sprIndex];

		Vector2 pos{ 0, getNoteYPosFromTick(note.tick + offsetTick) };
		const float x1 = laneToPosition(note.lane + offsetLane);
		const float x2 = pos.x + laneToPosition(note.lane + note.width + offsetLane);
		pos.x = midpoint(x1, x2);
		pos.y += notesHeight * 0.7f; // Move the arrow up a bit

		// Notes wider than 6 lanes also use flick arrow size 6
		int sizeIndex = std::min(note.width - 1, 5);
		Vector2 size{ laneWidth * flickArrowWidths[sizeIndex], notesHeight * flickArrowHeights[sizeIndex] };

		float sx1 = arrowS.getX1();
		float sx2 = arrowS.getX2();
		if (note.flick == FlickType::Right)
		{
			// Flip arrow to point to the right
			std::swap(sx1, sx2);
		}

		renderer->drawSprite(pos, 0.0f, size, AnchorType::MiddleCenter, tex,
			sx1, sx2, arrowS.getY1(), arrowS.getY2(), tint, (int)ZIndex::FlickArrow);
	}

	void ScoreEditorTimeline::drawNote(const Note& note, Renderer* renderer, const Color& tint, const int offsetTick, const int offsetLane)
	{
		int texIndex = noteSkins.getItemIndex(NoteSkinItem::Notes);
		if (texIndex == -1)
			return;

		const Texture& tex = ResourceManager::textures[texIndex];
		const int sprIndex = getNoteSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, tex.sprites))
			return;

		const Sprite& s = tex.sprites[sprIndex];

		Vector2 pos{ laneToPosition(note.lane + offsetLane), getNoteYPosFromTick(note.tick + offsetTick) };
		const Vector2 sliceSz(notesSliceSize, notesHeight);
		const AnchorType anchor = AnchorType::MiddleLeft;

		const float midLen = std::max(0.0f, (laneWidth * note.width) - (sliceSz.x * 2) + noteOffsetX + 5);
		const Vector2 midSz{ midLen, notesHeight };

		pos.x -= noteOffsetX;
		const int left = s.getX1() + noteCutoffX;
		const int right = s.getX2() - noteCutoffX;

		// Left slice
		renderer->drawSprite(pos, 0.0f, sliceSz, anchor, tex,
			left, left + noteSliceWidth,
			s.getY1(), s.getY2(), tint, (int)ZIndex::Note
		);
		pos.x += sliceSz.x;

		// Middle
		renderer->drawSprite(pos, 0.0f, midSz, anchor, tex,
			left + noteSliceWidth, left + noteSliceWidth + 1,
			s.getY1(), s.getY2(), tint, (int)ZIndex::Note
		);
		pos.x += midLen;

		// Right slice
		renderer->drawSprite(pos, 0.0f, sliceSz, anchor, tex,
			right - noteSliceWidth, right,
			s.getY1(), s.getY2(), tint, (int)ZIndex::Note
		);

		if (note.friction)
		{
			int frictionSprIndex = getFrictionSpriteIndex(note);
			if (isArrayIndexInBounds(frictionSprIndex, tex.sprites))
			{
				// Friction diamond is slightly smaller
				const Sprite& frictionSpr = tex.sprites[frictionSprIndex];
				const Vector2 nodeSz{ notesHeight * 0.8f, notesHeight * 0.8f };

				// Diamond is always centered
				pos.x = midpoint(laneToPosition(note.lane + offsetLane), laneToPosition(note.lane + offsetLane + note.width));
				renderer->drawSprite(pos, 0.0f, nodeSz, AnchorType::MiddleCenter, tex, frictionSpr.getX1(), frictionSpr.getX2(),
					frictionSpr.getY1(), frictionSpr.getY2(), tint, (int)ZIndex::Note);
			}
		}

		if (note.isFlick())
			drawFlickArrow(note, renderer, tint, offsetTick, offsetLane);

		drawSpeedRatioLabel(note, offsetTick, offsetLane);
	}

	void ScoreEditorTimeline::syncNoteSpeedRatioInput(const ScoreContext& context)
	{
		noteSpeedRatioInput.fill('\0');
		if (!context.selectionHasEditableSpeedRatio() || context.selectionHasMixedSpeedRatios())
			return;

		const std::unordered_set<int> targets = context.getSpeedRatioTargetsFromSelection();
		if (targets.empty())
			return;

		const std::string value = IO::formatFixedFloatTrimmed(context.score.notes.at(*targets.begin()).speedRatio);
		std::snprintf(noteSpeedRatioInput.data(), noteSpeedRatioInput.size(), "%s", value.c_str());
	}

	bool ScoreEditorTimeline::applyNoteSpeedRatioInput(ScoreContext& context)
	{
		char* end = nullptr;
		float speedRatio = std::strtof(noteSpeedRatioInput.data(), &end);
		if (end == noteSpeedRatioInput.data() || (end != nullptr && *end != '\0') || !std::isfinite(speedRatio) || speedRatio <= 0.0f)
			return false;

		context.setSpeedRatio(speedRatio);
		return true;
	}

	bool ScoreEditorTimeline::bpmControl(const Tempo& tempo)
	{
		return bpmControl(tempo.bpm, tempo.tick, !playing);
	}

	bool ScoreEditorTimeline::bpmControl(float bpm, int tick, bool enabled)
	{
		return eventControl(tick, tempoColor, IO::formatString("%g BPM", bpm).c_str(), false, enabled);
	}

	bool ScoreEditorTimeline::timeSignatureControl(int numerator, int denominator, int tick, bool enabled)
	{
		return eventControl(tick, timeColor, IO::formatString("%d/%d", numerator, denominator).c_str(), false, enabled);
	}

	bool ScoreEditorTimeline::skillControl(const SkillTrigger& skill)
	{
		return skillControl(skill.tick, !playing);
	}

	bool ScoreEditorTimeline::skillControl(int tick, bool enabled)
	{
		return eventControl(tick, skillColor, getString("skill"), true, enabled);
	}

	bool ScoreEditorTimeline::feverControl(const Fever& fever)
	{
		return feverControl(fever.chanceTick, true, !playing) || feverControl(fever.startTick, false, !playing);
	}

	bool ScoreEditorTimeline::feverControl(int tick, bool start, bool enabled)
	{
		if (tick < 0)
			return false;

		constexpr const char* feverStartText = "FEVER" ICON_FA_CARET_UP;
		constexpr const char* feverEndText = "FEVER" ICON_FA_CARET_DOWN;
		return eventControl(tick, feverColor, start ? feverStartText : feverEndText, true, enabled);
	}

	bool ScoreEditorTimeline::hiSpeedControl(ScoreContext& context, const HiSpeedChange& hiSpeed)
	{
		return hiSpeedControl(context, hiSpeed.tick, hiSpeed.speed, !playing);
	}

	bool ScoreEditorTimeline::hiSpeedControl(ScoreContext& context, int tick, float speed, bool enabled)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 1, 0 });
		const float frameHeight = ImGui::GetFrameHeightWithSpacing();
		ImGui::PopStyleVar();
		const float posY = floorf(position.y - tickToPosition(tick) + visualOffset - frameHeight);
		if (posY + frameHeight < boundaries.Min.y || posY > boundaries.Max.y)
			return false;

		std::string txt = IO::formatString("%sx", IO::formatFixedFloatTrimmed(speed));
		eventControl(tick, speedColor, txt.c_str(), false, enabled, selectedHiSpeedTicks.find(tick) != selectedHiSpeedTicks.end() && !playing);
		const ImVec2 rectMin = ImGui::GetItemRectMin();
		const ImVec2 rectMax = ImGui::GetItemRectMax();
		hiSpeedControlRects.push_back({ tick, rectMin.x - position.x, rectMax.x - position.x });

		if (enabled && ImGui::IsItemHovered())
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImGuiIO& io = ImGui::GetIO();
				if (!context.selectedNotes.empty())
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					return false;
				}

				if (!io.KeyCtrl && !io.KeyAlt)
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					selectedHiSpeedTicks.insert(tick);
				}
				else if (io.KeyAlt)
				{
					selectedHiSpeedTicks.erase(tick);
				}
				else
				{
					selectedHiSpeedTicks.insert(tick);
				}
			}

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				return true;
		}

		return false;
	}

	bool ScoreEditorTimeline::seVolumeControl(ScoreContext& context, const SEVolumeChange& seVolume)
	{
		return seVolumeControl(context, seVolume.tick, seVolume.volume, !playing);
	}

	bool ScoreEditorTimeline::seVolumeControl(ScoreContext& context, int tick, float volume, bool enabled)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 1, 0 });
		const float frameHeight = ImGui::GetFrameHeightWithSpacing();
		ImGui::PopStyleVar();
		const float posY = floorf(position.y - tickToPosition(tick) + visualOffset - frameHeight);
		if (posY + frameHeight < boundaries.Min.y || posY > boundaries.Max.y)
			return false;

		std::string txt = IO::formatString("SE %s", IO::formatFixedFloatTrimmed(volume).c_str());
		eventControl(tick, seVolumeColor, txt.c_str(), true, enabled, selectedSEVolumeTicks.find(tick) != selectedSEVolumeTicks.end() && !playing);
		const ImVec2 rectMin = ImGui::GetItemRectMin();
		const ImVec2 rectMax = ImGui::GetItemRectMax();
		seVolumeControlRects.push_back({ tick, rectMin.x - position.x, rectMax.x - position.x });

		if (enabled && ImGui::IsItemHovered())
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImGuiIO& io = ImGui::GetIO();
				if (!context.selectedNotes.empty())
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					return false;
				}

				if (!io.KeyCtrl && !io.KeyAlt)
				{
					selectedHiSpeedTicks.clear();
					selectedSEVolumeTicks.clear();
					selectedSEVolumeTicks.insert(tick);
				}
				else if (io.KeyAlt)
				{
					selectedSEVolumeTicks.erase(tick);
				}
				else
				{
					selectedSEVolumeTicks.insert(tick);
				}
			}

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				return true;
		}

		return false;
	}

	void ScoreEditorTimeline::eventEditor(ScoreContext& context)
	{
		ImGui::SetNextWindowSize(ImVec2(280, -1), ImGuiCond_Always);
		if (ImGui::BeginPopup("edit_event"))
		{
			std::string editLabel{"edit_"};
			editLabel.append(eventTypes[(int)eventEdit.type]);
			ImGui::Text(getString(editLabel));
			ImGui::Separator();
			const char* label;
			ImGuiStyle& style = ImGui::GetStyle();
			bool appearing = ImGui::IsWindowAppearing();

			if (eventEdit.type == EventType::Bpm)
			{
				if (!isArrayIndexInBounds(eventEdit.editIndex, context.score.tempoChanges))
				{
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}

				UI::beginPropertyColumns();

				Tempo& tempo = context.score.tempoChanges[eventEdit.editIndex];
				label = getString("bpm");
				if (appearing)
					ImGui::SetColumnWidth(0, ImGui::CalcTextSize(label).x + ImGui::GetCursorPosX() + style.ItemSpacing.x);
				UI::addFloatProperty(label, eventEdit.editBpm, "%g");
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Score prev = context.score;
					tempo.bpm = std::clamp(eventEdit.editBpm, MIN_BPM, MAX_BPM);
					eventEdit.editBpm = tempo.bpm;

					context.pushHistory("Change tempo", prev, context.score);
				}
				UI::endPropertyColumns();

				// cannot remove the first tempo change
				if (tempo.tick != 0)
				{
					ImGui::Separator();
					if (ImGui::Button(getString("remove"), ImVec2(-1, UI::btnSmall.y + 2)))
					{
						ImGui::CloseCurrentPopup();
						Score prev = context.score;
						context.score.tempoChanges.erase(context.score.tempoChanges.begin() + eventEdit.editIndex);
						context.pushHistory("Remove tempo change", prev, context.score);
					}
				}
			}
			else if (eventEdit.type == EventType::TimeSignature)
			{
				if (context.score.timeSignatures.find(eventEdit.editIndex) == context.score.timeSignatures.end())
				{
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}

				UI::beginPropertyColumns();
				label = getString("time_signature");
				if (appearing)
					ImGui::SetColumnWidth(0, ImGui::CalcTextSize(label).x + ImGui::GetCursorPosX() + style.ItemSpacing.x);
				if (UI::timeSignatureSelect(eventEdit.editTimeSignatureNumerator, eventEdit.editTimeSignatureDenominator))
				{
					Score prev = context.score;
					TimeSignature& ts = context.score.timeSignatures[eventEdit.editIndex];
					ts.numerator = std::clamp(abs(eventEdit.editTimeSignatureNumerator), MIN_TIME_SIGNATURE, MAX_TIME_SIGNATURE_NUMERATOR);
					ts.denominator = std::clamp(abs(eventEdit.editTimeSignatureDenominator), MIN_TIME_SIGNATURE, MAX_TIME_SIGNATURE_DENOMINATOR);
					eventEdit.editTimeSignatureNumerator = ts.numerator;
					eventEdit.editTimeSignatureDenominator = ts.denominator;

					context.pushHistory("Change time signature", prev, context.score);
				}
				UI::endPropertyColumns();

				// cannot remove the first time signature
				if (eventEdit.editIndex != 0)
				{
					ImGui::Separator();
					if (ImGui::Button(getString("remove"), ImVec2(-1, UI::btnSmall.y + 2)))
					{
						ImGui::CloseCurrentPopup();
						Score prev = context.score;
						context.score.timeSignatures.erase(eventEdit.editIndex);
						context.pushHistory("Remove time signature", prev, context.score);
					}
				}
			}
			else if (eventEdit.type == EventType::HiSpeed)
			{
				auto hiSpeedIt = std::find_if(context.score.hiSpeedChanges.begin(), context.score.hiSpeedChanges.end(),
					[this, &context](const HiSpeedChange& hiSpeed)
					{
						return hiSpeed.tick == eventEdit.editTick && hiSpeed.layer == context.currentLayer;
					});
				if (hiSpeedIt == context.score.hiSpeedChanges.end())
				{
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}

				UI::beginPropertyColumns();
				label = getString("hi_speed_speed");
				if (appearing)
					ImGui::SetColumnWidth(0, ImGui::CalcTextSize(label).x + ImGui::GetCursorPosX() + style.ItemSpacing.x);
				ImGui::PushID(eventEdit.editTick);
				UI::addNumericStringProperty(label, eventEdit.editHiSpeed);
				HiSpeedChange& hiSpeed = *hiSpeedIt;
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Score prev = context.score;
					try
					{
						hiSpeed.speed = std::stof(eventEdit.editHiSpeed);
					}
					catch (const std::out_of_range&)
					{
						hiSpeed.speed = 1;
					}
					catch (const std::invalid_argument&) { }
					eventEdit.setEditHispeed(hiSpeed.speed);

					context.pushHistory("Change hi-speed", prev, context.score);
				}
				ImGui::PopID();
				UI::endPropertyColumns();

				ImGui::Separator();
				if (ImGui::Button(getString("remove"), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					Score prev = context.score;
					selectedHiSpeedTicks.erase(hiSpeed.tick);
					context.score.hiSpeedChanges.erase(hiSpeedIt);
					context.pushHistory("Remove hi-speed change", prev, context.score);
				}
			}
			else if (eventEdit.type == EventType::SEVolume)
			{
				auto seVolumeIt = std::find_if(context.score.seVolumeChanges.begin(), context.score.seVolumeChanges.end(),
					[this](const SEVolumeChange& seVolume)
					{
						return seVolume.tick == eventEdit.editTick;
					});
				if (seVolumeIt == context.score.seVolumeChanges.end())
				{
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}

				UI::beginPropertyColumns();
				label = getString("se_volume_value");
				if (appearing)
					ImGui::SetColumnWidth(0, ImGui::CalcTextSize(label).x + ImGui::GetCursorPosX() + style.ItemSpacing.x);
				ImGui::PushID(eventEdit.editTick);
				UI::addNumericStringProperty(label, eventEdit.editSEVolume);
				SEVolumeChange& seVolume = *seVolumeIt;
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Score prev = context.score;
					try
					{
						seVolume.volume = std::stof(eventEdit.editSEVolume);
					}
					catch (const std::out_of_range&)
					{
						seVolume.volume = 1;
					}
					catch (const std::invalid_argument&) { }
					eventEdit.setEditSEVolume(seVolume.volume);

					context.pushHistory("Change SE volume", prev, context.score);
				}
				ImGui::PopID();
				UI::endPropertyColumns();

				ImGui::Separator();
				if (ImGui::Button(getString("remove"), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					Score prev = context.score;
					selectedSEVolumeTicks.erase(seVolume.tick);
					context.score.seVolumeChanges.erase(seVolumeIt);
					context.pushHistory("Remove SE volume change", prev, context.score);
				}
			}
			else if (eventEdit.type == EventType::Skill)
			{
				if (ImGui::Button(getString("remove"), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					Score prev = context.score;
					context.score.skills.erase(context.score.skills.begin() + eventEdit.editIndex);
					context.pushHistory("Remove skill trigger", prev, context.score);
				}
			}
			else if (eventEdit.type == EventType::Fever)
			{
				if (ImGui::Button(getString("remove"), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					Score prev = context.score;
					context.score.fever = { -1, -1 };
					context.pushHistory("Remove FEVER", prev, context.score);
				}
			}
			ImGui::EndPopup();
		}
	}

	bool ScoreEditorTimeline::isMouseInHoldPath(const Note& n1, const Note& n2, EaseType ease, float x, float y)
	{
		float xStart1 = laneToPosition(n1.lane);
		float xStart2 = laneToPosition(n1.lane + n1.width);
		float xEnd1 = laneToPosition(n2.lane);
		float xEnd2 = laneToPosition(n2.lane + n2.width);
		float y1 = getNoteYPosFromTick(n1.tick);
		float y2 = getNoteYPosFromTick(n2.tick);

		if (!isWithinRange(y, y1, y2))
			return false;

		auto easeFunc = getEaseFunction(ease);
		float percent = (y - y1) / (y2 - y1);
		float x1 = easeFunc(xStart1, xEnd1, percent);
		float x2 = easeFunc(xStart2, xEnd2, percent);

		return isWithinRange(x, std::min(x1, x2), std::max(x1, x2));
	}

	void ScoreEditorTimeline::previousTick(ScoreContext& context)
	{
		if (playing)
			return;

		context.currentTick = previousGridTick(context.score, context.currentTick);
		lastSelectedTick = context.currentTick;
		focusCursor(context.currentTick, Direction::Down);
	}

	void ScoreEditorTimeline::nextTick(ScoreContext& context)
	{
		if (playing)
			return;

		context.currentTick = nextGridTick(context.score, context.currentTick);
		lastSelectedTick = context.currentTick;
		focusCursor(context.currentTick, Direction::Up);
	}

	int ScoreEditorTimeline::nextGridTick(const Score& score, int tick) const
	{
		return Canvas::Tools::nextDivisionTick(tick, division, score);
	}

	int ScoreEditorTimeline::previousGridTick(const Score& score, int tick) const
	{
		return Canvas::Tools::previousDivisionTick(tick, division, score);
	}

	void ScoreEditorTimeline::insertNote(ScoreContext& context, EditArgs& edit)
	{
		Score prev = context.score;

		Note newNote = inputNotes.tap;
		newNote.ID = nextID++;
		newNote.layer = context.currentLayer;

		context.score.notes[newNote.ID] = newNote;
		Canvas::Stages::adoptNewNotes(context.score, { newNote.ID });
		context.pushHistory("Insert note", prev, context.score);
	}

	void ScoreEditorTimeline::insertHold(ScoreContext& context, EditArgs& edit)
	{
		Score prev = context.score;

		Note holdStart = inputNotes.holdStart;
		holdStart.ID = nextID++;

		Note holdEnd = inputNotes.holdEnd;
		holdEnd.ID = nextID++;
		holdEnd.parentID = holdStart.ID;

		if (holdStart.tick == holdEnd.tick)
		{
			holdEnd.tick = nextGridTick(context.score, holdStart.tick);
		}
		else if (holdStart.tick > holdEnd.tick)
		{
			std::swap(holdStart.tick, holdEnd.tick);
			std::swap(holdStart.width, holdEnd.width);
			std::swap(holdStart.lane, holdEnd.lane);
		}

		HoldNoteType holdType = currentMode == TimelineMode::InsertGuide ? HoldNoteType::Guide : HoldNoteType::Normal;
		holdStart.layer = holdEnd.layer = context.currentLayer;
		context.score.notes[holdStart.ID] = holdStart;
		context.score.notes[holdEnd.ID] = holdEnd;
		HoldNote& newHold = context.score.holdNotes[holdStart.ID];
		newHold = { {holdStart.ID, HoldStepType::Normal, edit.easeType}, {}, holdEnd.ID, holdType, holdType };
		if (holdType == HoldNoteType::Guide)
		{
			newHold.guideColor = edit.guideColor;
			newHold.fadeType = edit.fadeType;
			syncGuideCritical(context.score, newHold);
		}
		Canvas::Stages::adoptNewNotes(context.score, { holdStart.ID, holdEnd.ID });
		context.pushHistory("Insert hold", prev, context.score);
	}

	void ScoreEditorTimeline::insertHoldStep(ScoreContext& context, EditArgs& edit, int holdId)
	{
		// make sure the hold data exists
		if (context.score.holdNotes.find(holdId) == context.score.holdNotes.end())
			return;

		// make sure the parent hold note exists
		if (context.score.notes.find(holdId) == context.score.notes.end())
			return;

		Score prev = context.score;

		HoldNote& hold = context.score.holdNotes[holdId];
		Note holdStart = context.score.notes[holdId];

		Note holdStep = inputNotes.holdStep;
		holdStep.ID = nextID++;
		holdStep.critical = holdStart.critical;
		holdStep.parentID = holdStart.ID;
		holdStep.layer = holdStart.layer;

		context.score.notes[holdStep.ID] = holdStep;
		Canvas::Stages::adoptNewNotes(context.score, { holdStep.ID }, holdId);	// a step stays on its hold's stage

		hold.steps.push_back({ holdStep.ID, hold.isGuide() ? HoldStepType::Hidden : edit.stepType, edit.easeType});

		// sort steps in-case the step is inserted before/after existing steps
		sortHoldSteps(context.score, hold);
		context.pushHistory("Insert hold step", prev, context.score);
	}

	void ScoreEditorTimeline::debug(ScoreContext& context)
	{
		ImGui::Text("Window position: (%.2f, %.2f)\nWindow size: (%.2f, %.2f)", position.x, position.y, size.x, size.y);
		ImGui::Separator();

		ImGui::Text("Mouse position: (%.2f, %.2f)\nMouse in timeline: %s", mousePos.x, mousePos.y, boolToString(mouseInTimeline));
		ImGui::Separator();

		ImGui::Text("Minimum offset: %g\nCurrent offset: %g\nMaximum offset: %g", minOffset, offset, maxOffset);
		ImGui::Separator();
		
		if (mouseInTimeline)
		{
			ImGui::Text("Hover lane: %d\nHover tick: %d", hoverLane, hoverTick);
		}
		else
		{
			ImGui::TextDisabled("Hover lane: --\nHover tick: --");
		}

		ImGui::Text("Last selected tick : %d", lastSelectedTick);
		ImGui::Separator();

		if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Render Quads: %d", renderStats.getQuads());
			ImGui::Text("Render Vertices: %d", renderStats.getVerticies());
			ImGui::Text("Render Time: %.3fms", renderStats.getRenderCpuTime() * 1000.0f);
		}

		if (ImGui::CollapsingHeader("Hover Note", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Hovering note ID: %d", hoveringNote);
			ImGui::Text("Holding note ID: %d", holdingNote);

			auto it = context.score.notes.find(hoveringNote);
			if (it != context.score.notes.end())
			{
				const Note& note = it->second;
				ImGui::Text("ID: %d\nType: %d\nTick: %d\nLane: %d\nWidth: %d\nCritical: %s\nFriction: %s\nFlick: %s",
					note.ID,
					note.getType(),
					note.tick,
					note.lane,
					note.width,
					boolToString(note.critical),
					boolToString(note.friction),
					flickTypes[static_cast<int>(note.flick)]
				);
				ImGui::Text("Speed Ratio: %g", context.getEffectiveSpeedRatio(note));
			}
			else
			{
				// Prevent window scrolling from jumping around
				ImGui::TextDisabled("ID: -\nType: -\nTick: -\nLane: -\nWidth: -\nCritical: -\nFriction: -\nFlick: -\nSpeed Ratio: -");
			}
		}
	}

	ScoreEditorTimeline::ScoreEditorTimeline()
	{
		slidePathFramebuffer = std::make_unique<Framebuffer>(1920, 1080);
		notesFramebuffer = std::make_unique<Framebuffer>(1920, 1080);
		playbackSpeed = 1.0f;

		bool useDefaultBackground = config.backgroundImage.empty();
		if (!useDefaultBackground)
		{
			background.load(config.backgroundImage);
			useDefaultBackground = !background.isLoaded();
		}

		if (useDefaultBackground)
			background.load(Application::getAppDir() + "res/editor/default.png");
		
		background.setBrightness(0.67f);
	}

	void ScoreEditorTimeline::setPlaybackSpeed(ScoreContext& context, float speed)
	{
		playbackSpeed = std::clamp(speed, minPlaybackSpeed, maxPlaybackSpeed);
		context.audio.setPlaybackSpeed(playbackSpeed, time);

		// HACK: re-sync
		if (playing)
		{
			bool goBack = config.returnToLastSelectedTickOnPause;
			config.returnToLastSelectedTickOnPause = false;

			setPlaying(context, false);
			setPlaying(context, true);

			config.returnToLastSelectedTickOnPause = goBack;
		}
	}

	void ScoreEditorTimeline::setPlaying(ScoreContext& context, bool state)
	{
		if (playing == state)
			return;

		playing = state;
		if (playing)
		{
			dragging = false;
			playStartTime = time;
			context.audio.seekMusic(time);
			context.audio.playMusic(time);
			context.audio.setLastPlaybackTime(time);
			int seVolumeIndex = findSEVolumeChange(context.currentTick, context.score.seVolumeChanges);
			context.audio.setScoreSoundEffectsVolume(seVolumeIndex == -1 ? 1.0f : context.score.seVolumeChanges[seVolumeIndex].volume);
			context.audio.syncAudioEngineTimer();
		}
		else
		{
			context.audio.setScoreSoundEffectsVolume(1.0f);
			if (config.returnToLastSelectedTickOnPause)
			{
				context.currentTick = lastSelectedTick;
				offset = std::max(minOffset, tickToPosition(context.currentTick) + (size.y * (1.0f - config.cursorPositionThreshold)));
			}

			stopTime = -1;
			context.audio.stopSoundEffects(true);
			context.audio.stopMusic();
		}
	}

	void ScoreEditorTimeline::stop(ScoreContext& context)
	{
		playing = false;
		time = lastSelectedTick = context.currentTick = 0;
		offset = std::max(minOffset, tickToPosition(context.currentTick) + (size.y * (1.0f - config.cursorPositionThreshold)));

		context.audio.stopSoundEffects(false);
		context.audio.stopMusic();
		context.audio.setScoreSoundEffectsVolume(1.0f);
	}

	void ScoreEditorTimeline::updateNoteSE(ScoreContext& context)
	{
		if (!playing)
			return;

		int seVolumeIndex = findSEVolumeChange(context.currentTick, context.score.seVolumeChanges);
		const float currentSEVolume = seVolumeIndex == -1 ? 1.0f : context.score.seVolumeChanges[seVolumeIndex].volume;
		if (std::abs(context.audio.getScoreSoundEffectsVolume() - currentSEVolume) > 0.0001f)
			context.audio.setScoreSoundEffectsVolume(currentSEVolume);

		auto singleNoteSEFunc = [&context, this](const Note& note, float notePlayTime)
		{
			bool playSE = true;
			if (note.getType() == NoteType::Hold)
			{
				playSE = context.score.holdNotes.at(note.ID).startType == HoldNoteType::Normal;
			}
			else if (note.getType() == NoteType::HoldEnd)
			{
				playSE = context.score.holdNotes.at(note.parentID).endType == HoldNoteType::Normal;
			}

			if (playSE)
			{
				std::string_view se = getNoteSE(note, context.score);
				std::string key = std::to_string(note.tick) + "-" + se.data();
				if (!se.empty() && (playingNoteSounds.find(key) == playingNoteSounds.end()))
				{
					context.audio.playSoundEffect(se.data(), notePlayTime, -1, time);
					playingNoteSounds.insert(key);
				}
			}
		};

		auto holdNoteSEFunc = [&context, this](const Note& note, float startTime)
		{
			int endTick = context.score.notes.at(context.score.holdNotes.at(note.ID).end).tick;
			float endTime = accumulateDuration(endTick, TICKS_PER_BEAT, context.score.tempoChanges);

			float adjustedEndTime = endTime - playStartTime;
			context.audio.playSoundEffect(note.critical ? SE_CRITICAL_CONNECT : SE_CONNECT, startTime, adjustedEndTime, time);
		};

		playingNoteSounds.clear();
		for (const auto& [id, note] : context.score.notes)
		{
			float noteTime = accumulateDuration(note.tick, TICKS_PER_BEAT, context.score.tempoChanges);
			float notePlayTime = noteTime - playStartTime;
			float offsetNoteTime = noteTime - (audioLookAhead * playbackSpeed);

			if (offsetNoteTime >= timeLastFrame && offsetNoteTime < time)
			{
				singleNoteSEFunc(note, notePlayTime);
				if (note.getType() == NoteType::Hold && !context.score.holdNotes.at(note.ID).isGuide())
					holdNoteSEFunc(note, notePlayTime);
			}
			else if (time == playStartTime)
			{
				// Playback just started
				if (noteTime >= time && offsetNoteTime < time)
					singleNoteSEFunc(note, notePlayTime);

				// Playback started mid-hold
				if (note.getType() == NoteType::Hold && !context.score.holdNotes.at(note.ID).isGuide())
				{
					int endTick = context.score.notes.at(context.score.holdNotes.at(note.ID).end).tick;
					float endTime = accumulateDuration(endTick, TICKS_PER_BEAT, context.score.tempoChanges);
					if ((noteTime - time) <= audioLookAhead && endTime > time)
						holdNoteSEFunc(note, std::max(0.0f, notePlayTime));
				}
			}
		}
	}

	std::vector<int> ScoreEditorTimeline::getSelectedHiSpeedIndices(const ScoreContext& context) const
	{
		std::vector<int> indices;
		if (!context.selectedNotes.empty() || selectedHiSpeedTicks.empty())
			return indices;

		for (int index = 0; index < context.score.hiSpeedChanges.size(); ++index)
			if (context.score.hiSpeedChanges[index].layer == context.currentLayer &&
				selectedHiSpeedTicks.find(context.score.hiSpeedChanges[index].tick) != selectedHiSpeedTicks.end())
				indices.push_back(index);

		return indices;
	}

	bool ScoreEditorTimeline::hasSelectedHiSpeed(const ScoreContext& context) const
	{
		return !getSelectedHiSpeedIndices(context).empty();
	}

	std::vector<int> ScoreEditorTimeline::getSelectedSEVolumeIndices(const ScoreContext& context) const
	{
		std::vector<int> indices;
		if (!context.selectedNotes.empty() || selectedSEVolumeTicks.empty())
			return indices;

		for (int index = 0; index < context.score.seVolumeChanges.size(); ++index)
			if (selectedSEVolumeTicks.find(context.score.seVolumeChanges[index].tick) != selectedSEVolumeTicks.end())
				indices.push_back(index);

		return indices;
	}

	bool ScoreEditorTimeline::hasSelectedSEVolume(const ScoreContext& context) const
	{
		return !getSelectedSEVolumeIndices(context).empty();
	}

	bool ScoreEditorTimeline::copySelectedHiSpeed(const ScoreContext& context)
	{
		const std::vector<int> indices = getSelectedHiSpeedIndices(context);
		if (indices.empty())
			return false;

		std::vector<HiSpeedChange> hiSpeeds;
		hiSpeeds.reserve(indices.size());
		for (int index : indices)
			hiSpeeds.push_back(context.score.hiSpeedChanges[index]);

		Clipboard::store(hiSpeedSelectionToJson(hiSpeeds).dump());
		return true;
	}

	bool ScoreEditorTimeline::copySelectedSEVolume(const ScoreContext& context)
	{
		const std::vector<int> indices = getSelectedSEVolumeIndices(context);
		if (indices.empty())
			return false;

		std::vector<SEVolumeChange> seVolumes;
		seVolumes.reserve(indices.size());
		for (int index : indices)
			seVolumes.push_back(context.score.seVolumeChanges[index]);

		Clipboard::store(seVolumeSelectionToJson(seVolumes).dump());
		return true;
	}

	bool ScoreEditorTimeline::deleteSelectedHiSpeed(ScoreContext& context)
	{
		const std::vector<int> indices = getSelectedHiSpeedIndices(context);
		if (indices.empty())
			return false;

		Score prev = context.score;
		context.score.hiSpeedChanges.erase(
			std::remove_if(context.score.hiSpeedChanges.begin(), context.score.hiSpeedChanges.end(),
				[this, &context](const HiSpeedChange& hiSpeed)
				{
					return hiSpeed.layer == context.currentLayer && selectedHiSpeedTicks.find(hiSpeed.tick) != selectedHiSpeedTicks.end();
				}),
			context.score.hiSpeedChanges.end());
		selectedHiSpeedTicks.clear();
		context.pushHistory("Remove hi-speed change", prev, context.score);
		return true;
	}

	bool ScoreEditorTimeline::deleteSelectedSEVolume(ScoreContext& context)
	{
		const std::vector<int> indices = getSelectedSEVolumeIndices(context);
		if (indices.empty())
			return false;

		Score prev = context.score;
		context.score.seVolumeChanges.erase(
			std::remove_if(context.score.seVolumeChanges.begin(), context.score.seVolumeChanges.end(),
				[this](const SEVolumeChange& seVolume)
				{
					return selectedSEVolumeTicks.find(seVolume.tick) != selectedSEVolumeTicks.end();
				}),
			context.score.seVolumeChanges.end());
		selectedSEVolumeTicks.clear();
		context.pushHistory("Remove SE volume change", prev, context.score);
		return true;
	}

	bool ScoreEditorTimeline::cutSelectedHiSpeed(ScoreContext& context)
	{
		if (!copySelectedHiSpeed(context))
			return false;

		return deleteSelectedHiSpeed(context);
	}

	bool ScoreEditorTimeline::cutSelectedSEVolume(ScoreContext& context)
	{
		if (!copySelectedSEVolume(context))
			return false;

		return deleteSelectedSEVolume(context);
	}

	bool ScoreEditorTimeline::pasteHiSpeed(ScoreContext& context, bool flip)
	{
		(void)flip;

		const std::vector<HiSpeedChange> hiSpeeds = hiSpeedSelectionFromJson(Clipboard::get());
		if (hiSpeeds.empty())
			return false;

		Score prev = context.score;
		bool changed = false;
		std::unordered_set<int> pastedTicks;

		for (const HiSpeedChange& hiSpeed : hiSpeeds)
		{
			const int targetTick = std::max(0, context.currentTick + hiSpeed.tick);
			auto it = std::find_if(context.score.hiSpeedChanges.begin(), context.score.hiSpeedChanges.end(),
				[targetTick, &context](const HiSpeedChange& current) { return current.tick == targetTick && current.layer == context.currentLayer; });

			if (it == context.score.hiSpeedChanges.end())
			{
				context.score.hiSpeedChanges.push_back({ targetTick, hiSpeed.speed, context.currentLayer });
				changed = true;
			}
			else if (std::abs(it->speed - hiSpeed.speed) > 0.0001f)
			{
				it->speed = hiSpeed.speed;
				changed = true;
			}

			pastedTicks.insert(targetTick);
		}

		context.selectedNotes.clear();
		selectedHiSpeedTicks = pastedTicks;
		selectedSEVolumeTicks.clear();

		if (changed)
		{
			std::sort(context.score.hiSpeedChanges.begin(), context.score.hiSpeedChanges.end(),
				[](const HiSpeedChange& left, const HiSpeedChange& right) { return left.tick < right.tick; });
			context.pushHistory("Paste hi-speed change", prev, context.score);
		}

		return true;
	}

	bool ScoreEditorTimeline::pasteSEVolume(ScoreContext& context, bool flip)
	{
		(void)flip;

		const std::vector<SEVolumeChange> seVolumes = seVolumeSelectionFromJson(Clipboard::get());
		if (seVolumes.empty())
			return false;

		Score prev = context.score;
		bool changed = false;
		std::unordered_set<int> pastedTicks;

		for (const SEVolumeChange& seVolume : seVolumes)
		{
			const int targetTick = std::max(0, context.currentTick + seVolume.tick);
			auto it = std::find_if(context.score.seVolumeChanges.begin(), context.score.seVolumeChanges.end(),
				[targetTick](const SEVolumeChange& current) { return current.tick == targetTick; });

			if (it == context.score.seVolumeChanges.end())
			{
				context.score.seVolumeChanges.push_back({ targetTick, seVolume.volume });
				changed = true;
			}
			else if (std::abs(it->volume - seVolume.volume) > 0.0001f)
			{
				it->volume = seVolume.volume;
				changed = true;
			}

			pastedTicks.insert(targetTick);
		}

		context.selectedNotes.clear();
		selectedHiSpeedTicks.clear();
		selectedSEVolumeTicks = pastedTicks;

		if (changed)
		{
			std::sort(context.score.seVolumeChanges.begin(), context.score.seVolumeChanges.end(),
				[](const SEVolumeChange& left, const SEVolumeChange& right) { return left.tick < right.tick; });
			context.pushHistory("Paste SE volume change", prev, context.score);
		}

		return true;
	}

	void ScoreEditorTimeline::drawWaveform(const ScoreContext& context)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (!drawList)
			return;

		constexpr ImU32 waveformColorL = 0x80646464;
		constexpr ImU32 waveformColorR = 0x80585858;

		// Ideally this should be calculated based on the current BPM
		const double secondsPerPixel = waveformSecondsPerPixel / zoom;
		const double durationSeconds = context.waveformL.durationInSeconds;
		const double musicOffsetInSeconds = context.workingData.musicOffset / 1000.0f;

		const float timelineMidPosition = midpoint(getTimelineStartX(), getTimelineEndX());

		for (size_t index = 0; index < 2; index++)
		{
			const bool rightChannel = index == 1;
			const Audio::WaveformMipChain& waveform = rightChannel ? context.waveformR : context.waveformL;
			if (waveform.isEmpty() || !waveform.isDoneProcessing())
				continue;

			const ImU32 waveformColor = rightChannel ? waveformColorR : waveformColorL;
			const Audio::WaveformMip& mip = waveform.findClosestMip(secondsPerPixel);

			for (int y = visualOffset - size.y; y < visualOffset; y += 1)
			{
				int tick = positionToTick(y);

				// Small accuracy loss by converting to ticks but shouldn't be too noticeable
				const double secondsAtPixel = accumulateDuration(tick, TICKS_PER_BEAT, context.score.tempoChanges) - musicOffsetInSeconds;
				const bool outOfBounds = secondsAtPixel < 0 || secondsAtPixel > waveform.durationInSeconds;

				float amplitude = std::max(waveform.getAmplitudeAt(mip, secondsAtPixel, secondsPerPixel), 0.0f);
				float barValue = outOfBounds ? 0.0f : (amplitude * std::min(laneWidth * 6, 180.0f));
				float rectYPosition = floorf(position.y + visualOffset - y);

				// WARNING: A thickness of 0.5 or less does not draw with integrated graphics (optimization? limitation?)
				ImVec2 p1(timelineMidPosition - std::max(0.75f, barValue), rectYPosition);
				ImVec2 p2(timelineMidPosition + std::max(0.75f, barValue), rectYPosition);
				drawList->AddLine(p1, p2, waveformColor, 0.75f);
			}
		}
	}
}
