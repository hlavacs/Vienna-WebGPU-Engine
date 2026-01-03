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
	std::filesystem::path fullPath = resolvePath(file);
	logInfo("Loading image from '{}'", fullPath.string());

	stbi_set_flip_vertically_on_load(true);
	int width = 0, height = 0, channels = 0;

	// Decide HDR based on file extension
	bool isHDR = fullPath.extension() == ".hdr" || fullPath.extension() == ".exr";
	if (isHDR)
	{
		// Load HDR data - force RGBA for RGB images (WebGPU doesn't support RGB formats)
		float *hdrData = stbi_loadf(fullPath.string().c_str(), &width, &height, &channels, 0);
		if (!hdrData)
		{
			logError("Failed to load HDR image '{}': {}", fullPath.string(), stbi_failure_reason());
			return std::nullopt;
		}

		// Convert RGB to RGBA if needed
		std::vector<float> pixels;
		if (channels == 3)
		{
			logInfo("Converting HDR RGB to RGBA for WebGPU compatibility");
			size_t pixelCount = static_cast<size_t>(width) * height;
			pixels.reserve(pixelCount * 4);
			for (size_t i = 0; i < pixelCount; ++i)
			{
				pixels.push_back(hdrData[i * 3 + 0]); // R
				pixels.push_back(hdrData[i * 3 + 1]); // G
				pixels.push_back(hdrData[i * 3 + 2]); // B
				pixels.push_back(1.0f);				  // A
			}
			channels = 4;
		}
		else
		{
			size_t dataSize = static_cast<size_t>(width) * height * channels;
			pixels = std::vector<float>(hdrData, hdrData + dataSize);
		}

		ImageFormatType format = ImageFormat::formatFromChannels(channels, true);
		stbi_image_free(hdrData);

		return std::make_shared<Image>(width, height, format, std::move(pixels));
	}

	// Fallback to LDR - force RGBA for RGB images (WebGPU doesn't support RGB formats)
	unsigned char *ldrData = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 0);
	if (!ldrData)
	{
		logError("Failed to load image '{}': {}", fullPath.string(), stbi_failure_reason());
		return std::nullopt;
	}

	// Convert RGB to RGBA if needed
	std::vector<uint8_t> pixels;
	if (channels == 3)
	{
		logInfo("Converting LDR RGB to RGBA for WebGPU compatibility");
		size_t pixelCount = static_cast<size_t>(width) * height;
		pixels.reserve(pixelCount * 4);
		for (size_t i = 0; i < pixelCount; ++i)
		{
			pixels.push_back(ldrData[i * 3 + 0]); // R
			pixels.push_back(ldrData[i * 3 + 1]); // G
			pixels.push_back(ldrData[i * 3 + 2]); // B
			pixels.push_back(255);				  // A
		}
		channels = 4;
	}
	else
	{
		size_t dataSize = static_cast<size_t>(width) * height * channels;
		pixels = std::vector<uint8_t>(ldrData, ldrData + dataSize);
	}

	ImageFormatType format = ImageFormat::formatFromChannels(channels, false);
	stbi_image_free(ldrData);

	return std::make_shared<Image>(width, height, format, std::move(pixels));
}

} // namespace engine::resources::loaders
