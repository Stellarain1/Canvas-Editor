#include <cstdio>
#include "IO.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#include <AppKit/AppKit.h>
#endif
#include <algorithm>
#include <filesystem>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <cstring>
#include <zlib.h>
#include <sstream>
#include <cassert>
#include <cctype>
#include <codecvt>
#include <locale>

#undef min
#undef max

namespace IO
{
	bool suppressMessageBoxes = false;

	MessageBoxResult messageBox(std::string title, std::string message, MessageBoxButtons buttons, MessageBoxIcon icon, void* parentWindow)
	{
		if (suppressMessageBoxes)
		{
			std::printf("[%s] %s\n", title.c_str(), message.c_str());
			return buttons == MessageBoxButtons::Ok || buttons == MessageBoxButtons::OkCancel ? MessageBoxResult::Ok : MessageBoxResult::Yes;
		}

#ifdef _WIN32
		UINT flags = 0;
		switch (icon)
		{
		case MessageBoxIcon::Information:	flags |= MB_ICONINFORMATION; break;
		case MessageBoxIcon::Warning:		flags |= MB_ICONWARNING; break;
		case MessageBoxIcon::Error:			flags |= MB_ICONERROR; break;
		case MessageBoxIcon::Question:		flags |= MB_ICONQUESTION; break;
		default: break;
		}

		switch (buttons)
		{
		case MessageBoxButtons::Ok:			flags |= MB_OK; break;
		case MessageBoxButtons::OkCancel:	flags |= MB_OKCANCEL; break;
		case MessageBoxButtons::YesNo:		flags |= MB_YESNO; break;
		case MessageBoxButtons::YesNoCancel:flags |= MB_YESNOCANCEL; break;
		default: break;
		}

		const int result = MessageBoxExW(reinterpret_cast<HWND>(parentWindow), mbToWideStr(message).c_str(), mbToWideStr(title).c_str(), flags, 0);
		switch (result)
		{
		case IDABORT:	return MessageBoxResult::Abort;
		case IDCANCEL:	return MessageBoxResult::Cancel;
		case IDIGNORE:	return MessageBoxResult::Ignore;
		case IDNO:		return MessageBoxResult::No;
		case IDYES:		return MessageBoxResult::Yes;
		case IDOK:		return MessageBoxResult::Ok;
		default:		return MessageBoxResult::None;
		}
#elif defined(__APPLE__)
		@autoreleasepool
		{
			NSAlert* alert = [[NSAlert alloc] init];
			[alert setMessageText:[NSString stringWithUTF8String:title.c_str()] ?: @""];
			[alert setInformativeText:[NSString stringWithUTF8String:message.c_str()] ?: @""];

			switch (icon)
			{
			case MessageBoxIcon::Warning: [alert setAlertStyle:NSAlertStyleWarning]; break;
			case MessageBoxIcon::Error: [alert setAlertStyle:NSAlertStyleCritical]; break;
			default: [alert setAlertStyle:NSAlertStyleInformational]; break;
			}

			switch (buttons)
			{
			case MessageBoxButtons::OkCancel:
				[alert addButtonWithTitle:@"OK"];
				[alert addButtonWithTitle:@"Cancel"];
				break;
			case MessageBoxButtons::YesNo:
				[alert addButtonWithTitle:@"Yes"];
				[alert addButtonWithTitle:@"No"];
				break;
			case MessageBoxButtons::YesNoCancel:
				[alert addButtonWithTitle:@"Yes"];
				[alert addButtonWithTitle:@"No"];
				[alert addButtonWithTitle:@"Cancel"];
				break;
			case MessageBoxButtons::Ok:
			default:
				[alert addButtonWithTitle:@"OK"];
				break;
			}

			const NSModalResponse response = [alert runModal];
			const NSInteger buttonIndex = response - NSAlertFirstButtonReturn;
			if (buttons == MessageBoxButtons::YesNo || buttons == MessageBoxButtons::YesNoCancel)
			{
				if (buttonIndex == 0) return MessageBoxResult::Yes;
				if (buttonIndex == 1) return MessageBoxResult::No;
				if (buttonIndex == 2) return MessageBoxResult::Cancel;
			}
			else if (buttons == MessageBoxButtons::OkCancel && buttonIndex == 1)
			{
				return MessageBoxResult::Cancel;
			}
			return MessageBoxResult::Ok;
		}
#else
		// Linux / other unix: zenity or kdialog when available, stderr otherwise
		const bool question = buttons == MessageBoxButtons::YesNo || buttons == MessageBoxButtons::YesNoCancel || buttons == MessageBoxButtons::OkCancel;
		int exitCode = -1;
		if (commandExists("zenity"))
		{
			std::string kind = question ? "--question" : icon == MessageBoxIcon::Error ? "--error" : icon == MessageBoxIcon::Warning ? "--warning" : "--info";
			std::string cmd = "zenity " + kind + " --no-markup --title=" + shellQuote(title) + " --text=" + shellQuote(message);
			if (buttons == MessageBoxButtons::YesNo || buttons == MessageBoxButtons::YesNoCancel)
				cmd += " --ok-label=Yes --cancel-label=No";
			if (buttons == MessageBoxButtons::YesNoCancel)
				cmd += " --extra-button=Cancel";
			std::string out = runProcess(cmd + " 2>/dev/null", &exitCode);
			if (!question)
				return MessageBoxResult::Ok;
			if (buttons == MessageBoxButtons::YesNoCancel && trim(out) == "Cancel")
				return MessageBoxResult::Cancel;
			if (buttons == MessageBoxButtons::OkCancel)
				return exitCode == 0 ? MessageBoxResult::Ok : MessageBoxResult::Cancel;
			return exitCode == 0 ? MessageBoxResult::Yes : MessageBoxResult::No;
		}
		if (commandExists("kdialog"))
		{
			std::string kind = buttons == MessageBoxButtons::YesNoCancel ? "--yesnocancel" : question ? "--yesno" :
				icon == MessageBoxIcon::Error ? "--error" : icon == MessageBoxIcon::Warning ? "--sorry" : "--msgbox";
			runProcess("kdialog --title " + shellQuote(title) + " " + kind + " " + shellQuote(message) + " 2>/dev/null", &exitCode);
			if (!question)
				return MessageBoxResult::Ok;
			if (buttons == MessageBoxButtons::OkCancel)
				return exitCode == 0 ? MessageBoxResult::Ok : MessageBoxResult::Cancel;
			if (exitCode == 2)
				return MessageBoxResult::Cancel;
			return exitCode == 0 ? MessageBoxResult::Yes : MessageBoxResult::No;
		}

		fprintf(stderr, "[%s] %s\n", title.c_str(), message.c_str());
		if (buttons == MessageBoxButtons::YesNo || buttons == MessageBoxButtons::YesNoCancel)
			return MessageBoxResult::No;
		return MessageBoxResult::Ok;
#endif
	}

