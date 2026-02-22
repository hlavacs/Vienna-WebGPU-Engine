#include "engine/resources/loaders/ImageLoader.h"
#include "engine/debug/Loggable.h"
#include "engine/resources/Image.h"
#include "stb_image.h"
#include <filesystem>
#include <memory>
#include <optional>

namespace engine::resources::loaders
{

[[nodiscard]] std::optional<Image::Ptr> ImageLoader::load(const std::filesystem::path &file)
{
	const auto fullPath = resolvePath(file);
	logInfo("Loading image from '{}'", fullPath.string());

	if (isHDRImage(fullPath))
		return loadHDR(fullPath);
	else
		return loadLDR(fullPath);
}

bool ImageLoader::isHDRImage(const std::filesystem::path &path)
{
	const auto ext = path.extension().string();
	return ext == ".hdr";
}

std::optional<Image::Ptr> ImageLoader::loadHDR(const std::filesystem::path &fullPath)
{
	stbi_set_flip_vertically_on_load(false);

	int width = 0, height = 0, channels = 0;
	float *data = stbi_loadf(fullPath.string().c_str(), &width, &height, &channels, 0);

	if (!data)
	{
		logError("Failed to load HDR image '{}': {}", fullPath.string(), stbi_failure_reason());
		return std::nullopt;
	}

	std::vector<float> pixels;
	pixels.reserve(static_cast<size_t>(width) * height * 4);

	if (channels == 3)
	{
		for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
		{
			pixels.push_back(data[i * 3 + 0]);
			pixels.push_back(data[i * 3 + 1]);
			pixels.push_back(data[i * 3 + 2]);
			pixels.push_back(1.0f);
		}
		channels = 4;
	}
	else
	{
		pixels.assign(data, data + static_cast<size_t>(width) * height * channels);
	}

	stbi_image_free(data);

	ImageFormatType format = ImageFormat::formatFromChannels(channels, true);
	return std::make_shared<Image>(width, height, format, std::move(pixels));
}

std::optional<Image::Ptr> ImageLoader::loadLDR(const std::filesystem::path &fullPath)
{
	stbi_set_flip_vertically_on_load(false); // 🔥 see explanation below

	int width = 0, height = 0, channels = 0;
	unsigned char *data =
		stbi_load(fullPath.string().c_str(), &width, &height, &channels, 0);

	if (!data)
	{
		logError("Failed to load image '{}': {}", fullPath.string(), stbi_failure_reason());
		return std::nullopt;
	}

	std::vector<uint8_t> pixels;
	pixels.reserve(static_cast<size_t>(width) * height * 4);

	if (channels == 3)
	{
		for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
		{
			pixels.push_back(data[i * 3 + 0]);
			pixels.push_back(data[i * 3 + 1]);
			pixels.push_back(data[i * 3 + 2]);
			pixels.push_back(255);
		}
		channels = 4;
	}
	else
	{
		pixels.assign(data, data + static_cast<size_t>(width) * height * channels);
	}

	stbi_image_free(data);

	ImageFormatType format = ImageFormat::formatFromChannels(channels, false);
	return std::make_shared<Image>(width, height, format, std::move(pixels));
}

} // namespace engine::resources::loaders
