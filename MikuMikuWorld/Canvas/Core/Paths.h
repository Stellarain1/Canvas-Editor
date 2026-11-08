#pragma once
#include <string>

namespace Canvas
{
	// Folder of the running executable, with a trailing separator ("" when unknown).
	std::string executableDirectory();
}
