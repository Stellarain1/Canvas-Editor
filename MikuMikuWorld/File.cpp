#include "File.h"
#include "IO.h"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif
#ifdef __APPLE__
#include <AppKit/AppKit.h>
#endif
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <filesystem>
#include <sstream>

namespace IO
{
	FileDialogFilter mmwsFilter{ "MikuMikuWorld Score", "*.mmws" };
	FileDialogFilter ceFilter{ "Canvas Editor Chart", "*.ce" };
	FileDialogFilter susFilter{ "Sliding Universal Score", "*.sus" };
	FileDialogFilter lvlDatFilter{ "Sonolus Level Data", "*.json.gz" };
	FileDialogFilter customScoreJsonFilter{ "ChartMaker JSON", "*.json" };
	FileDialogFilter imageFilter{ "Image Files", "*.jpg;*.jpeg;*.png" };
	FileDialogFilter videoFilter{ "Video Files", "*.mp4;*.webm;*.mkv;*.mov;*.m4v;*.avi" };
	FileDialogFilter audioFilter{ "Audio Files", "*.mp3;*.wav;*.flac;*.ogg" };
	FileDialogFilter presetFilter{ "Notes Preset", "*.json" };
	FileDialogFilter allFilter{ "All Files", "*.*" };

	File::File(const std::string& filename, FileMode mode)
	{
		stream = std::make_unique<std::fstream>();
		open(filename, mode);
	}

	File::File(const std::wstring& filename, FileMode mode)
	{
		stream = std::make_unique<std::fstream>();
		open(filename, mode);
	}

	File::~File()
	{
		if (stream->is_open())
			stream->close();
	}

	std::ios_base::openmode File::getStreamMode(FileMode mode) const
	{
		switch (mode)
		{
		case FileMode::Read:
			return std::fstream::in;
		case FileMode::Write:
			return std::fstream::out;
		case FileMode::ReadBinary:
			return std::fstream::in | std::fstream::binary;
		case FileMode::WriteBinary:
			return std::fstream::out | std::fstream::binary;
		default:
			return {};
		}
	}

	void File::open(const std::string& filename, FileMode mode)
	{
		openFilename = filename;
		open(IO::mbToWideStr(filename), mode);
	}

	void File::open(const std::wstring& filename, FileMode mode)
	{
		openFilenameW = filename;
#ifdef _WIN32
		stream->open(std::filesystem::path(filename), getStreamMode(mode));
#else
		stream->open(wideStringToMb(filename), getStreamMode(mode));
#endif
	}

	void File::close()
	{
		openFilename.clear();
		openFilenameW.clear();
		stream->close();
	}

	void File::flush()
	{
		stream->flush();
	}

	std::vector<uint8_t> File::readAllBytes()
	{
		if (!stream->is_open())
			return {};

		stream->seekg(0, std::ios_base::end);
		size_t length = stream->tellg();
		stream->seekg(0, std::ios_base::beg);

		std::vector<uint8_t> bytes;
		bytes.resize(length);
		stream->read((char*)bytes.data(), length);

		return bytes;
	}

	std::string File::readLine()
	{
		if (!stream->is_open())
			return {};

		std::string line{};
		std::getline(*stream, line);
		return line;
	}

	std::vector<std::string> File::readAllLines()
	{
		if (!stream->is_open())
			return {};

		std::vector<std::string> lines;
		while (!stream->eof())
			lines.push_back(readLine());

		return lines;
	}

	std::string File::readAllText()
	{
		if (!stream->is_open())
			return {};

		std::stringstream buffer;
		buffer << stream->rdbuf();
		return buffer.str();
	}

	bool File::isEndofFile()
	{
		return stream->is_open() ? stream->eof() : true;
	}

	void File::write(const std::string& str)
	{
		if (stream->is_open())
		{
			stream->write(str.c_str(), str.length());
		}
	}

	void File::writeLine(const std::string line)
	{
		write(line + "\n");
	}

	void File::writeAllLines(const std::vector<std::string>& lines)
	{
		if (stream->is_open())
		{
			std::stringstream ss{};
			for (const auto& line : lines)
				ss << line + '\n';

			std::string allLines{ ss.str() };
			stream->write(allLines.c_str(), allLines.size());
		}
	}

	void File::writeAllBytes(const std::vector<uint8_t>& bytes)
	{
		if (stream->is_open())
		{
			stream->write((char*)bytes.data(), bytes.size());
		}
	}

	std::string File::getFilename(const std::string& filename)
	{
		size_t start = filename.find_last_of("\\/");
		return filename.substr(start + 1, filename.size() - (start + 1));
	}

	std::string File::getFileExtension(const std::string& filename)
	{
		size_t end = filename.find_last_of(".");
		if (end == std::string::npos)
			return "";

		return filename.substr(end);
	}

