#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>

#define Z_CHUNK_SIZE 32768ULL

namespace IO
{
	enum class MessageBoxButtons : uint8_t
	{
		Ok,
		OkCancel,
		YesNo,
		YesNoCancel
	};

	enum class MessageBoxIcon : uint8_t
	{
		None,
		Information,
		Warning,
		Error,
		Question
	};

	enum class MessageBoxResult : uint8_t
	{
		None,
		Abort,
		Cancel,
		Ignore,
		No,
		Yes,
		Ok
	};

	constexpr const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	char* reverse(char* str);
	char* tostringBaseN(char* buff, long long num, int base);
	bool isComment(const std::string_view& line, const std::string_view& delim);
	bool startsWith(const std::string_view& line, const std::string_view& key);
	bool endsWith(const std::string_view& line, const std::string_view& key);
	bool isDigit(const std::string_view& str);
	std::string trim(const std::string& line);
	std::vector<std::string> split(const std::string& line, const std::string& delim);

	std::string wideStringToMb(const std::wstring& str);
	std::wstring mbToWideStr(const std::string& str);

	std::string concat(const char* s1, const char* s2, const char* join = "");

	std::vector<uint8_t> inflateGzip(const std::vector<uint8_t>& data);
	std::vector<uint8_t> deflateGzip(const std::vector<uint8_t>& data);
	bool isGzipCompressed(const std::vector<uint8_t>& data);

	std::string base64Encode(const std::vector<uint8_t>& data);
	// Ignores whitespace; returns false on any other invalid character.
	bool base64Decode(const std::string& text, std::vector<uint8_t>& out);
	
	namespace formatting
	{
		inline const char* to_printable(const char* s) { return s; }
		inline const char* to_printable(char* s) { return s; }
		inline const char* to_printable(const std::string& s) { return s.c_str(); }
		template<typename T,
			typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
		T to_printable(T v) { return v; }
		inline void* to_printable(void* p) { return p; }
		inline const void* to_printable(const void* p) { return p; }
	}

	template<typename ... Args>
	std::string formatString(const char* format, Args&& ... args)
	{
		auto length = std::snprintf(nullptr, 0, format, formatting::to_printable(std::forward<Args>(args)) ...) + 1;
		if (length <= 0)
			throw std::runtime_error("An error occured while attempting to format a string.");

		std::unique_ptr<char[]> buf(new char[length]);
		std::snprintf(buf.get(), length, format, formatting::to_printable(std::forward<Args>(args)) ...);

		return std::string(buf.get());
	}

	static std::string formatFixedFloatTrimmed(float value, int precision = 7)
	{
		auto length = std::snprintf(NULL, 0, "%.*f", precision, value);
		if (length < 0)
			return "NaN";
		std::string buf(length, '\0');
		std::snprintf(buf.data(), length + 1, "%.*f", precision, value);
		// Trim trailing zeros
		size_t end = buf.find_last_not_of('0');
		if (end != std::string::npos)
			buf.erase(buf[end] == '.' ? end : end + 1);
		return buf;
	}

	// Automated runs (--screenshot / --export-video) set this: dialogs are printed instead of
	// popping up on somebody's screen and answered with OK / Yes.
	extern bool suppressMessageBoxes;
	MessageBoxResult messageBox(std::string title, std::string message, MessageBoxButtons buttons, MessageBoxIcon icon, void* parentWindow = NULL);

	// Quotes one argument for the platform shell (POSIX sh or cmd.exe).
	std::string shellQuote(const std::string& arg);

	// Runs a shell command, returns everything it wrote to stdout. exitCode receives the
	// process exit status (-1 if it could not be started).
	std::string runProcess(const std::string& command, int* exitCode = nullptr);

	// Starts `command` with a pipe to its stdin (binary). closeProcess returns its exit code.
	FILE* openProcessForWriting(const std::string& command);
	int closeProcess(FILE* pipe);

	// True when `name` can be found on PATH (or is an existing file path).
	bool commandExists(const std::string& name);

	// Opens a URL, file or folder with the desktop's default handler (non-blocking).
	void openInShell(const std::string& target);
}