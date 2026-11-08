#include "LiveCards.h"
#include "../../Application.h"
#include "../../IO.h"
#include "../../File.h"
#include "../../Rendering/Texture.h"
#include "stb_image.h"
#include <json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;
using namespace MikuMikuWorld;

namespace Canvas::Preview
{
	namespace
	{
		constexpr const char* STORAGE = "https://storage.sekai.best/sekai-jp-assets/";
		constexpr const char* MASTER = "https://sekai-world.github.io/sekai-master-db-diff/";

		struct Deck
		{
			std::array<LiveCard, DECK_SIZE> bundled, user;
			std::array<bool, DECK_SIZE> hasUser{};
			bool loaded{ false };
			int revision{ 1 };
		};

		Deck& deckState()
		{
			static Deck deck;
			return deck;
		}

		int wrap(int index) { return ((index % DECK_SIZE) + DECK_SIZE) % DECK_SIZE; }

		std::string slotFolder(int index) { return userCardsFolder() + "slot" + std::to_string(wrap(index) + 1); }

		SkillKind kindOf(const std::string& name)
		{
			if (name == "judgment") return SkillKind::Judgment;
			if (name == "life") return SkillKind::Life;
			return SkillKind::Score;
		}

		const char* kindName(SkillKind kind)
		{
			switch (kind)
			{
			case SkillKind::Judgment: return "judgment";
			case SkillKind::Life: return "life";
			default: return "score";
			}
		}

		// thumbnail.png / cutin.jpg / voice.mp3...: whatever extension the file has
		std::string findArt(const fs::path& folder, const std::string& stem, std::initializer_list<const char*> extensions)
		{
			std::error_code error;
			for (const char* extension : extensions)
			{
				const fs::path path = folder / fs::u8path(stem + extension);
				if (fs::is_regular_file(path, error))
					return path.u8string();
			}
			return {};
		}

		const std::initializer_list<const char*> IMAGE_EXTENSIONS{ ".png", ".jpg", ".jpeg", ".bmp", ".tga" };
		const std::initializer_list<const char*> SOUND_EXTENSIONS{ ".mp3", ".wav", ".ogg", ".flac" };

		void readArt(LiveCard& card, const fs::path& folder)
		{
			card.thumbnail = findArt(folder, "thumbnail", IMAGE_EXTENSIONS);
			card.cutin = findArt(folder, "cutin", IMAGE_EXTENSIONS);
			card.voice = findArt(folder, "voice", SOUND_EXTENSIONS);
		}

		LiveCard cardFromJson(const nlohmann::json& j, const fs::path& base, bool custom)
		{
			LiveCard card;
			if (!j.is_object())
				return card;
			card.name = j.value("name", std::string{});
			card.skill = kindOf(j.value("skill", std::string("score")));
			card.value = j.value("value", std::string{});
			card.rarity = j.value("rarity", 4);
			card.trained = j.value("trained", true);
			card.trimmed = j.value("trimmed", true);
			card.source = j.value("source", std::string{});
			card.custom = custom;
			readArt(card, base / fs::u8path(j.value("folder", std::string{})));
			return card;
		}

		nlohmann::json readJson(const fs::path& path)
		{
			std::ifstream in(path);
			return in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
		}

		void load()
		{
			Deck& d = deckState();
			if (d.loaded)
				return;
			d.loaded = true;
			const fs::path bundled = fs::u8path(Application::getAppDir() + "res/sekai/cards");
			const nlohmann::json pack = readJson(bundled / "deck.json");
			if (pack.contains("cards") && pack["cards"].is_array())
				for (int i = 0; i < DECK_SIZE && i < static_cast<int>(pack["cards"].size()); ++i)
					d.bundled[i] = cardFromJson(pack["cards"][i], bundled, false);
			for (int i = 0; i < DECK_SIZE; ++i)
				if (d.bundled[i].name.empty())
					d.bundled[i].name = "Card " + std::to_string(i + 1);

			const nlohmann::json mine = readJson(fs::u8path(userCardsFolder()) / "deck.json");
			if (mine.contains("cards") && mine["cards"].is_array())
				for (int i = 0; i < DECK_SIZE && i < static_cast<int>(mine["cards"].size()); ++i)
					if (mine["cards"][i].is_object())
					{
						d.user[i] = cardFromJson(mine["cards"][i], fs::u8path(userCardsFolder()), true);
						d.hasUser[i] = true;
					}
		}

