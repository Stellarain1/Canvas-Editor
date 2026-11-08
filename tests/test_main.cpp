#include "TestFramework.h"
#include "Canvas/Core/Version.h"
#include <cstring>
#include <exception>

// canvas_tests [filter]  - runs every test whose name contains `filter`
int main(int argc, char** argv)
{
	const char* filter = argc > 1 ? argv[1] : nullptr;
	std::printf("canvas_tests (%s)\n", Canvas::generatorString().c_str());

	int run = 0, failedCases = 0;
	for (const auto& test : CanvasTest::registry())
	{
		if (filter && !std::strstr(test.name, filter))
			continue;

		++run;
		const int before = CanvasTest::failures();
		try
		{
			test.body();
		}
		catch (const std::exception& ex)
		{
			CanvasTest::fail(__FILE__, __LINE__, std::string("exception: ") + ex.what());
		}

		const bool ok = CanvasTest::failures() == before;
		failedCases += !ok;
		std::printf("%s %s\n", ok ? "  ok  " : " FAIL ", test.name);
	}

	std::printf("%d test(s), %d failed\n", run, failedCases);
	return failedCases == 0 ? 0 : 1;
}
