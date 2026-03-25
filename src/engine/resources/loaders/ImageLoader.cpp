#include "engine/resources/loaders/ImageLoader.h"

#include <filesystem>
#include <memory>
#include <optional>

#include "engine/debug/Loggable.h"
#include "engine/resources/Image.h"

#include "stb_image.h"
#include "stb_image_write.h"

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
	stbi_set_flip_vertically_on_load(false);

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

std::optional<Image::Ptr> ImageLoader::createEmpty(
	uint32_t width,
	uint32_t height,
	std::optional<ImageFormat::Type> format
)
{
	const auto imgFormat = format.value_or(ImageFormat::Type::LDR_RGBA8);

	if (ImageFormat::isHDRFormat(imgFormat))
	{
		std::vector<float> pixels(width * height * ImageFormat::getChannelCount(imgFormat), 0.0f);
		return std::make_shared<Image>(width, height, imgFormat, std::move(pixels));
	}
	else
	{
		std::vector<uint8_t> pixels(width * height * ImageFormat::getChannelCount(imgFormat), 0);
		return std::make_shared<Image>(width, height, imgFormat, std::move(pixels));
	}
}

bool ImageLoader::saveAsPNG(const Image &image, const std::filesystem::path &filePath) const
{
	if (!image.isLDR())
	{
		logError("Only LDR images can be saved as PNG. Image format is not LDR.");
		return false;
	}
	if(filePath.extension() != ".png")
	{
		logError("File extension must be .png to save as PNG. Provided: '{}'", filePath.extension().string());
		return false;
	}
	if(!std::filesystem::exists(std::filesystem::absolute(filePath.parent_path())))
	{
		std::filesystem::create_directories(std::filesystem::absolute(filePath.parent_path()));
	}

	int width = static_cast<int>(image.getWidth());
	int height = static_cast<int>(image.getHeight());
	int channels = static_cast<int>(image.getChannelCount());

	stbi_flip_vertically_on_write(false);

	int result = stbi_write_png(
		filePath.string().c_str(),
		width,
		height,
		channels,
		image.getPixels8().data(),
		width * channels
	);

	if (result == 0)
	{
		logError("Failed to save image as PNG to '{}'", filePath.string());
		return false;
	}

	return true;
}

} // namespace engine::resources::loaders
