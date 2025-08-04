#include "engine/io/FileReader.h"
#include <fstream>
#include <sstream>

namespace engine::io
{

// Read binary into std::vector<uint8_t>
std::optional<std::vector<uint8_t>> FileReader::loadBinary(const std::string &path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return std::nullopt;

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(size);
	if (!file.read(reinterpret_cast<char *>(buffer.data()), size))
		return std::nullopt;

	return buffer;
}

// Read text into std::string
std::optional<std::string> FileReader::loadText(const std::string &path)
{
	std::ifstream file(path);
	if (!file)
		return std::nullopt;

	std::ostringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

// Read binary into null-terminated char buffer
std::optional<std::unique_ptr<char[]>> FileReader::loadAsCString(const std::string &path)
{
	auto data = loadBinary(path);
	if (!data)
		return std::nullopt;

	std::unique_ptr<char[]> buffer = std::make_unique<char[]>(data->size() + 1);
	std::copy(data->begin(), data->end(), buffer.get());
	buffer[data->size()] = '\0';

	return buffer;
}
} // namespace engine::io