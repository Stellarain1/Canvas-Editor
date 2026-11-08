#pragma once
#include "../../ScoreContext.h"
#include <array>

namespace MikuMikuWorld
{
	// Hi-speed (soflan) layers: which layer new notes / hi-speed changes go to,
	// show/hide, rename, add, remove, and moving the selection between layers.
	class LayersWindow
	{
	public:
		void update(ScoreContext& context);

	private:
		int renamingLayer{ -1 };
		std::array<char, 64> renameBuffer{};

		void removeLayer(ScoreContext& context, int layer);
	};
}
