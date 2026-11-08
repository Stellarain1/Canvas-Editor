#include "NativeScoreSerializer.h"
#include "IO.h"
#include "Canvas/Core/ChartTransforms.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace MikuMikuWorld
{
	using namespace IO;
	using json = nlohmann::json;

	const int NativeScoreSerializer::SCORE_VERSION = 7;
	const int NativeScoreSerializer::COMPATIBLE_VERSION = 4;
	bool NativeScoreSerializer::saveExtendedByDefault = false;

	namespace
	{
		enum NoteFlags
		{
			NOTE_CRITICAL = 1 << 0,
			NOTE_FRICTION = 1 << 1,
			NOTE_DUMMY = 1 << 2		// UCMMWS
		};

		enum HoldFlags
		{
			HOLD_START_HIDDEN = 1 << 0,
			HOLD_END_HIDDEN = 1 << 1,
			HOLD_GUIDE = 1 << 2,
			HOLD_FAKE = 1 << 3		// UCMMWS
		};

		constexpr const char* EXTENSION_MAGIC = "CVEX";
		constexpr const char* EXTENSION_TRAILER = "CVEXTAIL";
		constexpr int EXTENSION_VERSION = 1;

		// Which on-disk layout we are reading
		struct Layout
		{
			int version{};			// MMWS version (1-7) or the CCMMWS base version
			int cyanvas{};			// CCMMWS extension version, 0 for plain MMWS
			int untitled{};			// UCMMWS version, 0 otherwise

			bool hasAddresses() const { return version > 2; }
			bool hasSkillFever() const { return version > 1; }
			bool hasHiSpeed() const { return version > 2; }
			bool hasHoldFlags() const { return version > 3; }
			bool hasMusicId() const { return cyanvas == 0 && version > 4; }
			bool hasSpeedRatio() const { return cyanvas == 0 && version > 5; }
			bool hasSEVolume() const { return cyanvas == 0 && version > 6; }
			bool hasLaneExtension() const { return cyanvas >= 1; }
			bool hasDamage() const { return cyanvas >= 1; }
			bool hasFade() const { return cyanvas >= 2; }
			bool hasGuideColor() const { return cyanvas >= 3; }
			bool hasLayers() const { return cyanvas >= 4; }
			bool hasWaypoints() const { return cyanvas >= 5; }
			bool hasFloatLanes() const { return cyanvas >= 6; }
			bool hasDummy() const { return untitled >= 1; }
		};

		struct ReadStats
		{
			int laneAdjusted{};
			int dummyDropped{};
			int damageDropped{};
		};

		Note readNote(NoteType type, const Layout& layout, BinaryReader& reader, ReadStats& stats, bool* dummy = nullptr)
		{
			Note note(type);
			note.tick = static_cast<int>(reader.readInt32());

			float lane = 0, width = 3;
			if (layout.hasFloatLanes())
			{
				lane = reader.readSingle();
				width = reader.readSingle();
			}
			else
			{
				lane = static_cast<float>(static_cast<int32_t>(reader.readInt32()));
				width = static_cast<float>(static_cast<int32_t>(reader.readInt32()));
			}

			if (layout.hasLayers())
				note.layer = static_cast<int>(reader.readInt32());

			if (!note.hasEase())
				note.flick = static_cast<FlickType>(std::min<uint32_t>(reader.readInt32(), static_cast<uint32_t>(FlickType::FlickTypeCount) - 1));

			const unsigned int flags = reader.readInt32();
			note.critical = (flags & NOTE_CRITICAL) != 0;
			note.friction = (flags & NOTE_FRICTION) != 0;
			if (dummy)
				*dummy = layout.hasDummy() && (flags & NOTE_DUMMY);

			if (layout.hasSpeedRatio())
			{
				note.speedRatio = reader.readSingle();
				if (!std::isfinite(note.speedRatio) || note.speedRatio <= 0.0f)
					note.speedRatio = 1.0f;
			}

			// Chart Cyanvas allows extended (off-field) and fractional lanes; Project SEKAI has 12
			int intWidth = std::clamp(static_cast<int>(std::lround(width)), MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
			int intLane = std::clamp(static_cast<int>(std::lround(lane)), MIN_LANE, MAX_LANE - intWidth + 1);
			if (intLane != lane || intWidth != width)
				++stats.laneAdjusted;
			note.lane = intLane;
			note.width = intWidth;
			return note;
		}

		void writeNote(const Note& note, BinaryWriter& writer, bool cyanvas, bool speedRatio = true)
		{
			writer.writeInt32(note.tick);
			if (cyanvas)
			{
				writer.writeSingle(static_cast<float>(note.lane));
				writer.writeSingle(static_cast<float>(note.width));
				writer.writeInt32(note.layer);
			}
			else
			{
				writer.writeInt32(note.lane);
				writer.writeInt32(note.width);
			}

			if (!note.hasEase())
				writer.writeInt32(static_cast<int>(note.flick));

			unsigned int flags{};
			if (note.critical) flags |= NOTE_CRITICAL;
			if (note.friction) flags |= NOTE_FRICTION;
			writer.writeInt32(flags);

			if (!cyanvas && speedRatio)
				writer.writeSingle(note.speedRatio);
		}

		// Plain MMWS readers only know linear / in / out: map the extended eases onto the closest
		// of those (the exact value is kept in the CVEX block)
		int legacyEase(EaseType ease)
		{
			switch (ease)
			{
			case EaseType::EaseInOut: return static_cast<int>(EaseType::EaseOut);
			case EaseType::EaseOutIn: return static_cast<int>(EaseType::EaseIn);
			default: return static_cast<int>(ease);
			}
		}

		EaseType toEase(uint32_t value)
		{
			return value < static_cast<uint32_t>(EaseType::EaseTypeCount) ? static_cast<EaseType>(value) : EaseType::Linear;
		}
	}

	void NativeScoreSerializer::serialize(const Score& score, std::string filename)
	{
		warnings.clear();
		if (flavor == Flavor::MikuMikuWorld4CC)
			serializeCyanvas(score, filename);
		else
			serializeCanvas(score, filename, flavor == Flavor::CanvasMmwsExtended || saveExtendedByDefault);
	}

	void NativeScoreSerializer::serializeCanvas(const Score& input, const std::string& filename, bool extended)
	{
		Score score = input;
		Canvas::normalizeEvents(score);

		if (!extended)
		{
			// kept inside the file for Canvas, but the original MikuMikuWorld has nowhere to read them
			if (int n = Canvas::countNotesWithSpeedRatio(score))
				warnings.push_back(IO::formatString("%d note speed ratio(s) are stored in Canvas' own block; other editors will not see them (Settings > Canvas > Export has the extended format).", n));
			if (!score.seVolumeChanges.empty())
				warnings.push_back(IO::formatString("%d SE volume change(s) are stored in Canvas' own block; other editors will not see them.", static_cast<int>(score.seVolumeChanges.size())));
		}

		BinaryWriter writer(filename);
		if (!writer.isStreamValid())
			throw std::runtime_error("Could not open the file for writing.");

		writer.writeString("MMWS");
		writer.writeInt32(extended ? SCORE_VERSION : COMPATIBLE_VERSION);

		// offsets address in order: metadata -> events -> taps -> holds
		uint32_t offsetsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t) * 4);

		// ---- metadata
		uint32_t metadataAddress = writer.getStreamPosition();
		writer.writeString(score.metadata.title);
		writer.writeString(score.metadata.author);
		writer.writeString(score.metadata.artist);
		writer.writeString(score.metadata.musicFile);
		writer.writeSingle(score.metadata.musicOffset);
		writer.writeString(score.metadata.jacketFile);
		if (extended)
			writer.writeInt32(score.metadata.musicId);

		// ---- events (the base section only carries the default layer's hi-speeds)
		uint32_t eventsAddress = writer.getStreamPosition();
		writer.writeInt32(score.timeSignatures.size());
		for (const auto& [_, ts] : score.timeSignatures)
		{
			writer.writeInt32(ts.measure);
			writer.writeInt32(ts.numerator);
			writer.writeInt32(ts.denominator);
		}

		writer.writeInt32(score.tempoChanges.size());
		for (const auto& tempo : score.tempoChanges)
		{
			writer.writeInt32(tempo.tick);
			writer.writeSingle(tempo.bpm);
		}

		std::vector<HiSpeedChange> baseSpeeds = hiSpeedsOfLayer(score, 0);
		writer.writeInt32(baseSpeeds.size());
		for (const auto& hiSpeed : baseSpeeds)
		{
			writer.writeInt32(hiSpeed.tick);
			writer.writeSingle(hiSpeed.speed);
		}

		if (extended)
		{
			writer.writeInt32(score.seVolumeChanges.size());
			for (const auto& seVolume : score.seVolumeChanges)
			{
				writer.writeInt32(seVolume.tick);
				writer.writeSingle(seVolume.volume);
			}
		}

		writer.writeInt32(score.skills.size());
		for (const auto& skill : score.skills)
			writer.writeInt32(skill.tick);

		writer.writeInt32(score.fever.chanceTick);
		writer.writeInt32(score.fever.startTick);

		// ---- taps
		json tapLayers = json::object();
		json noteStages = json::object();	// "t<tap>" / "h<hold>" -> stage id (0 is left out)
		json tapSpeeds = json::object();	// version 4 has no room for note speeds: they go in the extension block
		uint32_t tapsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t));

		int tapCount = 0;
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap)
				continue;

			if (note.layer != 0)
				tapLayers[std::to_string(tapCount)] = note.layer;
			if (const int stage = score.stages.stageOf(id); stage != 0)
				noteStages["t" + std::to_string(tapCount)] = stage;
			if (!extended && std::abs(note.speedRatio - 1.0f) > 0.0001f)
				tapSpeeds[std::to_string(tapCount)] = note.speedRatio;

			writeNote(note, writer, false, extended);
			++tapCount;
		}

		uint32_t holdsAddress = writer.getStreamPosition();
		writer.seek(tapsAddress);
		writer.writeInt32(tapCount);
		writer.seek(holdsAddress);

		// ---- holds
		json holdExtras = json::object();
		int holdIndex = 0;
		writer.writeInt32(score.holdNotes.size());
		for (const auto& [id, hold] : score.holdNotes)
		{
			unsigned int flags{};
			if (hold.startType == HoldNoteType::Guide) flags |= HOLD_GUIDE;
			if (hold.startType == HoldNoteType::Hidden) flags |= HOLD_START_HIDDEN;
			if (hold.endType == HoldNoteType::Hidden) flags |= HOLD_END_HIDDEN;
			writer.writeInt32(flags);

			const Note& start = score.notes.at(hold.start.ID);
			writeNote(start, writer, false, extended);
			// a hold lives on one stage: its start note's
			if (const int stage = score.stages.stageOf(hold.start.ID); stage != 0)
				noteStages["h" + std::to_string(holdIndex)] = stage;
			writer.writeInt32(legacyEase(hold.start.ease));

			json extra = json::object();
			bool needsExtra = false;
			json layers = json::array({ start.layer });
			json eases = json::array({ static_cast<int>(hold.start.ease) });
			json speeds = json::array({ start.speedRatio });
			bool oddSpeed = std::abs(start.speedRatio - 1.0f) > 0.0001f;
			needsExtra |= start.layer != 0 || isExtendedEase(hold.start.ease);

			writer.writeInt32(hold.steps.size());
			for (const auto& step : hold.steps)
			{
				const Note& mid = score.notes.at(step.ID);
				writeNote(mid, writer, false, extended);
				writer.writeInt32(static_cast<int>(step.type));
				writer.writeInt32(legacyEase(step.ease));
				layers.push_back(mid.layer);
				eases.push_back(static_cast<int>(step.ease));
				speeds.push_back(mid.speedRatio);
				oddSpeed |= std::abs(mid.speedRatio - 1.0f) > 0.0001f;
				needsExtra |= mid.layer != 0 || isExtendedEase(step.ease);
			}

			const Note& end = score.notes.at(hold.end);
			writeNote(end, writer, false, extended);
			layers.push_back(end.layer);
			speeds.push_back(end.speedRatio);
			oddSpeed |= std::abs(end.speedRatio - 1.0f) > 0.0001f;
			needsExtra |= end.layer != 0;

			if (hold.isGuide() && (hold.guideColor != (start.critical ? GuideColor::Yellow : GuideColor::Green) || hold.fadeType != FadeType::Out))
			{
				extra["guideColor"] = static_cast<int>(hold.guideColor);
				extra["fade"] = static_cast<int>(hold.fadeType);
				needsExtra = true;
			}

			if (!extended && oddSpeed)
			{
				extra["speeds"] = speeds;
				needsExtra = true;
			}

			if (needsExtra)
			{
				extra["layers"] = layers;
				extra["eases"] = eases;
				holdExtras[std::to_string(holdIndex)] = extra;
			}
			++holdIndex;
		}

		// ---- Canvas extension block
		const bool multiLayer = score.layers.size() > 1 || score.layers.front().name != "default" || score.layers.front().hidden;
		// what the version 4 layout cannot hold
		const bool compatExtras = (!extended && (score.metadata.musicId != 0 || !score.seVolumeChanges.empty() || !tapSpeeds.empty())) || score.metadata.endTick >= 0 ||
			!score.animation.empty() || !score.stages.empty();
		if (multiLayer || !tapLayers.empty() || !holdExtras.empty() || compatExtras)
		{
			json ext;
			ext["format"] = "canvas-editor/mmws-extension";
			ext["version"] = EXTENSION_VERSION;
			ext["tapCount"] = tapCount;
			ext["holdCount"] = holdIndex;

			json layers = json::array();
			for (const auto& layer : score.layers)
				layers.push_back({ { "name", layer.name }, { "hidden", layer.hidden } });
			ext["layers"] = layers;

			json speeds = json::array();
			for (const auto& change : score.hiSpeedChanges)
				speeds.push_back({ change.tick, change.speed, change.layer });
			ext["hiSpeeds"] = speeds;
			ext["tapLayers"] = tapLayers;
			ext["holds"] = holdExtras;
			if (score.metadata.endTick >= 0)
				ext["endTick"] = score.metadata.endTick;
			if (!score.animation.empty())
				ext["previewAnimation"] = json::parse(Canvas::Animation::toJson(score.animation));
			if (!score.stages.empty())
			{
				json stages = json::parse(Canvas::Stages::toJson(score.stages));
				stages.erase("notes");	// keyed by this session's note ids: noteStages says it by position
				ext["dynamicStages"] = stages;
				ext["noteStages"] = noteStages;
			}
			if (!extended)
			{
				if (score.metadata.musicId != 0)
					ext["musicId"] = score.metadata.musicId;
				json volumes = json::array();
				for (const auto& change : score.seVolumeChanges)
					volumes.push_back({ change.tick, change.volume });
				if (!volumes.empty())
					ext["seVolumes"] = volumes;
				if (!tapSpeeds.empty())
					ext["tapSpeeds"] = tapSpeeds;
			}

			const std::string payload = ext.dump();
			const uint32_t extensionAddress = writer.getStreamPosition();
			writer.writeBytes(EXTENSION_MAGIC);
			writer.writeInt32(EXTENSION_VERSION);
			writer.writeInt32(payload.size());
			writer.writeBytes(payload);
			writer.writeInt32(extensionAddress);
			writer.writeBytes(EXTENSION_TRAILER);
		}

		writer.seek(offsetsAddress);
		writer.writeInt32(metadataAddress);
		writer.writeInt32(eventsAddress);
		writer.writeInt32(tapsAddress);
		writer.writeInt32(holdsAddress);

		writer.flush();
		writer.close();
	}

	void NativeScoreSerializer::serializeCyanvas(const Score& input, const std::string& filename)
	{
		Score score = input;
		Canvas::normalizeEvents(score);

		if (int n = Canvas::countNotesWithSpeedRatio(score))
			warnings.push_back(IO::formatString("%d note speed ratio(s) were dropped: CCMMWS has no per-note speed.", n));
		if (!score.seVolumeChanges.empty())
			warnings.push_back(IO::formatString("%d SE volume change(s) were dropped: CCMMWS has no SE volume.", static_cast<int>(score.seVolumeChanges.size())));

		BinaryWriter writer(filename);
		if (!writer.isStreamValid())
			throw std::runtime_error("Could not open the file for writing.");

		writer.writeString("CCMMWS");
		writer.writeInt16(4);	// base version
		writer.writeInt16(6);	// cyanvas version

		// metadata -> events -> taps -> holds -> damages -> layers -> waypoints
		uint32_t offsetsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t) * 7);

		uint32_t metadataAddress = writer.getStreamPosition();
		writer.writeString(score.metadata.title);
		writer.writeString(score.metadata.author);
		writer.writeString(score.metadata.artist);
		writer.writeString(score.metadata.musicFile);
		writer.writeSingle(score.metadata.musicOffset);
		writer.writeString(score.metadata.jacketFile);
		writer.writeInt32(0); // lane extension

		uint32_t eventsAddress = writer.getStreamPosition();
		writer.writeInt32(score.timeSignatures.size());
		for (const auto& [_, ts] : score.timeSignatures)
		{
			writer.writeInt32(ts.measure);
			writer.writeInt32(ts.numerator);
			writer.writeInt32(ts.denominator);
		}

		writer.writeInt32(score.tempoChanges.size());
		for (const auto& tempo : score.tempoChanges)
		{
			writer.writeInt32(tempo.tick);
			writer.writeSingle(tempo.bpm);
		}

		writer.writeInt32(score.hiSpeedChanges.size());
		for (const auto& hiSpeed : score.hiSpeedChanges)
		{
			writer.writeInt32(hiSpeed.tick);
			writer.writeSingle(hiSpeed.speed);
			writer.writeInt32(hiSpeed.layer);
		}

		writer.writeInt32(score.skills.size());
		for (const auto& skill : score.skills)
			writer.writeInt32(skill.tick);
		writer.writeInt32(score.fever.chanceTick);
		writer.writeInt32(score.fever.startTick);

		uint32_t tapsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t));
		int tapCount = 0;
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap)
				continue;
			writeNote(note, writer, true);
			++tapCount;
		}

		uint32_t holdsAddress = writer.getStreamPosition();
		writer.seek(tapsAddress);
		writer.writeInt32(tapCount);
		writer.seek(holdsAddress);

		writer.writeInt32(score.holdNotes.size());
		for (const auto& [id, hold] : score.holdNotes)
		{
			unsigned int flags{};
			if (hold.startType == HoldNoteType::Guide) flags |= HOLD_GUIDE;
			if (hold.startType == HoldNoteType::Hidden) flags |= HOLD_START_HIDDEN;
			if (hold.endType == HoldNoteType::Hidden) flags |= HOLD_END_HIDDEN;
			writer.writeInt32(flags);

			writeNote(score.notes.at(hold.start.ID), writer, true);
			writer.writeInt32(static_cast<int>(hold.start.ease));
			writer.writeInt32(static_cast<int>(hold.fadeType));
			writer.writeInt32(static_cast<int>(hold.guideColor));

			writer.writeInt32(hold.steps.size());
			for (const auto& step : hold.steps)
			{
				writeNote(score.notes.at(step.ID), writer, true);
				writer.writeInt32(static_cast<int>(step.type));
				writer.writeInt32(static_cast<int>(step.ease));
			}

			writeNote(score.notes.at(hold.end), writer, true);
		}

		uint32_t damagesAddress = writer.getStreamPosition();
		writer.writeInt32(0);

		uint32_t layersAddress = writer.getStreamPosition();
		writer.writeInt32(score.layers.size());
		for (const auto& layer : score.layers)
			writer.writeString(layer.name);

		uint32_t waypointsAddress = writer.getStreamPosition();
		writer.writeInt32(0);

		writer.seek(offsetsAddress);
		writer.writeInt32(metadataAddress);
		writer.writeInt32(eventsAddress);
		writer.writeInt32(tapsAddress);
		writer.writeInt32(holdsAddress);
		writer.writeInt32(damagesAddress);
		writer.writeInt32(layersAddress);
		writer.writeInt32(waypointsAddress);

		writer.flush();
		writer.close();
	}

	Score NativeScoreSerializer::deserialize(std::string filename)
	{
		warnings.clear();
		Score score;
		BinaryReader reader(filename);
		if (!reader.isStreamValid())
			throw std::runtime_error("Could not open the file.");

		const size_t fileSize = reader.getFileSize();
		const std::string signature = reader.readString();

		Layout layout;
		if (signature == "MMWS")
		{
			layout.version = static_cast<int>(reader.readInt32());
		}
		else if (signature == "CCMMWS")
		{
			layout.version = reader.readInt16();
			layout.cyanvas = std::max<int>(1, reader.readInt16());
		}
		else if (signature == "UCMMWS")
		{
			layout.untitled = static_cast<int>(reader.readInt32());
			layout.version = 4;
			layout.cyanvas = 6;
		}
		else
		{
			throw std::runtime_error("Not an MMWS file (unknown signature).");
		}

		if (signature == "MMWS" && layout.version > SCORE_VERSION)
			warnings.push_back(IO::formatString("This MMWS file is version %d; the newest version this build knows is %d. It may not load correctly.", layout.version, SCORE_VERSION));

		uint32_t metadataAddress{}, eventsAddress{}, tapsAddress{}, holdsAddress{};
		uint32_t damagesAddress{}, layersAddress{}, waypointsAddress{};
		if (layout.hasAddresses())
		{
			metadataAddress = reader.readInt32();
			eventsAddress = reader.readInt32();
			tapsAddress = reader.readInt32();
			holdsAddress = reader.readInt32();
			if (layout.hasDamage()) damagesAddress = reader.readInt32();
			if (layout.hasLayers()) layersAddress = reader.readInt32();
			if (layout.hasWaypoints()) waypointsAddress = reader.readInt32();
			reader.seek(metadataAddress);
		}

		// ---- metadata
		score.metadata.title = reader.readString();
		score.metadata.author = reader.readString();
		score.metadata.artist = reader.readString();
		score.metadata.musicFile = reader.readString();
		score.metadata.musicOffset = reader.readSingle();
		if (layout.version > 1)
			score.metadata.jacketFile = reader.readString();
		if (layout.hasMusicId())
			score.metadata.musicId = static_cast<int>(reader.readInt32());
		if (layout.hasLaneExtension())
		{
			const int laneExtension = static_cast<int>(reader.readInt32());
			if (laneExtension != 0)
				warnings.push_back("The chart uses Chart Cyanvas extended lanes; notes outside the 12 Project SEKAI lanes were moved inside.");
		}

		// ---- events
		if (layout.hasAddresses())
			reader.seek(eventsAddress);

		const int timeSignatureCount = static_cast<int>(reader.readInt32());
		if (timeSignatureCount)
			score.timeSignatures.clear();
		for (int i = 0; i < timeSignatureCount; ++i)
		{
			int measure = static_cast<int>(reader.readInt32());
			int numerator = static_cast<int>(reader.readInt32());
			int denominator = static_cast<int>(reader.readInt32());
			score.timeSignatures[measure] = { measure, numerator, denominator };
		}

		const int tempoCount = static_cast<int>(reader.readInt32());
		if (tempoCount)
			score.tempoChanges.clear();
		for (int i = 0; i < tempoCount; ++i)
		{
			int tick = static_cast<int>(reader.readInt32());
			float bpm = reader.readSingle();
			score.tempoChanges.push_back({ tick, bpm });
		}

		if (layout.hasHiSpeed())
		{
			const int hiSpeedCount = static_cast<int>(reader.readInt32());
			for (int i = 0; i < hiSpeedCount; ++i)
			{
				int tick = static_cast<int>(reader.readInt32());
				float speed = reader.readSingle();
				int layer = layout.hasLayers() ? static_cast<int>(reader.readInt32()) : 0;
				score.hiSpeedChanges.push_back({ tick, speed, layer });
			}
		}

		if (layout.hasSEVolume())
		{
			const int seVolumeCount = static_cast<int>(reader.readInt32());
			for (int i = 0; i < seVolumeCount; ++i)
			{
				int tick = static_cast<int>(reader.readInt32());
				float volume = reader.readSingle();
				score.seVolumeChanges.push_back({ tick, volume });
			}
		}

		if (layout.hasSkillFever())
		{
			const int skillCount = static_cast<int>(reader.readInt32());
			for (int i = 0; i < skillCount; ++i)
				score.skills.push_back({ nextSkillID++, static_cast<int>(reader.readInt32()) });

			score.fever.chanceTick = static_cast<int32_t>(reader.readInt32());
			score.fever.startTick = static_cast<int32_t>(reader.readInt32());
			if (score.fever.chanceTick < 0) score.fever.chanceTick = -1;
			if (score.fever.startTick < 0) score.fever.startTick = -1;
		}

		ReadStats stats;
		std::vector<int> tapIds;

		// ---- taps
		if (layout.hasAddresses())
			reader.seek(tapsAddress);

		const int tapCount = static_cast<int>(reader.readInt32());
		for (int i = 0; i < tapCount; ++i)
		{
			bool dummy = false;
			Note note = readNote(NoteType::Tap, layout, reader, stats, &dummy);
			if (dummy)
			{
				++stats.dummyDropped;
				tapIds.push_back(-1);
				continue;
			}
			note.ID = nextID++;
			score.notes[note.ID] = note;
			tapIds.push_back(note.ID);
		}

		// ---- holds
		if (layout.hasAddresses())
			reader.seek(holdsAddress);

		std::vector<int> holdIds;
		const int holdCount = static_cast<int>(reader.readInt32());
		for (int i = 0; i < holdCount; ++i)
		{
			HoldNote hold;
			const unsigned int flags = layout.hasHoldFlags() ? reader.readInt32() : 0;
			if (flags & HOLD_START_HIDDEN)
				hold.startType = HoldNoteType::Hidden;
			if (flags & HOLD_END_HIDDEN)
				hold.endType = HoldNoteType::Hidden;
			if (flags & HOLD_GUIDE)
				hold.startType = hold.endType = HoldNoteType::Guide;
			const bool fake = layout.hasDummy() && (flags & HOLD_FAKE);

			Note start = readNote(NoteType::Hold, layout, reader, stats);
			start.ID = nextID++;
			hold.start.ease = toEase(reader.readInt32());
			hold.start.ID = start.ID;
			if (layout.hasFade())
				hold.fadeType = static_cast<FadeType>(std::min<uint32_t>(reader.readInt32(), 2));
			if (layout.hasGuideColor())
				hold.guideColor = static_cast<GuideColor>(std::min<uint32_t>(reader.readInt32(), static_cast<uint32_t>(GuideColor::GuideColorCount) - 1));
			else
				hold.guideColor = start.critical ? GuideColor::Yellow : GuideColor::Green;

			std::vector<Note> mids;
			const int stepCount = static_cast<int>(reader.readInt32());
			hold.steps.reserve(stepCount);
			for (int s = 0; s < stepCount; ++s)
			{
				Note mid = readNote(NoteType::HoldMid, layout, reader, stats);
				mid.ID = nextID++;
				mid.parentID = start.ID;

				HoldStep step{};
				step.type = static_cast<HoldStepType>(std::min<uint32_t>(reader.readInt32(), 2));
				step.ease = toEase(reader.readInt32());
				step.ID = mid.ID;
				hold.steps.push_back(step);
				mids.push_back(mid);
			}

			Note end = readNote(NoteType::HoldEnd, layout, reader, stats);
			end.ID = nextID++;
			end.parentID = start.ID;
			hold.end = end.ID;

			if (fake)
			{
				++stats.dummyDropped;
				holdIds.push_back(-1);
				continue;
			}

			score.notes[start.ID] = start;
			for (const auto& mid : mids)
				score.notes[mid.ID] = mid;
			score.notes[end.ID] = end;
			if (hold.isGuide())
				syncGuideCritical(score, hold);
			score.holdNotes[start.ID] = hold;
			holdIds.push_back(start.ID);
		}

		// ---- Chart Cyanvas sections
		if (layout.hasDamage() && damagesAddress)
		{
			reader.seek(damagesAddress);
			stats.damageDropped = static_cast<int>(reader.readInt32());
		}

		if (layout.hasLayers() && layersAddress)
		{
			reader.seek(layersAddress);
			const int layerCount = static_cast<int>(reader.readInt32());
			score.layers.clear();
			for (int i = 0; i < layerCount; ++i)
				score.layers.push_back({ reader.readString() });
			if (score.layers.empty())
				score.layers.push_back({ "default" });
		}

		// ---- Canvas extension block (MMWS written by Canvas Editor)
		if (signature == "MMWS" && fileSize > 16)
		{
			reader.seek(fileSize - 12);
			const uint32_t extensionAddress = reader.readInt32();
			const std::string trailer = reader.readBytes(8);
			if (trailer == EXTENSION_TRAILER && extensionAddress < fileSize - 12)
			{
				reader.seek(extensionAddress);
				const std::string magic = reader.readBytes(4);
				reader.readInt32(); // extension version
				const uint32_t length = reader.readInt32();
				const json ext = magic == EXTENSION_MAGIC && length < fileSize ? json::parse(reader.readBytes(length), nullptr, false) : json();

				if (ext.is_object() && ext.value("tapCount", -1) == tapCount && ext.value("holdCount", -1) == holdCount)
				{
					// what the version 4 layout has no room for
					if (ext.contains("musicId") && ext["musicId"].is_number_integer())
						score.metadata.musicId = ext["musicId"].get<int>();
					if (ext.contains("endTick") && ext["endTick"].is_number_integer())
						score.metadata.endTick = ext["endTick"].get<int>();
					if (ext.contains("previewAnimation") && ext["previewAnimation"].is_object())
						Canvas::Animation::fromJson(ext["previewAnimation"].dump(), score.animation);
					if (ext.contains("dynamicStages") && ext["dynamicStages"].is_object())
					{
						Canvas::Stages::fromJson(ext["dynamicStages"].dump(), score.stages);
						score.stages.noteStage.clear();
						if (ext.contains("noteStages") && ext["noteStages"].is_object())
							for (const auto& [key, value] : ext["noteStages"].items())
							{
								if (key.size() < 2 || !value.is_number_integer())
									continue;
								const size_t i = std::strtoul(key.c_str() + 1, nullptr, 10);
								const int stage = value.get<int>();
								if (key[0] == 't' && i < tapIds.size() && tapIds[i] >= 0)
									score.stages.assign(tapIds[i], stage);
								else if (key[0] == 'h' && i < holdIds.size() && holdIds[i] >= 0)
								{
									const HoldNote& hold = score.holdNotes.at(holdIds[i]);
									score.stages.assign(hold.start.ID, stage);
									for (const auto& step : hold.steps)
										score.stages.assign(step.ID, stage);
									score.stages.assign(hold.end, stage);
								}
							}
						score.stages.prune(score);
					}
					if (ext.contains("seVolumes") && ext["seVolumes"].is_array())
					{
						score.seVolumeChanges.clear();
						for (const auto& change : ext["seVolumes"])
							if (change.is_array() && change.size() >= 2)
								score.seVolumeChanges.push_back({ change[0].get<int>(), change[1].get<float>() });
					}
					if (ext.contains("tapSpeeds") && ext["tapSpeeds"].is_object())
					{
						for (const auto& [index, ratio] : ext["tapSpeeds"].items())
						{
							const size_t i = std::stoul(index);
							const float value = ratio.get<float>();
							if (i < tapIds.size() && tapIds[i] >= 0 && std::isfinite(value) && value > 0.0f)
								score.notes.at(tapIds[i]).speedRatio = value;
						}
					}

					if (ext.contains("layers") && ext["layers"].is_array() && !ext["layers"].empty())
					{
						score.layers.clear();
						for (const auto& layer : ext["layers"])
							score.layers.push_back({ layer.value("name", std::string("layer")), layer.value("hidden", false) });
					}

					if (ext.contains("hiSpeeds") && ext["hiSpeeds"].is_array())
					{
						score.hiSpeedChanges.clear();
						for (const auto& change : ext["hiSpeeds"])
							if (change.is_array() && change.size() >= 3)
								score.hiSpeedChanges.push_back({ change[0].get<int>(), change[1].get<float>(), change[2].get<int>() });
					}

					if (ext.contains("tapLayers") && ext["tapLayers"].is_object())
					{
						for (const auto& [index, layer] : ext["tapLayers"].items())
						{
							const size_t i = std::stoul(index);
							if (i < tapIds.size() && tapIds[i] >= 0)
								score.notes.at(tapIds[i]).layer = layer.get<int>();
						}
					}

					if (ext.contains("holds") && ext["holds"].is_object())
					{
						for (const auto& [index, extra] : ext["holds"].items())
						{
							const size_t i = std::stoul(index);
							if (i >= holdIds.size() || holdIds[i] < 0)
								continue;

							HoldNote& hold = score.holdNotes.at(holdIds[i]);
							if (extra.contains("guideColor"))
								hold.guideColor = static_cast<GuideColor>(std::clamp(extra["guideColor"].get<int>(), 0, static_cast<int>(GuideColor::GuideColorCount) - 1));
							if (extra.contains("fade"))
								hold.fadeType = static_cast<FadeType>(std::clamp(extra["fade"].get<int>(), 0, 2));

							if (extra.contains("eases") && extra["eases"].is_array() && extra["eases"].size() == hold.steps.size() + 1)
							{
								hold.start.ease = toEase(extra["eases"][0].get<int>());
								for (size_t s = 0; s < hold.steps.size(); ++s)
									hold.steps[s].ease = toEase(extra["eases"][s + 1].get<int>());
							}

							if (extra.contains("speeds") && extra["speeds"].is_array() && extra["speeds"].size() == hold.steps.size() + 2)
							{
								const auto& speeds = extra["speeds"];
								auto apply = [&](int id, const nlohmann::json& value) {
									const float ratio = value.get<float>();
									if (std::isfinite(ratio) && ratio > 0.0f)
										score.notes.at(id).speedRatio = ratio;
								};
								apply(hold.start.ID, speeds[0]);
								for (size_t s = 0; s < hold.steps.size(); ++s)
									apply(hold.steps[s].ID, speeds[s + 1]);
								apply(hold.end, speeds[speeds.size() - 1]);
							}

							if (extra.contains("layers") && extra["layers"].is_array() && extra["layers"].size() == hold.steps.size() + 2)
							{
								const auto& layers = extra["layers"];
								score.notes.at(hold.start.ID).layer = layers[0].get<int>();
								for (size_t s = 0; s < hold.steps.size(); ++s)
									score.notes.at(hold.steps[s].ID).layer = layers[s + 1].get<int>();
								score.notes.at(hold.end).layer = layers[layers.size() - 1].get<int>();
							}

							if (hold.isGuide())
								syncGuideCritical(score, hold);
						}
					}
				}
				else if (magic == EXTENSION_MAGIC)
				{
					warnings.push_back("This file's Canvas extension block does not match its notes (was it edited by another program?); layers and guide colours from it were ignored.");
				}
			}
		}

		reader.close();

		if (stats.laneAdjusted)
			warnings.push_back(IO::formatString("%d note(s) used fractional or extended lanes and were snapped into the 12 lanes.", stats.laneAdjusted));
		if (stats.dummyDropped)
			warnings.push_back(IO::formatString("%d fake/dummy note(s) were dropped (not supported by Project SEKAI).", stats.dummyDropped));
		if (stats.damageDropped)
			warnings.push_back(IO::formatString("%d damage note(s) were dropped (not supported by Project SEKAI).", stats.damageDropped));

		Canvas::normalizeEvents(score);
		return score;
	}
}
