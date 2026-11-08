#pragma once
#include <cstdint>
#include <string>
#include <vector>

// SHA-256 (FIPS 180-4), for checking downloads against the checksum the update server publishes.
namespace Canvas
{
	class Sha256
	{
	public:
		Sha256() { reset(); }
		void reset();
		void update(const void* data, size_t size);
		// lower-case hex digest; the object starts over afterwards
		std::string hex();

		static std::string ofBytes(const std::vector<uint8_t>& bytes);
		// "" when the file cannot be read
		static std::string ofFile(const std::string& path);

	private:
		void block(const uint8_t* chunk);
		uint32_t state[8]{};
		uint8_t buffer[64]{};
		uint64_t length{};
		size_t used{};
	};
}