	std::string shellQuote(const std::string& arg)
	{
#ifdef _WIN32
		std::string out = "\"";
		for (char c : arg)
		{
			if (c == '"')
				out += "\\\"";
			else
				out += c;
		}
		return out + "\"";
#else
		std::string out = "'";
		for (char c : arg)
		{
			if (c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		return out + "'";
#endif
	}

#ifdef _WIN32
	// popen would flash a console window each time (updates, Browse...): run cmd.exe hidden and read
	// its output through our own pipe instead
	static std::string runHidden(const std::string& command, int* exitCode)
	{
		std::string output;
		if (exitCode)
			*exitCode = -1;

		SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE readEnd = nullptr, writeEnd = nullptr;
		if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0))
			return output;
		SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
		HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_HIDE;
		startup.hStdInput = nul;
		startup.hStdOutput = writeEnd;
		startup.hStdError = nul;	// like popen with no redirect it was the console; nobody reads that

		// cmd.exe /c strips the first and last quote when the line starts with one (a quoted
		// executable path): wrap the whole line in quotes
		std::wstring line = L"cmd.exe /d /c \"" + mbToWideStr(command) + L"\"";
		PROCESS_INFORMATION process{};
		const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
		CloseHandle(writeEnd);
		if (nul != INVALID_HANDLE_VALUE)
			CloseHandle(nul);
		if (!started)
		{
			CloseHandle(readEnd);
			return output;
		}

		char buffer[4096];
		DWORD read = 0;
		while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0)
			output.append(buffer, read);
		CloseHandle(readEnd);

		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD code = 0;
		GetExitCodeProcess(process.hProcess, &code);
		CloseHandle(process.hProcess);
		CloseHandle(process.hThread);
		if (exitCode)
			*exitCode = static_cast<int>(code);
		return output;
	}
