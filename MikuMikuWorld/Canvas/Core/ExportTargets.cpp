#include "ExportTargets.h"
#include "../../IO.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace Canvas
{
	namespace
	{
		std::string formatKey(ExportFormat format)
		{
			switch (format)
			{
			case ExportFormat::Mmws: return "mmws";
			case ExportFormat::MmwsExtended: return "mmws-v7";
			case ExportFormat::CcMmws: return "ccmmws";
			case ExportFormat::Sus: return "sus";
			case ExportFormat::SusNoNoteSpeed: return "sus-no-speed";
			case ExportFormat::MusicScoreJson: return "json";
			case ExportFormat::MusicScoreJsonBase64: return "json-base64";
			case ExportFormat::Usc: return "usc";
			case ExportFormat::LevelDataNextSekai: return "leveldata";
			default: return "sus";
			}
		}
	}

	const char* exportTargetKindName(ExportTargetKind kind)
	{
		switch (kind)
		{
		case ExportTargetKind::Folder: return "folder";
		case ExportTargetKind::Command: return "command";
		case ExportTargetKind::Http: return "http";
		default: return "folder";
		}
	}

	nlohmann::json ExportTarget::toJson() const
	{
		nlohmann::json json{
			{ "name", name },
			{ "kind", exportTargetKindName(kind) },
			{ "format", formatKey(format) },
			{ "enabled", enabled },
		};

		switch (kind)
		{
		case ExportTargetKind::Folder:
			json["folder"] = folder;
			json["file_name"] = fileNameTemplate;
			break;
		case ExportTargetKind::Command:
			json["command"] = commandTemplate;
			break;
		case ExportTargetKind::Http:
			json["url"] = url;
			json["method"] = method;
			json["multipart"] = multipart;
			json["field"] = fieldName;
			json["headers"] = headers;
			json["fields"] = fields;
			break;
		}
		return json;
	}

	ExportTarget ExportTarget::fromJson(const nlohmann::json& json)
	{
		ExportTarget target;
		if (!json.is_object())
			return target;

		target.name = json.value("name", "");
		const std::string kind = json.value("kind", "folder");
		target.kind = kind == "command" ? ExportTargetKind::Command : kind == "http" ? ExportTargetKind::Http : ExportTargetKind::Folder;
		target.format = exportFormatFromName(json.value("format", "sus")).value_or(ExportFormat::Sus);
		target.enabled = json.value("enabled", true);
		target.folder = json.value("folder", "");
		target.fileNameTemplate = json.value("file_name", target.fileNameTemplate);
		target.commandTemplate = json.value("command", "");
		target.url = json.value("url", "");
		target.method = json.value("method", "POST");
		target.multipart = json.value("multipart", true);
		target.fieldName = json.value("field", "file");
		if (json.contains("headers") && json["headers"].is_array())
			for (const auto& header : json["headers"])
				if (header.is_string())
					target.headers.push_back(header.get<std::string>());
		if (json.contains("fields") && json["fields"].is_object())
			for (const auto& [key, value] : json["fields"].items())
				if (value.is_string())
					target.fields[key] = value.get<std::string>();
		return target;
	}

	std::vector<ExportTarget> readExportTargets(const nlohmann::json& json)
	{
		std::vector<ExportTarget> targets;
		if (json.is_array())
			for (const auto& entry : json)
				targets.push_back(ExportTarget::fromJson(entry));
		return targets;
	}

	nlohmann::json writeExportTargets(const std::vector<ExportTarget>& targets)
	{
		nlohmann::json json = nlohmann::json::array();
		for (const auto& target : targets)
			json.push_back(target.toJson());
		return json;
	}

	std::string expandTemplate(const std::string& text, const TemplateVars& vars, bool quote)
	{
		std::string out;
		for (size_t i = 0; i < text.size();)
		{
			if (text[i] == '{')
			{
				const size_t close = text.find('}', i + 1);
				if (close != std::string::npos)
				{
					const std::string key = text.substr(i + 1, close - i - 1);
					auto it = vars.find(key);
					if (it != vars.end())
					{
						out += quote ? IO::shellQuote(it->second) : it->second;
						i = close + 1;
						continue;
					}
				}
			}
			out += text[i++];
		}
		return out;
	}

	std::string sanitizeFileName(const std::string& name)
	{
		std::string out;
		for (unsigned char c : name)
			out += (c < 32 || std::string("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string::npos) ? '_' : static_cast<char>(c);
		while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
			out.pop_back();
		return out.empty() ? "chart" : out;
	}

	PublishPlan planPublish(const ExportTarget& target, const TemplateVars& baseVars, const std::string& tempDir, const std::string& curl)
	{
		TemplateVars vars = baseVars;
		vars["ext"] = exportFormatExtension(target.format);
		vars["format"] = exportFormatName(target.format);

		// the exported file keeps a readable name, also when it only lives in the temp folder
		TemplateVars fileVars;
		for (const auto& [key, value] : vars)
			fileVars[key] = sanitizeFileName(value);
		const std::string fileName = sanitizeFileName(expandTemplate(target.fileNameTemplate, fileVars));

		PublishPlan plan;
		if (target.kind == ExportTargetKind::Folder)
		{
			plan.finalPath = (fs::u8path(target.folder) / fs::u8path(fileName)).u8string();
			plan.exportPath = plan.finalPath;
			return plan;
		}

		plan.exportPath = (fs::u8path(tempDir) / fs::u8path(fileName)).u8string();
		vars["file"] = plan.exportPath;
		vars["filename"] = fileName;

		if (target.kind == ExportTargetKind::Command)
		{
			plan.command = expandTemplate(target.commandTemplate, vars, true);
			return plan;
		}

		// http via curl: -f fails on HTTP errors, response body is shown to the user
		std::string cmd = IO::shellQuote(curl) + " -sS -f --max-time 120";
		cmd += " -X " + (target.method == "PUT" ? std::string("PUT") : std::string("POST"));
		for (const auto& header : target.headers)
			cmd += " -H " + IO::shellQuote(expandTemplate(header, vars));
		if (target.multipart)
		{
			cmd += " -F " + IO::shellQuote((target.fieldName.empty() ? std::string("file") : target.fieldName) + "=@" + plan.exportPath);
			// --form-string: a value starting with @ or < must not make curl read a local file
			for (const auto& [key, value] : target.fields)
				cmd += " --form-string " + IO::shellQuote(key + "=" + expandTemplate(value, vars));
		}
		else
		{
			cmd += " --data-binary " + IO::shellQuote("@" + plan.exportPath);
		}
		cmd += " " + IO::shellQuote(expandTemplate(target.url, vars));
		plan.command = cmd;
		return plan;
	}
}