	std::string File::getFilenameWithoutExtension(const std::string& filename)
	{
		std::string str = getFilename(filename);
		size_t end = str.find_last_of(".");

		return str.substr(0, end);
	}

	std::string File::getFullFilenameWithoutExtension(const std::string& filename)
	{
		size_t end = filename.find_last_of(".");
		return filename.substr(0, end);
	}

	std::wstring File::getFullFilenameWithoutExtension(const std::wstring& filename)
	{
		size_t end = filename.find_last_of(L".");
		return filename.substr(0, end);
	}

	std::string File::getFilepath(const std::string& filename)
	{
		size_t start = 0;
		size_t end = filename.find_last_of("\\/");

		return filename.substr(start, end - start + 1);
	}

	std::string File::fixPath(const std::string& path)
	{
		std::string result = path;
		int index = 0;
		while (true)
		{
			index = result.find("\\", index);
			if (index == result.npos)
				break;

			result.replace(index, 1, "/");
			index += 1;
		}

		return result;
	}

	bool File::exists(const std::string& path)
	{
#ifdef _WIN32
		std::wstring wPath = mbToWideStr(path);
		return std::filesystem::exists(wPath);
#else
		return std::filesystem::exists(path);
#endif
	}

	bool File::hasFileExtension(const std::string_view& filename, const std::string_view& extension)
	{
		return endsWith(filename, extension);
	}

	FileDialogResult FileDialog::showFileDialog(DialogType type, DialogSelectType selectType)
	{
#ifdef _WIN32
		std::wstring wTitle = mbToWideStr(title);

		if (selectType == DialogSelectType::Folder)
		{
			BROWSEINFOW browseInfo{};
			browseInfo.hwndOwner = reinterpret_cast<HWND>(parentWindowHandle);
			browseInfo.lpszTitle = wTitle.c_str();
			browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

			PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browseInfo);
			if (!pidl)
				return FileDialogResult::Cancel;

			wchar_t folderPath[MAX_PATH]{};
			const BOOL gotPath = SHGetPathFromIDListW(pidl, folderPath);
			CoTaskMemFree(pidl);
			if (!gotPath)
				return FileDialogResult::Error;

			outputFilename = wideStringToMb(folderPath);
			return outputFilename.empty() ? FileDialogResult::Cancel : FileDialogResult::OK;
		}

