#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace MikuMikuWorld
{
	class Texture;
}

// The five cards the Score Preview's live UI uses: their icons in the skill panel and the co-op
// members row, the art their skill cut-in slides in, the line they say with it, and their skill.
// Out of the box they come from the asset pack (res/sekai/cards); Settings > Canvas > Live Cards
// swaps any of them for your own images and sound, or for a card from sekai.best.
namespace Canvas::Preview
{
	enum class SkillKind { Score = 0, Judgment = 1, Life = 2 };

	struct LiveCard
	{
		std::string name;
		std::string thumbnail, cutin, voice;	// full paths; empty = none
		SkillKind skill{ SkillKind::Score };
		std::string value;		// what the skill panel shows after "Lv.4", like "120%"
		int rarity{ 4 };		// 1-4, 5 = birthday
		bool trained{ true };
		bool trimmed{ true };	// the cut-in art has a see-through background
		std::string source;		// the sekai.best link it came from
		bool custom{ false };	// the user's, not the asset pack's
	};

	inline constexpr int DECK_SIZE = 5;

	// slot `index` (wraps around, so skill n uses card n % 5)
	const LiveCard& deckCard(int index);
	LiveCard& editDeckCard(int index);
	void saveDeck();
	void resetDeckCard(int index);
	// bumps whenever a card changes, so textures and sounds made from the cards reload
	int deckRevision();
	void touchDeck();
	// <user data>/cards
	std::string userCardsFolder();

	enum class CardArt { Thumbnail, Cutin };
	const MikuMikuWorld::Texture* cardTexture(int index, CardArt art);
	void clearCardTextures();

	// Copies a picked file (an image, or a sound for the voice) into the slot's folder.
	bool setCardFile(int index, CardArt art, const std::string& file, std::string& error);
	bool setCardVoice(int index, const std::string& file, std::string& error);

	// "Get from sekai.best": a card's icon, cut-in art, voice and skill, downloaded with curl on a
	// worker thread. Takes a link like https://sekai.best/card/1484 or just the number.
	class CardImport
	{
	public:
		~CardImport();
		bool start(int slot, const std::string& linkOrNumber, bool trained);
		bool busy() const { return running; }
		int slot() const { return importSlot; }
		// call every frame: puts a finished card into the deck; the progress or result text
		std::string poll();

	private:
		void run(int slot, int cardId, bool trained);
		std::thread worker;
		std::atomic<bool> running{ false };
		std::mutex mutex;
		std::string status;
		bool finished{ false }, succeeded{ false };
		LiveCard result;
		int importSlot{ -1 };
	};
}
