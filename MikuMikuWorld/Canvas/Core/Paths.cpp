#include "Paths.h"
#include <filesystem>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#endif

namespace Canvas
{
	std::string executableDirectory()
	{
		std::filesystem::path exe;
#ifdef _WIN32
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length > 0)
			exe = std::filesystem::path(std::wstring(buffer.data(), length));
#elif defined(__APPLE__)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::vector<char> buffer(size + 1, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) == 0)
			exe = std::filesystem::path(buffer.data());
#else
		std::error_code error;
		exe = std::filesystem::read_symlink("/proc/self/exe", error);
#endif
		if (exe.empty())
			return {};
		std::error_code canonicalError;
		const auto canonical = std::filesystem::weakly_canonical(exe, canonicalError);
		const std::string dir = (canonicalError ? exe : canonical).parent_path().u8string();
		return dir.empty() ? dir : dir + "/";
	}
}