		OPENFILENAMEW ofn;
		memset(&ofn, 0, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = reinterpret_cast<HWND>(parentWindowHandle);
		ofn.lpstrTitle = wTitle.c_str();
		ofn.nFilterIndex = filterIndex + 1;
		ofn.nFileOffset = 0;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_LONGNAMES | OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

		std::wstring wDefaultExtension = mbToWideStr(defaultExtension);
		ofn.lpstrDefExt = wDefaultExtension.c_str();

		std::vector<std::wstring> ofnFilters;
		ofnFilters.reserve(filters.size());

		/*
			since '\0' terminates the string,
			we'll do a C# by using ' | ' then replacing it with '\0' when constructing the final wide string
		*/
		std::string filtersCombined;
		for (const auto& filter : filters)
		{
			filtersCombined
				.append(filter.filterName)
				.append(" (")
				.append(filter.filterType)
				.append(")|")
				.append(filter.filterType)
				.append("|");
		}

		std::wstring wFiltersCombined = mbToWideStr(filtersCombined);
		std::replace(wFiltersCombined.begin(), wFiltersCombined.end(), '|', '\0');
		ofn.lpstrFilter = wFiltersCombined.c_str();

		std::wstring wInputFilename = mbToWideStr(inputFilename);
		wchar_t ofnFilename[1024]{ 0 };

		// suppress return value not used warning
#pragma warning(suppress: 6031)
		lstrcpynW(ofnFilename, wInputFilename.c_str(), 1024);
		ofn.lpstrFile = ofnFilename;

		if (type == DialogType::Save)
		{
			ofn.Flags |= OFN_HIDEREADONLY;
			if (GetSaveFileNameW(&ofn))
			{
				outputFilename = wideStringToMb(ofn.lpstrFile);
			}
			else
			{
				// user canceled
				return FileDialogResult::Cancel;
			}
		}
		else if (GetOpenFileNameW(&ofn))
		{
			outputFilename = wideStringToMb(ofn.lpstrFile);
		}
		else
		{
			return FileDialogResult::Cancel;
		}

		if (outputFilename.empty())
			return FileDialogResult::Cancel;

		filterIndex = ofn.nFilterIndex - 1;
		return FileDialogResult::OK;
#elif defined(__APPLE__)
		@autoreleasepool
		{
			NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
			NSMutableArray<NSString*>* extensions = [NSMutableArray array];
			for (const auto& filter : filters)
			{
				std::stringstream stream(filter.filterType);
				std::string pattern;
				while (std::getline(stream, pattern, ';'))
				{
					if (pattern == "*.*" || pattern == "*")
						continue;

					if (pattern.rfind("*.", 0) == 0)
						pattern.erase(0, 2);
					if (!pattern.empty())
						[extensions addObject:[NSString stringWithUTF8String:pattern.c_str()]];
				}
			}

			NSSavePanel* panel = nil;
			if (type == DialogType::Save)
			{
				panel = [NSSavePanel savePanel];
				if (!inputFilename.empty())
					[panel setNameFieldStringValue:[NSString stringWithUTF8String:File::getFilename(inputFilename).c_str()]];
			}
			else
			{
				NSOpenPanel* openPanel = [NSOpenPanel openPanel];
				[openPanel setCanChooseFiles:selectType == DialogSelectType::File];
				[openPanel setCanChooseDirectories:selectType == DialogSelectType::Folder];
				[openPanel setAllowsMultipleSelection:NO];
				panel = openPanel;
			}

			[panel setTitle:nsTitle ?: @""];
			if (extensions.count > 0)
				[panel setAllowedFileTypes:extensions];

			if ([panel runModal] != NSModalResponseOK)
				return FileDialogResult::Cancel;

			NSURL* url = [panel URL];
			if (!url)
				return FileDialogResult::Cancel;

			outputFilename = [[url path] UTF8String];
			return outputFilename.empty() ? FileDialogResult::Cancel : FileDialogResult::OK;
		}
#else
		// Linux / other unix: zenity (GTK) or kdialog (KDE)
		std::vector<std::string> patterns;
		for (const auto& filter : filters)
		{
			std::stringstream stream(filter.filterType);
			std::string pattern;
			while (std::getline(stream, pattern, ';'))
				if (!pattern.empty())
					patterns.push_back(pattern);
		}

		std::string startPath = inputFilename;
		if (type == DialogType::Save && !defaultExtension.empty() && !startPath.empty() && File::getFileExtension(startPath).empty())
			startPath += (defaultExtension[0] == '.' ? "" : ".") + defaultExtension;

		std::string command;
		if (commandExists("zenity"))
		{
			command = "zenity --file-selection --title=" + shellQuote(title);
			if (selectType == DialogSelectType::Folder)
				command += " --directory";
			if (type == DialogType::Save)
				command += " --save --confirm-overwrite";
			if (!startPath.empty())
				command += " --filename=" + shellQuote(startPath);
			for (const auto& filter : filters)
			{
				std::string globs = filter.filterType;
				std::replace(globs.begin(), globs.end(), ';', ' ');
				command += " --file-filter=" + shellQuote(filter.filterName + " | " + globs);
			}
		}
		else if (commandExists("kdialog"))
		{
			const char* mode = selectType == DialogSelectType::Folder ? "--getexistingdirectory" :
				type == DialogType::Save ? "--getsavefilename" : "--getopenfilename";
			std::string filterArg;
			for (const auto& filter : filters)
			{
				std::string globs = filter.filterType;
				std::replace(globs.begin(), globs.end(), ';', ' ');
				if (!filterArg.empty())
					filterArg += "\n";
				filterArg += globs + "|" + filter.filterName;
			}
			command = std::string("kdialog --title ") + shellQuote(title) + " " + mode + " " + shellQuote(startPath.empty() ? "." : startPath);
			if (selectType != DialogSelectType::Folder && !filterArg.empty())
				command += " " + shellQuote(filterArg);
		}
		else
		{
			fprintf(stderr, "No file dialog available: install zenity or kdialog, or drag files onto the window.\n");
			return FileDialogResult::Error;
		}

		int exitCode = -1;
		std::string output = trim(runProcess(command + " 2>/dev/null", &exitCode));
		if (exitCode != 0 || output.empty())
			return FileDialogResult::Cancel;

		outputFilename = output;
		if (type == DialogType::Save && selectType == DialogSelectType::File && !defaultExtension.empty() && File::getFileExtension(outputFilename).empty())
			outputFilename += (defaultExtension[0] == '.' ? "" : ".") + defaultExtension;
		return FileDialogResult::OK;
#endif
	}

	FileDialogResult FileDialog::openFile()
	{
		return showFileDialog(DialogType::Open, DialogSelectType::File);
	}

	FileDialogResult FileDialog::saveFile()
	{
		return showFileDialog(DialogType::Save, DialogSelectType::File);
	}

	FileDialogResult FileDialog::selectFolder()
	{
		return showFileDialog(DialogType::Open, DialogSelectType::Folder);
	}
}
