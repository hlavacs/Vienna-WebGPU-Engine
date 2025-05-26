#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace engine::io
{
	class FileReader
	{
	public:
		// Read binary file into bytes
		static std::optional<std::vector<uint8_t>> loadBinary(const std::string &path);

		// Read text file into std::string
		static std::optional<std::string> loadText(const std::string &path);

		// Read binary file into char buffer (null-terminated)
		static std::optional<std::unique_ptr<char[]>> loadAsCString(const std::string &path);
	};
} // namespace engine::io