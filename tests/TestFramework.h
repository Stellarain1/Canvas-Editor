#pragma once
// A very small test runner: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
// No dependencies so the tests build everywhere the editor builds.
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace CanvasTest
{
	struct Case
	{
		const char* name;
		std::function<void()> body;
	};

	inline std::vector<Case>& registry()
	{
		static std::vector<Case> cases;
		return cases;
	}

	inline int& failures()
	{
		static int count = 0;
		return count;
	}

	struct Register
	{
		Register(const char* name, std::function<void()> body) { registry().push_back({ name, std::move(body) }); }
	};

	template <typename T>
	std::string show(const T& value)
	{
		std::ostringstream out;
		if constexpr (std::is_enum_v<T>)
			out << static_cast<long long>(value);
		else
			out << value;
		return out.str();
	}

	inline std::string show(const std::string& value) { return "\"" + value + "\""; }

	template <typename T>
	std::string show(const std::vector<T>& values)
	{
		std::string out = "[";
		for (size_t i = 0; i < values.size(); ++i)
			out += (i ? ", " : "") + show(values[i]);
		return out + "]";
	}

	inline void fail(const char* file, int line, const std::string& what)
	{
		++failures();
		std::fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, what.c_str());
	}
}

#define CANVAS_CONCAT_(a, b) a##b
#define CANVAS_CONCAT(a, b) CANVAS_CONCAT_(a, b)

#define TEST(name)                                                                               \
	static void CANVAS_CONCAT(test_, name)();                                                    \
	static CanvasTest::Register CANVAS_CONCAT(register_, name)(#name, CANVAS_CONCAT(test_, name)); \
	static void CANVAS_CONCAT(test_, name)()

#define CHECK(condition)                                                     \
	do                                                                       \
	{                                                                        \
		if (!(condition))                                                    \
			CanvasTest::fail(__FILE__, __LINE__, "CHECK(" #condition ")"); \
	} while (0)

#define CHECK_EQ(actual, expected)                                                                              \
	do                                                                                                          \
	{                                                                                                           \
		const auto& a_ = (actual);                                                                              \
		const auto& e_ = (expected);                                                                            \
		if (!(a_ == e_))                                                                                        \
			CanvasTest::fail(__FILE__, __LINE__,                                                                \
				std::string(#actual " == " #expected ": got ") + CanvasTest::show(a_) + ", expected " + CanvasTest::show(e_)); \
	} while (0)