		// does the image have see-through pixels along its edges (a cut-out, not a scene)?
		bool hasTransparentEdges(const std::string& file)
		{
			int w = 0, h = 0, channels = 0;
			unsigned char* data = stbi_load(file.c_str(), &w, &h, &channels, 4);
			if (!data)
				return false;
			int clear = 0, total = 0;
			for (int x = 0; x < w; x += std::max(1, w / 32))
				for (int y : { 0, h - 1 })
				{
					clear += data[(static_cast<size_t>(y) * w + x) * 4 + 3] < 128;
					++total;
				}
			stbi_image_free(data);
			return total > 0 && clear * 2 >= total;
		}

		bool download(const std::string& url, const fs::path& path)
		{
			std::error_code error;
			fs::remove(path, error);
			int exitCode = -1;
			IO::runProcess("curl -L -f -s -S --compressed -o " + IO::shellQuote(path.u8string()) + " " + IO::shellQuote(url), &exitCode);
			return exitCode == 0 && fs::is_regular_file(path, error) && fs::file_size(path, error) > 0;
		}

		std::string readAll(const fs::path& path)
		{
			std::ifstream in(path, std::ios::binary);
			std::ostringstream text;
			text << in.rdbuf();
			return text.str();
		}

		// cards.json is ~35 MB: walk its top-level objects and parse only the one whose first key
		// is the wanted "id"
		nlohmann::json findCard(const std::string& text, int id)
		{
			static const std::regex idAtStart(R"re(^\{\s*"id"\s*:\s*(\d+))re");
			int depth = 0;
			bool inString = false, escaped = false;
			size_t start = 0;
			for (size_t i = 0; i < text.size(); ++i)
			{
				const char c = text[i];
				if (inString)
				{
					if (escaped) escaped = false;
					else if (c == '\\') escaped = true;
					else if (c == '"') inString = false;
					continue;
				}
				if (c == '"')
					inString = true;
				else if (c == '{' || c == '[')
				{
					if (c == '{' && depth == 1)
						start = i;
					++depth;
				}
				else if (c == '}' || c == ']')
				{
					--depth;
					if (c == '}' && depth == 1)
					{
						std::smatch match;
						const std::string head = text.substr(start, std::min<size_t>(64, i - start + 1));
						if (std::regex_search(head, match, idAtStart) && std::stoi(match[1].str()) == id)
							return nlohmann::json::parse(text.begin() + start, text.begin() + i + 1, nullptr, false);
					}
				}
			}
			return nlohmann::json();
		}

		bool olderThan(const fs::path& path, std::chrono::hours age)
		{
			std::error_code error;
			if (!fs::is_regular_file(path, error))
				return true;
			const auto written = fs::last_write_time(path, error);
			return error || fs::file_time_type::clock::now() - written > age;
		}

		std::map<std::string, std::shared_ptr<Texture>>& textures()
		{
			static std::map<std::string, std::shared_ptr<Texture>> cache;
			return cache;
		}
	}

	std::string userCardsFolder()
	{
		return Application::getUserDataDir() + "cards/";
	}

	const LiveCard& deckCard(int index)
	{
		load();
		const Deck& d = deckState();
		const int i = wrap(index);
		return d.hasUser[i] ? d.user[i] : d.bundled[i];
	}

