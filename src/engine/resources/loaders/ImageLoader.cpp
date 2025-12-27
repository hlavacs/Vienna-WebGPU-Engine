#pragma once

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
	std::filesystem::path fullPath = file.is_absolute() ? file : (m_basePath / file);
	logInfo("Loading image from '{}'", fullPath.string());

	int width = 0, height = 0, channels = 0;
	stbi_set_flip_vertically_on_load(true);

	// Load HDR image first
	float *hdrData = stbi_loadf(fullPath.string().c_str(), &width, &height, &channels, 0);
	if (hdrData)
	{
		ImageFormat format = ImageFormat::Unknown;
		switch (channels)
		{
		case 1:
			format = ImageFormat::HDR_R16F;
			break;
		case 3:
			format = ImageFormat::HDR_RGB16F;
			break;
		case 4:
			format = ImageFormat::HDR_RGBA16F;
			break;
		default:
			format = ImageFormat::Unknown;
			break; // fallback
		}

		size_t dataSize = static_cast<size_t>(width) * height * channels;
		std::vector<float> pixels(hdrData, hdrData + dataSize);
		stbi_image_free(hdrData);

		auto image = std::make_shared<Image>(width, height, format, std::move(pixels));
		logInfo("Loaded HDR image '{}', size: {}x{}, channels: {}", fullPath.string(), width, height, channels);
		return image;
	}

	// Fallback to LDR
	unsigned char *ldrData = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 0);
	if (!ldrData)
	{
		logError("Failed to load image '{}': {}", fullPath.string(), stbi_failure_reason());
		return std::nullopt;
	}

	ImageFormat format = ImageFormat::Unknown;
	switch (channels)
	{
	case 1:
		format = ImageFormat::LDR_R8;
		break;
	case 3:
		format = ImageFormat::LDR_RGB8;
		break;
	case 4:
		format = ImageFormat::LDR_RGBA8;
		break;
	default:
		format = ImageFormat::Unknown;
		break; // fallback
	}

	size_t dataSize = static_cast<size_t>(width) * height * channels;
	std::vector<uint8_t> pixels(ldrData, ldrData + dataSize);
	stbi_image_free(ldrData);

	auto image = std::make_shared<Image>(width, height, format, std::move(pixels));
	logInfo("Loaded LDR image '{}', size: {}x{}, channels: {}", fullPath.string(), width, height, channels);
	return image;
}

} // namespace engine::resources::loaders
