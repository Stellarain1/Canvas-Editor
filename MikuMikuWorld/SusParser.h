#pragma once
#include <string>
#include "SUS.h"

namespace MikuMikuWorld
{
	class SusDataLine
	{
	private:
		int measureOffset{};
		int measure{};

	public:
		std::string header{};
		std::string data{};
		// #HISPEED id in effect when this line was read ("" = none given, i.e. #TIL00)
		std::string hiSpeedGroupId{};

		SusDataLine(int measureOffset, const std::string& line);

		inline constexpr int getEffectiveMeasure() const { return measureOffset + measure; }
	};

	class SusParser
	{
	private:
		int ticksPerBeat;
		int measureOffset;
		float waveOffset;
		std::string title;
		std::string artist;
		std::string designer;
		std::map<std::string, float> bpmDefinitions;
		std::vector<Bar> bars;
		std::string currentHiSpeedGroupId;

		bool isCommand(const std::string& line);
		int getTicks(int measure, int i, int total);
		std::vector<std::pair<std::string, float>> getNoteCells(const std::string& data);

		SUSNoteStream getNoteStream(const std::vector<SUSNote>& stream);
		std::vector<SUSNote> getNotes(const SusDataLine& line, int hiSpeedGroup = 0);
		std::vector<BPM> getBpms(const std::vector<SusDataLine>& bpmLines);
		std::vector<Bar> getBars(const std::vector<BarLength>& barLengths);
		std::vector<HiSpeed> getHiSpeeds(const std::vector<SusDataLine>& hiSpeeds);
		std::vector<SEVolume> getSEVolumes(const std::vector<SusDataLine>& seVolumeLines);

	public:
		SusParser();

		SUS parse(const std::string& filename);
		void processCommand(std::string& line);
	};
}
