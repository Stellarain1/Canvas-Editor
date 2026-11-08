#pragma once
#include <string>
#include <map>
#include <vector>
#include "IO.h"
#include "SUS.h"

namespace MikuMikuWorld
{
	// SUS can only handle up to (36^2) - 1 unique BPMs
	constexpr size_t MAX_BPM_IDENTIFIERS = (36ll * 36ll) - 1;

	class ChannelProvider
	{
	public:
		struct TickRange { int start; int end; };
		ChannelProvider()
		{
			clear();
		}

		int generateChannel(int startTick, int endTick);
		void clear();

	private:
		std::map<int, TickRange> channels;
	};

	class SusExportError : public std::runtime_error
	{
	public:
		SusExportError(const char* message) : std::runtime_error(message) {}
		
		virtual ~SusExportError() = default;
		virtual std::string_view getDetailedMessage() const = 0;
	};

	class TooManySlideChannelsError : public SusExportError
	{
	public:
		std::string_view getDetailedMessage() const { return detailedMessage; }

		TooManySlideChannelsError(const ChannelProvider::TickRange& tickRange)
			: tickRange{ tickRange }, SusExportError("No more slide channels available")
		{
			detailedMessage = IO::formatString("Too many overlapping slides/guides. Exporter ran out of channels at range (%d - %d)",
				this->tickRange.start, this->tickRange.end);
		}

	private:
		ChannelProvider::TickRange tickRange;
		std::string detailedMessage;
	};

	class TooManyBpmIdentifiersError : public SusExportError
	{
	public:
		TooManyBpmIdentifiersError(size_t count) : count{ count }, SusExportError("Too many BPM changes.")
		{
			detailedMessage = IO::formatString("Number of unique identifiers (%d) exceeds maximum supported (%d)",
				count, MAX_BPM_IDENTIFIERS);
		}

		std::string_view getDetailedMessage() const { return detailedMessage; }

	private:
		std::string detailedMessage;
		size_t count{};
	};

	struct NoteMap
	{
		struct RawData
		{
			int tick;
			std::string data;
			float speedRatio{ 1.0f };
		};

		std::vector<RawData> data;
		int ticksPerMeasure;
	};

	struct MeasureMap
	{
		int measure;
		// keyed by (hi-speed group, channel info) so lines of different #HISPEED groups never mix
		std::map<std::pair<int, std::string>, NoteMap> notesMap;
	};

	struct BarLengthTicks
	{
		BarLength barLength;
		int ticks;
	};

	class SusExporter
	{
	private:
		int ticksPerBeat;
		std::map<int, MeasureMap> measuresMap;
		std::vector<BarLengthTicks> barLengthTicks;
		std::vector<std::string> hiSpeedGroupIds;
		int writtenHiSpeedGroup{ 0 };

		int getMeasureFromTicks(int ticks);
		int getTicksFromMeasure(int measure);
		void appendSlideData(const SUSNoteStream& slides, const std::string& infoPrefix);
		std::vector<std::string> getNoteLines(int baseMeasure, bool includeNoteSpeed);

	public:
		SusExporter();

		void appendData(int tick, std::string info, std::string data, float speedRatio = 1.0f, int hiSpeedGroup = 0);
		void appendNoteData(const SUSNote& note, const std::string infoPrefix, const std::string channel);
		void dump(const SUS& sus, const std::string& filename, std::string comment = "", bool includeNoteSpeed = false);
	};
}
