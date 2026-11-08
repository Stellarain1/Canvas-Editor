#pragma once
#include <string>

struct ImFont;

// "What are you Charting today?": the start screen (it replaced the "Welcome to Canvas." intro).
// Three cards pick how the editor works for this session:
//   Extended Score  layers (hi-speed groups), guide colours, MMW4CC / UntitledCharts formats
//   Normal MMW      the classic MikuMikuWorld tools; saves files every MMW opens
//   Dynamic Stages  Next SEKAI: several stages that move, turn and fade, and a camera
// Chart > Charting Mode switches later. The choice lives in config.chartMode.
namespace Canvas::Sekai
{
	enum class ChartMode : int { Extended = 0, NormalMmw = 1, DynamicStages = 2 };
	constexpr int CHART_MODE_COUNT = 3;

	const char* chartModeName(ChartMode mode);

	class ModePicker
	{
	public:
		void open() { visible = true; openedAt = -1.0; }
		bool isOpen() const { return visible; }
		// Draws over everything; returns true on the frame a mode was picked (read `picked`).
		// `openRequested` is set when the user asked to open a chart file instead.
		bool update(ImFont* titleFont, bool sounds, float volume);

		ChartMode picked{ ChartMode::Extended };
		bool openRequested{ false };

	private:
		bool visible{ false };
		double openedAt{ -1.0 };
		float hover[3]{};
	};
}
