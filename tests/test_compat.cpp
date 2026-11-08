#include "TestFramework.h"
#include "ChartBuilder.h"
#include "Canvas/Core/ChartIO.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// What the original MikuMikuWorld (and every fork of it) needs from the files Canvas writes.
// These guard the two bugs that made charts saved in Canvas unreadable there:
//  - .sus lines ended in CR CR LF, so the original's parser met a stray character at the end of
//    every data line ("invalid stoul argument")
//  - .mmws was written as version 7, which the original does not know (it crashes on it)

using namespace MikuMikuWorld;
using namespace CanvasTest;

namespace
{
	std::string tempPath(const std::string& name)
	{
		const auto dir = std::filesystem::temp_directory_path() / "canvas_tests";
		std::filesystem::create_directories(dir);
		return (dir / name).string();
	}

	std::vector<uint8_t> readBytes(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	Score fourFour()
	{
		Score score;
		score.tempoChanges = { Tempo(0, 150.0f) };
		score.timeSignatures[0] = TimeSignature{ 0, 4, 4 };
		return score;
	}

	Score chartWithExtras()
	{
		resetNextID();
		Score score = fourFour();
		for (int i = 0; i < 12; ++i)
			addTap(score, 1920 + i * 480, (i * 2) % 9, 2);
		addTap(score, 8000, 3, 2).speedRatio = 2.5f;
		HoldNote& hold = addHold(score, { 9000, 1, 3 }, { { 9480, 4, 3, HoldStepType::Normal } }, { 10200, 4, 3 });
		score.notes.at(hold.steps[0].ID).speedRatio = 0.5f;
		score.metadata.musicId = 123;
		score.seVolumeChanges = { { 0, 1.0f }, { 4000, 0.4f } };
		return score;
	}

	int mmwsVersion(const std::vector<uint8_t>& bytes)
	{
		// "MMWS", a terminating zero, then the version as a little-endian int32
		if (bytes.size() < 9 || bytes[0] != 'M' || bytes[1] != 'M' || bytes[2] != 'W' || bytes[3] != 'S')
			return -1;
		return bytes[5] | (bytes[6] << 8) | (bytes[7] << 16) | (bytes[8] << 24);
	}
}

TEST(sus_uses_plain_crlf_line_endings)
{
	const Score score = chartWithExtras();
	const std::string path = tempPath("endings.sus");
	Canvas::saveChart(score, path, Canvas::ExportFormat::Sus);
	const auto bytes = readBytes(path);
	int lf = 0, crlf = 0, crcrlf = 0;
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		if (bytes[i] != '\n')
			continue;
		++lf;
		if (i >= 1 && bytes[i - 1] == '\r')
		{
			++crlf;
			if (i >= 2 && bytes[i - 2] == '\r')
				++crcrlf;
		}
	}
	CHECK(lf > 10);
	CHECK_EQ(crlf, lf);		// every line ends in CR LF
	CHECK_EQ(crcrlf, 0);	// never CR CR LF
}

TEST(sus_data_lines_hold_whole_two_character_cells)
{
	// the original parser reads a note line two characters at a time and stops on anything else
	const Score score = chartWithExtras();
	const std::string path = tempPath("cells.sus");
	Canvas::saveChart(score, path, Canvas::ExportFormat::SusNoNoteSpeed);
	std::ifstream in(path, std::ios::binary);
	std::string line;
	int noteLines = 0;
	while (std::getline(in, line))
	{
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		const size_t colon = line.find(':');
		if (line.size() < 7 || line[0] != '#' || colon == std::string::npos)
			continue;
		const std::string header = line.substr(1, colon - 1);
		if (header.size() != 5 && header.size() != 6)
			continue;
		if (header.find_first_not_of("0123456789") <= 2)
			continue;
		const std::string data = line.substr(colon + 1);
		if (data.empty() || data.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
			continue;	// BPM / hi-speed / volume lines
		++noteLines;
		CHECK_EQ(data.size() % 2, size_t(0));
	}
	CHECK(noteLines > 3);
}

TEST(mmws_is_the_original_layout_by_default)
{
	const Score score = chartWithExtras();
	const std::string path = tempPath("compat.mmws");
	Canvas::saveChart(score, path, Canvas::ExportFormat::Mmws);
	CHECK_EQ(mmwsVersion(readBytes(path)), 4);

	const std::string extended = tempPath("extended.mmws");
	Canvas::saveChart(score, extended, Canvas::ExportFormat::MmwsExtended);
	CHECK_EQ(mmwsVersion(readBytes(extended)), 7);
}

TEST(mmws_keeps_note_speed_se_volume_and_music_id_in_either_layout)
{
	const Score score = chartWithExtras();
	for (auto format : { Canvas::ExportFormat::Mmws, Canvas::ExportFormat::MmwsExtended })
	{
		const std::string path = tempPath("extras.mmws");
		Canvas::saveChart(score, path, format);
		const Score loaded = Canvas::loadChart(path).score;

		CHECK_EQ(loaded.metadata.musicId, 123);
		CHECK_EQ(loaded.seVolumeChanges.size(), size_t(2));
		if (loaded.seVolumeChanges.size() == 2)
			CHECK(std::abs(loaded.seVolumeChanges[1].volume - 0.4f) < 1e-4f);

		int odd = 0;
		bool tapSpeed = false, midSpeed = false;
		for (const auto& [id, note] : loaded.notes)
		{
			if (std::abs(note.speedRatio - 1.0f) > 1e-4f)
				++odd;
			tapSpeed |= note.getType() == NoteType::Tap && std::abs(note.speedRatio - 2.5f) < 1e-4f;
			midSpeed |= note.getType() == NoteType::HoldMid && std::abs(note.speedRatio - 0.5f) < 1e-4f;
		}
		CHECK_EQ(odd, 2);
		CHECK(tapSpeed);
		CHECK(midSpeed);
		CHECK_EQ(loaded.notes.size(), score.notes.size());
	}
}

TEST(mmws_compatible_layout_warns_about_what_others_will_not_see)
{
	const Score score = chartWithExtras();
	const auto warnings = Canvas::saveChart(score, tempPath("warn.mmws"), Canvas::ExportFormat::Mmws);
	CHECK(!warnings.empty());
	const auto none = Canvas::saveChart(score, tempPath("warn_ext.mmws"), Canvas::ExportFormat::MmwsExtended);
	CHECK(none.empty());
}