	LiveCard& editDeckCard(int index)
	{
		load();
		Deck& d = deckState();
		const int i = wrap(index);
		if (!d.hasUser[i])
		{
			// the first edit copies the pack's card (and its files) into the slot's own folder
			d.user[i] = d.bundled[i];
			d.user[i].custom = true;
			const fs::path folder = fs::u8path(slotFolder(i));
			std::error_code error;
			fs::remove_all(folder, error);
			fs::create_directories(folder, error);
			for (std::string* file : { &d.user[i].thumbnail, &d.user[i].cutin, &d.user[i].voice })
			{
				if (file->empty())
					continue;
				const fs::path from = fs::u8path(*file);
				const fs::path to = folder / from.filename();
				fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
				*file = error ? std::string{} : to.u8string();
			}
			d.hasUser[i] = true;
		}
		return d.user[i];
	}

	void saveDeck()
	{
		load();
		const Deck& d = deckState();
		nlohmann::ordered_json json;
		json["format"] = "canvas-editor/deck";
		json["cards"] = nlohmann::ordered_json::array();
		for (int i = 0; i < DECK_SIZE; ++i)
		{
			if (!d.hasUser[i])
			{
				json["cards"].push_back(nullptr);
				continue;
			}
			const LiveCard& c = d.user[i];
			json["cards"].push_back({ { "name", c.name }, { "folder", "slot" + std::to_string(i + 1) }, { "skill", kindName(c.skill) },
				{ "value", c.value }, { "rarity", c.rarity }, { "trained", c.trained }, { "trimmed", c.trimmed }, { "source", c.source } });
		}
		std::error_code error;
		fs::create_directories(fs::u8path(userCardsFolder()), error);
		std::ofstream out(fs::u8path(userCardsFolder()) / "deck.json");
		out << json.dump(1);
	}

	void resetDeckCard(int index)
	{
		load();
		Deck& d = deckState();
		const int i = wrap(index);
		d.hasUser[i] = false;
		d.user[i] = LiveCard{};
		std::error_code error;
		fs::remove_all(fs::u8path(slotFolder(i)), error);
		saveDeck();
		touchDeck();
	}

	int deckRevision() { return deckState().revision; }

	void touchDeck()
	{
		++deckState().revision;
		clearCardTextures();
	}

	const Texture* cardTexture(int index, CardArt art)
	{
		const LiveCard& card = deckCard(index);
		const std::string& path = art == CardArt::Thumbnail ? card.thumbnail : card.cutin;
		if (path.empty())
			return nullptr;
		auto& cache = textures();
		auto it = cache.find(path);
		if (it == cache.end())
			it = cache.emplace(path, std::make_shared<Texture>(path, Texture::Smooth)).first;
		return it->second && it->second->getID() ? it->second.get() : nullptr;
	}

	void clearCardTextures()
	{
		for (auto& [path, texture] : textures())
			if (texture)
				texture->dispose();
		textures().clear();
	}

