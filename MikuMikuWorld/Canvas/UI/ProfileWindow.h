#pragma once
#include "../Core/ProfileLink.h"
#include <future>
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	// Optional profile link (Settings > Canvas > Profile Link, off by default): browse the
	// levels on a Sonolus-compatible server (filtered by the profile's keywords, usually the
	// author name) and pull one into the editor. Everything runs in the background; without a
	// network the window just says so and editing is unaffected.
	class ProfileWindow
	{
	public:
		void open();
		// Returns the path of a downloaded chart to open, once one is ready.
		std::string update();

	private:
		struct ListResult
		{
			bool ok{ false };
			std::string error;
			Canvas::RemoteLevelPage page;
		};

		struct PullResult
		{
			bool ok{ false };
			std::string error;
			std::string chartPath;
		};

		bool openRequested{ false };
		bool closeRequested{ false };
		std::string search;
		int page{ 0 };
		Canvas::RemoteLevelPage levels;
		std::string status;
		std::future<ListResult> listJob;
		std::future<PullResult> pullJob;
		std::string pulling;

		void requestList();
		void requestPull(const Canvas::RemoteLevel& level);
	};
}