#endif

	std::string runProcess(const std::string& command, int* exitCode)
	{
		std::string output;
#ifdef _WIN32
		return runHidden(command, exitCode);
#else
		FILE* pipe = popen(command.c_str(), "r");
		if (!pipe)
		{
			if (exitCode)
				*exitCode = -1;
			return output;
		}

		char buffer[4096];
		size_t read = 0;
		while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
			output.append(buffer, read);

		int status = pclose(pipe);
		if (status != -1 && WIFEXITED(status))
			status = WEXITSTATUS(status);
		if (exitCode)
			*exitCode = status;
		return output;
#endif
	}

	FILE* openProcessForWriting(const std::string& command)
	{
#ifdef _WIN32
		// cmd.exe /c strips the first and last quote when the line starts with one
		return _wpopen(mbToWideStr("\"" + command + "\"").c_str(), L"wb");
#else
		return popen(command.c_str(), "w");
#endif
	}

	int closeProcess(FILE* pipe)
	{
		if (!pipe)
			return -1;
#ifdef _WIN32
		return _pclose(pipe);
#else
		int status = pclose(pipe);
		if (status != -1 && WIFEXITED(status))
			status = WEXITSTATUS(status);
		return status;
#endif
	}

	void openInShell(const std::string& target)
	{
		if (target.empty())
			return;
#if defined(_WIN32)
		ShellExecuteW(NULL, L"open", mbToWideStr(target).c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
		std::system(("open " + shellQuote(target) + " >/dev/null 2>&1 &").c_str());
#else
		std::system(("xdg-open " + shellQuote(target) + " >/dev/null 2>&1 &").c_str());
#endif
	}

	bool commandExists(const std::string& name)
	{
		if (name.empty())
			return false;

		if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
			return std::filesystem::exists(name);

		int exitCode = -1;
#ifdef _WIN32
		runProcess("where " + shellQuote(name) + " >NUL 2>NUL", &exitCode);
#else
		runProcess("command -v " + shellQuote(name) + " >/dev/null 2>&1", &exitCode);
#endif
		return exitCode == 0;
	}

	char* reverse(char* str)
	{
		char* end = str;
		char* start = str;

		if (!str || !*str) return str;
		while (*(end + 1)) end++;
		while (end > start)
		{
			int ch = *end;
			*end-- = *start;
			*start++ = ch;
		}
		return str;
	}

	char* tostringBaseN(char* buff, long long num, int base)
	{
		int sign = num < 0;
		char* savedbuff = buff;

		if (base < 2 || base >= sizeof(digits)) return NULL;
		if (buff)
		{
			do
			{
				*buff++ = digits[abs(num % base)];
				num /= base;
			} while (num);
			if (sign)
			{
				*buff++ = '-';
			}
			*buff = 0;
			reverse(savedbuff);
		}
		return savedbuff;
	}

	bool isComment(const std::string_view& line, const std::string_view& delim)
	{
		if (line.empty())
			return true;

		return line.find_first_of(delim) == 0 || line.at(0) == '\n';
	}

	bool startsWith(const std::string_view& line, const std::string_view& key)
	{
		if (line.length() < key.length())
			return false;

		return std::equal(key.begin(), key.end(), line.begin());
	}

	bool endsWith(const std::string_view& line, const std::string_view& key)
	{
		if (line.length() < key.length())
			return false;

		return std::equal(key.rbegin(), key.rend(), line.rbegin());
	}

	bool isDigit(const std::string_view& str)
	{
		if (str.empty())
			return false;

		return std::all_of(str.begin() + (str.at(0) == '-' ? 1 : 0), str.end(), [](unsigned char c) { return std::isdigit(c); });
	}

	std::string trim(const std::string& line)
	{
		if (line.empty())
			return line;

		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return "";

		size_t end = line.find_last_not_of(" \t\r\n");

		return line.substr(start, end - start + 1);
	}

	std::vector<std::string> split(const std::string& line, const std::string& delim)
	{
		std::vector<std::string> values;
		size_t start = 0;
		size_t end = line.length() - 1;

		while (start < line.length() && end != std::string::npos)
		{
			end = line.find_first_of(delim, start);
			values.push_back(line.substr(start, end - start));

			start = end + 1;
		}

		return values;
	}

	std::string wideStringToMb(const std::wstring& str)
	{
#ifdef _WIN32
		if (str.empty())
			return {};

		int size = WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0, NULL, NULL);
		std::string result(size, 0);
		WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), &result[0], size, NULL, NULL);

		return result;
