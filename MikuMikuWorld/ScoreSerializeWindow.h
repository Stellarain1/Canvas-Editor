#pragma once
#include "ScoreSerializer.h"
#include "ScoreContext.h"
#include "Canvas/Core/ChartIO.h"
#include <optional>

namespace MikuMikuWorld
{
	// Export: pick a format (or take it from the file name), save through Canvas::saveChart,
	// and report anything the format could not keep.
	class DefaultScoreSerializeController : public ScoreSerializeController
	{
		static bool openFileDialog(Canvas::ExportFormat format, const std::string& suggestedName, std::string& filename);
	public:
		DefaultScoreSerializeController(Score score, std::string suggestedName = {});
		DefaultScoreSerializeController(Score score, const std::string& filename, Canvas::ExportFormat format);

		SerializeResult update() override;
	private:
		std::optional<Canvas::ExportFormat> format;
		int selectedFormat{ -1 };
		std::string suggestedName;
	};

	// Import: the format is detected from the file contents (Canvas::loadChart).
	class DefaultScoreDeserializeController : public ScoreSerializeController
	{
	public:
		DefaultScoreDeserializeController(const std::string& filename);

		SerializeResult update() override;
	};

	class ScoreEditor;

	class ScoreSerializeWindow
	{
	public:
		ScoreSerializeWindow() = default;
		void update(ScoreEditor& editor, ScoreContext& context, ScoreEditorTimeline& timeline);
		void serialize(const ScoreContext& context);
		void serialize(const ScoreContext& context, const std::string& filename, Canvas::ExportFormat format);
		void deserialize(const std::string& filename);
	private:
		std::unique_ptr<ScoreSerializeController> controller{};
	};
}
