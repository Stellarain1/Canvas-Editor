#pragma once
#include "ChartIO.h"
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

// Export targets ("publish to..."): user-defined places a chart can be sent with one click.
// Canvas ships with none; every target is something the user (or a private deployment's config)
// adds. Three kinds:
//
//   folder   copy the exported chart into a folder, file name from a template
//   command  run a command line with the exported file, e.g. scp / rsync / a build script
//   http     upload the file with curl (POST or PUT, multipart or raw body, extra headers)
//
// Templates can use {file} {filename} {title} {artist} {designer} {difficulty} {format} {ext}
// {date} {time}. Stored under "canvas" -> "export_targets" in app_config.json.
namespace Canvas
{
	enum class ExportTargetKind
	{
		Folder,
		Command,
		Http
	};

	struct ExportTarget
	{
		std::string name;
		ExportTargetKind kind{ ExportTargetKind::Folder };
		ExportFormat format{ ExportFormat::Sus };
		bool enabled{ true };

		// folder
		std::string folder;
		std::string fileNameTemplate{ "{title} - {difficulty}.{ext}" };

		// command
		std::string commandTemplate;

		// http
		std::string url;
		std::string method{ "POST" };		// POST or PUT
		bool multipart{ true };				// multipart/form-data with the file as `fieldName`
		std::string fieldName{ "file" };
		std::vector<std::string> headers;	// "Name: value", templates allowed
		std::map<std::string, std::string> fields;	// extra form fields, templates allowed

		nlohmann::json toJson() const;
		static ExportTarget fromJson(const nlohmann::json& json);
	};

	const char* exportTargetKindName(ExportTargetKind kind);

	std::vector<ExportTarget> readExportTargets(const nlohmann::json& json);
	nlohmann::json writeExportTargets(const std::vector<ExportTarget>& targets);

	using TemplateVars = std::map<std::string, std::string>;

	// {name} -> value; unknown names are left as they are. `quote` shell-quotes every value
	// (for command templates).
	std::string expandTemplate(const std::string& text, const TemplateVars& vars, bool quote = false);

	// Characters that are not allowed in file names become '_'.
	std::string sanitizeFileName(const std::string& name);

	struct PublishPlan
	{
		std::string exportPath;		// where the chart is written first
		std::string command;		// command to run afterwards (empty for folder targets)
		std::string finalPath;		// for folder targets
	};

	// Everything a target will do for one exported chart, without doing it.
	PublishPlan planPublish(const ExportTarget& target, const TemplateVars& vars, const std::string& tempDir,
		const std::string& curl = "curl");
}
