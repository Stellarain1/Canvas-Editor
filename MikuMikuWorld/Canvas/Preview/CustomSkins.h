#pragma once
#include <string>
#include <vector>

// Custom note skins, note effects and note sounds: folders the user adds next to the built-in sets.
//
//   <user data>/custom/note_skins/<name>/    notes.png, longNoteLine.png, touchLine_eff.png (+ spr/*.txt)
//   <user data>/custom/effect_skins/<name>/  tex_note_common_all_v2.png, fx_*.json (the particle effects)
//   <user data>/custom/sound_skins/<name>/   se_live_perfect.flac, se_live_flick.mp3 ... (.flac / .mp3 / .wav)
//
// A skin only needs the files it changes: anything missing comes from the built-in default (notes 01,
// effects "normal", the SEKAI sound set). "New from built-in" copies the whole default set into a new
// folder to start from.
namespace Canvas::CustomSkins
{
	enum class Kind { Notes, Effects, Sounds };

	std::string folder(Kind kind);
	// folder names, sorted
	std::vector<std::string> list(Kind kind);
	// copies the built-in set into <folder>/<name>; false with `error` when it cannot. `name` comes back
	// cleaned (only letters, digits, space, - and _), which is the folder name
	bool createFromBuiltIn(Kind kind, std::string& name, std::string& error);
	void openFolder(Kind kind);

	// note skins: loads every custom note skin not loaded yet into the note skin list (built-ins are
	// 0 and 1, custom ones follow in list order)
	void loadNoteSkins();

	// note effects: profile 0 / 1 are the built-in normal / reduced sets, 2+ the custom effect skins
	int effectProfileCount();
	std::string effectProfileName(int profile);
	// the folder a profile's file comes from: the custom skin's when it has the file, else the default's
	std::string effectFile(int profile, const std::string& fileName);

	// note sounds: Audio's last profile slot plays the chosen custom sound set (config.customSoundSet)
	std::string soundDir(size_t profileIndex);
	// a custom set's file for `name` (se_live_perfect), or the default set's when the set has none
	std::string soundFile(size_t profileIndex, const std::string& name);
}
