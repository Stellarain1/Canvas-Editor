#include "TestFramework.h"
#define CANVAS_INTRO_PURE
#include "Canvas/Preview/IntroOverlay.cpp"

using namespace Canvas::Preview;

namespace
{
	// vertex count of one frame of the intro; no text (it would need a font atlas), fake texture ids
	int vertices(float seconds, bool v1)
	{
		static ImGuiContext* ctx = ImGui::CreateContext();
		(void)ctx;
		ImDrawList list(ImGui::GetDrawListSharedData());
		list._ResetForNewFrame();
		list.PushClipRect(ImVec2(0, 0), ImVec2(1920, 1080), false);
		IntroInfo info;
		info.difficulty = "";
		info.jacket = (ImTextureID)1;
		info.gradient = (ImTextureID)2;
		info.sheet = (ImTextureID)3;
		const bool covered = drawIntro(&list, ImVec2(0, 0), ImVec2(1920, 1080), seconds, info, v1);
		return covered ? list.VtxBuffer.Size : -1;
	}
}

TEST(intro_timeline_matches_the_exo)
{
	CHECK(introState(0.0f).flash > 0.99f);
	CHECK(introState(1.0f).flash < 0.01f);			// frame 60: white flash done
	CHECK(introState(4.0f).fade > 0.99f);			// frame 240
	CHECK(introState(4.75f).fade < 0.51f && introState(4.75f).fade > 0.49f);	// frame 285: half way out
	CHECK(introState(0.5f).sweep < 0.0f);			// the streak starts at frame 61
	CHECK(introState(1.5f).sweep > 0.0f);
	CHECK(introState(4.0f).sweep > 0.0f);			// second sweep
	CHECK(introState(1.9f).drift > 0.99f);			// frame 112: tile settled
	CHECK(introState(0.5f).tilt > -0.01f);			// 2.8.0 tile turns only after frame 45
	CHECK(introState(1.0f).tilt < -4.5f);
}

TEST(intro_draws_only_while_it_plays)
{
	CHECK(vertices(-0.1f, false) < 0);
	CHECK(vertices(5.0f, false) < 0);
	CHECK(vertices(5.5f, true) < 0);
	for (bool v1 : { false, true })
	{
		const int a = vertices(0.5f, v1), b = vertices(2.0f, v1), c = vertices(4.0f, v1);
		CHECK(a > 0);
		CHECK(b > 0);
		CHECK(c > 0);
	}
}
