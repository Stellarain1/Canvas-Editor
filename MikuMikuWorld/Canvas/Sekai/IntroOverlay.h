#pragma once
#include "SekaiTransition.h"
#include <memory>
#include <string>

namespace MikuMikuWorld { class Texture; }

// The startup greeting: a SEKAI-style pop-up (the SEKAI transition burst, then the in-game
// "scene text" band sliding open with the text) that says Canvas::Branding::INTRO_TEXT.
//
// To change the words, edit INTRO_TEXT / INTRO_SUBTEXT in Canvas/CanvasBranding.h, or set
// Settings > Canvas > Intro text (no rebuild needed). Help > Replay Intro plays it again.
namespace Canvas::Sekai
{
	struct IntroOptions
	{
		std::string text;			// big line
		std::string subtext;		// small line under the band ("" = none)
		float duration{ 3.2f };		// seconds
		ImFont* font{ nullptr };	// FOT-Rodin when SEKAI assets are available, else the UI font
		bool sounds{ false };
		float volume{ 0.6f };
		std::string backgroundImage;	// optional full-screen image behind everything
	};

	class IntroOverlay
	{
	public:
		IntroOverlay();
		~IntroOverlay();

		void start(IntroOptions options);
		void skip();
		// Draws on top of everything; call every frame after the editor UI.
		void update();
		bool active() const { return running; }

	private:
		IntroOptions options;
		Transition transition;
		std::unique_ptr<MikuMikuWorld::Texture> background;
		std::string backgroundPath;
		double startTime{ -1.0 };
		double lastTime{ -1.0 };
		float endTime{ 0.0f };
		bool running{ false };
		bool transitionStarted{ false };
		bool playedOpen{ false }, playedClose{ false };
	};
}
