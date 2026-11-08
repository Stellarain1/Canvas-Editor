#pragma once
#include "../Core/ExportTargets.h"
#include "../../Score.h"
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace MikuMikuWorld
{
	// File > Publish To: send the chart (or every difficulty of the song) to a user-defined
	// export target, and File > Publish To > Manage Targets to set those up. No target exists
	// until the user adds one.
	class PublishWindow
	{
	public:
		using Charts = std::vector<std::pair<std::string, Score>>;	// difficulty name -> chart

		void openManager();
		void publish(const std::string& targetName, Charts charts);
		void update();

		// enabled targets, for the menu
		std::vector<std::string> targetNames() const;
		bool isBusy() const { return running; }

	private:
		struct Result
		{
			bool ok{ true };
			std::string log;
		};

		std::vector<Canvas::ExportTarget> targets;
		bool loaded{ false };
		bool managerRequested{ false };
		bool resultRequested{ false };
		int selected{ -1 };

		bool running{ false };
		std::future<Result> job;
		Result result;
		std::string runningLabel;

		void load();
		void save();
		void drawManager();
		void drawTargetEditor(Canvas::ExportTarget& target);
	};
}
