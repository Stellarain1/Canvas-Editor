#pragma once
#include <chrono>
#include <map>
#include <string>

// A tiny frame profiler for --bench: CANVAS_PROFILE("name") times the rest of the scope and adds it
// to that name's total. Only counts while enabled (the bench turns it on), so the normal app pays
// one bool check per scope.
namespace Canvas::Profiler
{
	struct Entry
	{
		double totalMs{};
		long calls{};
	};

	inline bool enabled = false;
	// --bench sets this to glFinish: CANVAS_PROFILE_GPU scopes wait for the GPU at both ends, so
	// their time is what the GPU spent on them (slows the bench down, never the app)
	inline void (*sync)() = nullptr;

	inline std::map<std::string, Entry>& entries()
	{
		static std::map<std::string, Entry> all;
		return all;
	}

	class Scope
	{
	public:
		explicit Scope(const char* name) : name(enabled ? name : nullptr)
		{
			if (this->name)
				start = std::chrono::steady_clock::now();
		}
		~Scope()
		{
			if (!name)
				return;
			Entry& e = entries()[name];
			e.totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			++e.calls;
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		const char* name;
		std::chrono::steady_clock::time_point start{};
	};
}

#define CANVAS_PROFILE_CAT2(a, b) a##b
#define CANVAS_PROFILE_CAT(a, b) CANVAS_PROFILE_CAT2(a, b)
#define CANVAS_PROFILE(name) ::Canvas::Profiler::Scope CANVAS_PROFILE_CAT(profileScope_, __LINE__)(name)

namespace Canvas::Profiler
{
	struct GpuScope
	{
		Scope scope;
		explicit GpuScope(const char* name) : scope((enabled && sync ? (sync(), name) : name))
		{
		}
		~GpuScope()
		{
			if (enabled && sync)
				sync();
		}
	};
}
#define CANVAS_PROFILE_GPU(name) ::Canvas::Profiler::GpuScope CANVAS_PROFILE_CAT(profileGpu_, __LINE__)(name)