#else
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		return converter.to_bytes(str);
#endif
	}

	std::wstring mbToWideStr(const std::string& str)
	{
#ifdef _WIN32
		if (str.empty())
			return {};

		int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], str.size(), NULL, 0);
		std::wstring wResult(size, 0);
		MultiByteToWideChar(CP_UTF8, 0, &str[0], str.size(), &wResult[0], size);

		return wResult;
#else
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		return converter.from_bytes(str);
#endif
	}

	std::string concat(const char* s1, const char* s2, const char* join)
	{
		return std::string(s1).append(join).append(s2);
	}

	static std::vector<uint8_t> processCompressionStream(const std::vector<uint8_t>& data, z_stream* stream, int flush, int (*process)(z_streamp, int))
	{
		stream->avail_in = data.size();
		stream->next_in = (Byte*)data.data();

		std::vector<uint8_t> dest;
		std::unique_ptr<Byte[]> buf(new Byte[Z_CHUNK_SIZE]);
		int result = Z_OK;

		do
		{
			stream->next_out = buf.get();
			stream->avail_out = Z_CHUNK_SIZE;

			result = process(stream, flush);
			if (dest.size() < stream->total_out)
			{
				size_t count = Z_CHUNK_SIZE - stream->avail_out;
				for (size_t i = 0; i < count; i++)
					dest.push_back(buf[i]);
			}

		} while (result == Z_OK);

		return dest;
	}

	std::vector<uint8_t> inflateGzip(const std::vector<uint8_t>& data)
	{
		z_stream stream{};
		memset(&stream, 0, sizeof(z_stream));

		int result = inflateInit2(&stream, 15 | 16);
		if (result != Z_OK)
			return {};

		std::vector<uint8_t> dest = processCompressionStream(data, &stream, Z_FULL_FLUSH, inflate);

		inflateEnd(&stream);
		return dest;
	}

	std::vector<uint8_t> deflateGzip(const std::vector<uint8_t>& data)
	{
		z_stream stream{};
		memset(&stream, 0, sizeof(z_stream));

		int result = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
		if (result != Z_OK)
			return {};

		std::vector<uint8_t> dest = processCompressionStream(data, &stream, Z_FINISH, deflate);

		deflateEnd(&stream);
		return dest;
	}

	bool isGzipCompressed(const std::vector<uint8_t>& data)
	{
		return data.size() > 2 && data[0] == 0x1F && data[1] == 0x8B;
	}

	static constexpr const char* base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string base64Encode(const std::vector<uint8_t>& data)
	{
		std::string out;
		out.reserve(((data.size() + 2) / 3) * 4);
		size_t i = 0;
		for (; i + 2 < data.size(); i += 3)
		{
			const uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
			out += base64Alphabet[(n >> 18) & 63];
			out += base64Alphabet[(n >> 12) & 63];
			out += base64Alphabet[(n >> 6) & 63];
			out += base64Alphabet[n & 63];
		}

		const size_t remaining = data.size() - i;
		if (remaining == 1)
		{
			const uint32_t n = data[i] << 16;
			out += base64Alphabet[(n >> 18) & 63];
			out += base64Alphabet[(n >> 12) & 63];
			out += "==";
		}
		else if (remaining == 2)
		{
			const uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
			out += base64Alphabet[(n >> 18) & 63];
			out += base64Alphabet[(n >> 12) & 63];
			out += base64Alphabet[(n >> 6) & 63];
			out += '=';
		}
		return out;
	}

	bool base64Decode(const std::string& text, std::vector<uint8_t>& out)
	{
		out.clear();
		uint32_t buffer = 0;
		int bits = 0;
		for (char c : text)
		{
			if (c == '=' )
				break;
			if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
				continue;

			const char* pos = std::strchr(base64Alphabet, c);
			if (!pos || c == '\0')
				return false;

			buffer = (buffer << 6) | static_cast<uint32_t>(pos - base64Alphabet);
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
			}
		}
		return true;
	}
}
