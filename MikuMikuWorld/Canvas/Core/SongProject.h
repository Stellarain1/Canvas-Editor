#pragma once
#include <string>
#include <vector>

// A song project: one small JSON file (".canvassong") that lists the charts of a song, one
// .mmws per difficulty, plus what they share (title, artist, music, offset, jacket).
//
//   {
//     "format": "canvas-editor/song", "version": 1,
//     "title": "...", "artist": "...", "designer": "...",
//     "music": "music.mp3", "musicOffset": 0, "jacket": "jacket.png",
//     "difficulties": [ { "name": "easy", "file": "easy.mmws", "level": 5 }, ... ]
//   }
//
// Paths in the file are relative to it; in memory they are absolute.
namespace Canvas
{
	inline constexpr const char* SONG_EXTENSION = ".canvassong";

	struct SongDifficulty
	{
		std::string name;	// easy / normal / hard / expert / master / append, or anything
		std::string file;	// absolute path of the .mmws
		int level{ 0 };		// play level shown in-game (0 = not set)
	};

	struct SongProject
	{
		std::string title;
		std::string artist;
		std::string designer;
		std::string musicFile;
		std::string jacketFile;
		float musicOffset{ 0.0f };
		std::vector<SongDifficulty> difficulties;

		SongDifficulty* find(const std::string& name);
		const SongDifficulty* find(const std::string& name) const;

		// Throws std::runtime_error with a readable message.
		static SongProject load(const std::string& path);
		void save(const std::string& path) const;
	};

	// easy, normal, hard, expert, master, append
	const std::vector<std::string>& standardDifficulties();

	// Sort key: standard difficulties in game order, custom ones after them.
	int difficultyOrder(const std::string& name);

	// "<folder>/<name>.mmws"
	std::string defaultDifficultyFile(const std::string& songPath, const std::string& name);
}