	namespace
	{
		// copies `file` into the slot as <stem><extension>, replacing what was there
		std::string copyIntoSlot(int index, const std::string& stem, const std::string& file, std::initializer_list<const char*> kinds, std::string& error)
		{
			const fs::path from = fs::u8path(file);
			std::string extension = from.extension().u8string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (std::find_if(kinds.begin(), kinds.end(), [&](const char* k) { return extension == k; }) == kinds.end())
			{
				error = "That file type is not supported.";
				return {};
			}
			editDeckCard(index);
			const fs::path folder = fs::u8path(slotFolder(index));
			std::error_code ec;
			fs::create_directories(folder, ec);
			for (const char* k : kinds)
				fs::remove(folder / fs::u8path(stem + k), ec);
			const fs::path to = folder / fs::u8path(stem + extension);
			fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				error = ec.message();
				return {};
			}
			return to.u8string();
		}
	}

	bool setCardFile(int index, CardArt art, const std::string& file, std::string& error)
	{
		const std::string copied = copyIntoSlot(index, art == CardArt::Thumbnail ? "thumbnail" : "cutin", file, IMAGE_EXTENSIONS, error);
		if (copied.empty())
			return false;
		LiveCard& card = editDeckCard(index);
		if (art == CardArt::Thumbnail)
			card.thumbnail = copied;
		else
		{
			card.cutin = copied;
			card.trimmed = hasTransparentEdges(copied);
		}
		saveDeck();
		touchDeck();
		return true;
	}

	bool setCardVoice(int index, const std::string& file, std::string& error)
	{
		const std::string copied = copyIntoSlot(index, "voice", file, SOUND_EXTENSIONS, error);
		if (copied.empty())
			return false;
		editDeckCard(index).voice = copied;
		saveDeck();
		touchDeck();
		return true;
	}

	// ---- sekai.best ------------------------------------------------------------------------------

	CardImport::~CardImport()
	{
		if (worker.joinable())
			worker.join();
	}

	bool CardImport::start(int slot, const std::string& linkOrNumber, bool trained)
	{
		if (running)
			return false;
		// the last number in the text: https://sekai.best/card/1484, "1484", "card 1484"...
		static const std::regex number(R"((\d+)\D*$)");
		std::smatch match;
		if (!std::regex_search(linkOrNumber, match, number))
		{
			std::lock_guard lock(mutex);
			status = "Paste a sekai.best card link, like https://sekai.best/card/1484";
			return false;
		}
		if (!IO::commandExists("curl"))
		{
			std::lock_guard lock(mutex);
			status = "curl is needed to download cards.";
			return false;
		}
		if (worker.joinable())
			worker.join();
		const int id = std::stoi(match[1].str());
		{
			std::lock_guard lock(mutex);
			status = "Looking up card " + std::to_string(id) + "...";
			finished = succeeded = false;
		}
		importSlot = wrap(slot);
		running = true;
		worker = std::thread(&CardImport::run, this, importSlot, id, trained);
		return true;
	}

	void CardImport::run(int slot, int cardId, bool trained)
	{
		auto say = [&](const std::string& text) {
			std::lock_guard lock(mutex);
			status = text;
		};
		auto fail = [&](const std::string& text) {
			std::lock_guard lock(mutex);
			status = text;
			finished = true;
			succeeded = false;
			running = false;
		};

		const fs::path cache = fs::u8path(userCardsFolder() + "cache");
		std::error_code error;
		fs::create_directories(cache, error);

		// the card tables, kept for a while (cards.json is the big one)
		bool freshCards = false;
		auto table = [&](const char* name, bool force) {
			const fs::path path = cache / (std::string(name) + ".json");
			if (force || olderThan(path, std::chrono::hours(12)))
			{
				if (!download(std::string(MASTER) + name + ".json", path))
					return false;
				freshCards |= std::string(name) == "cards";
			}
			return fs::is_regular_file(path, error);
		};
		say("Downloading the card list...");
		if (!table("cards", false))
			return fail("Could not download the card list from sekai.best.");
		nlohmann::json card = findCard(readAll(cache / "cards.json"), cardId);
		if (!card.is_object() && !freshCards && table("cards", true))
			card = findCard(readAll(cache / "cards.json"), cardId);
		if (!card.is_object())
			return fail("Card " + std::to_string(cardId) + " is not on sekai.best.");
		table("skills", false);
		table("gameCharacters", false);

		const std::string bundle = card.value("assetbundleName", std::string{});
		const std::string rarity = card.value("cardRarityType", std::string{});
		if (bundle.empty())
			return fail("Card " + std::to_string(cardId) + " has no art listed.");
		const bool trainable = rarity == "rarity_3" || rarity == "rarity_4";
		const std::string art = trained && trainable ? "after_training" : "normal";
		const std::string otherArt = art == "normal" ? "after_training" : "normal";

		const fs::path folder = fs::u8path(slotFolder(slot) + ".new");
		fs::remove_all(folder, error);
		fs::create_directories(folder, error);
		say("Downloading the card's art...");
		// some cards only come trained (and new ones have no trimmed cut-out yet)
		bool haveThumb = download(STORAGE + std::string("thumbnail/chara/") + bundle + "_" + art + ".png", folder / "thumbnail.png") ||
			download(STORAGE + std::string("thumbnail/chara/") + bundle + "_" + otherArt + ".png", folder / "thumbnail.png");
		bool trimmed = false, haveCutin = false;
		// the cut-in shows the card's scene (member_cutout), like the game's 2D cut-in
		for (const auto& [kind, which] : { std::pair{ "member_cutout", art }, std::pair{ "member_cutout", otherArt },
			std::pair{ "member_cutout_trm", art }, std::pair{ "member_cutout_trm", otherArt } })
		{
			if (download(STORAGE + std::string("character/") + kind + "/" + bundle + "/" + which + ".png", folder / "cutin.png"))
			{
				haveCutin = true;
				trimmed = std::string(kind) == "member_cutout_trm";
				break;
			}
		}
		if (!haveThumb && !haveCutin)
		{
			fs::remove_all(folder, error);
			return fail("sekai.best has no art for card " + std::to_string(cardId) + " yet.");
		}
		say("Downloading the voice...");
		if (card.value("gachaPhrase", std::string("-")) != "-")
			download(STORAGE + std::string("sound/gacha/get_voice/") + bundle + "/" + bundle + ".mp3", folder / "voice.mp3");

		LiveCard made;
		made.custom = true;
		made.trained = art == "after_training";
		made.trimmed = trimmed;
		made.rarity = rarity == "rarity_1" ? 1 : rarity == "rarity_2" ? 2 : rarity == "rarity_3" ? 3 : rarity == "rarity_4" ? 4 : 5;
		made.source = "https://sekai.best/card/" + std::to_string(cardId);

		// skill: what it does (score up, judgement, life) and its best score-up at level 4
		const nlohmann::json skills = readJson(cache / "skills.json");
		const int skillId = card.value("skillId", 0);
		if (skills.is_array())
			for (const auto& skill : skills)
				if (skill.value("id", -1) == skillId)
				{
					const std::string sprite = skill.value("descriptionSpriteName", std::string{});
					made.skill = sprite == "judgment_up" ? SkillKind::Judgment : sprite == "life_recovery" ? SkillKind::Life : SkillKind::Score;
					int best = 0;
					if (skill.contains("skillEffects") && skill["skillEffects"].is_array())
						for (const auto& effect : skill["skillEffects"])
							if (effect.value("skillEffectType", std::string{}).rfind("score_up", 0) == 0 && effect.contains("skillEffectDetails"))
								for (const auto& detail : effect["skillEffectDetails"])
									if (detail.value("level", 0) == 4)
										best = std::max(best, detail.value("activateEffectValue", 0));
					if (best > 0)
						made.value = std::to_string(best) + "%";
				}
		std::string character;
		const nlohmann::json characters = readJson(cache / "gameCharacters.json");
		if (characters.is_array())
			for (const auto& c : characters)
				if (c.value("id", -1) == card.value("characterId", 0))
					character = c.value("firstName", std::string{}) + c.value("givenName", std::string{});
		made.name = (character.empty() ? std::string("Card ") + std::to_string(cardId) : character) + " - " + card.value("prefix", std::string{});

		// the new folder replaces the slot's
		const fs::path target = fs::u8path(slotFolder(slot));
		fs::remove_all(target, error);
		fs::rename(folder, target, error);
		if (error)
			return fail("Could not save the card: " + error.message());
		readArt(made, target);

		std::lock_guard lock(mutex);
		result = made;
		status = "Added " + made.name;
		finished = succeeded = true;
		running = false;
	}

	std::string CardImport::poll()
	{
		bool apply = false;
		LiveCard made;
		std::string text;
		{
			std::lock_guard lock(mutex);
			if (finished)
			{
				finished = false;
				apply = succeeded;
				made = result;
			}
			text = status;
		}
		if (apply)
		{
			if (worker.joinable())
				worker.join();
			load();
			Deck& d = deckState();
			d.user[importSlot] = made;
			d.hasUser[importSlot] = true;
			saveDeck();
			touchDeck();
		}
		return text;
	}
}
