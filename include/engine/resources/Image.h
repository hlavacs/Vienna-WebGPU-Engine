#pragma once
#include "engine/core/Enum.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ToDo: Dokumentation
namespace engine::resources
{

ENUM_BEGIN_WRAPPED(ImageFormat, 9, Unknown, LDR_RGBA8, LDR_RGB8, LDR_RG8, LDR_R8, HDR_RGBA16F, HDR_RGB16F, HDR_RG16F, HDR_R16F)

/**
 * @brief Gets the number of color channels for the given format.
 * @param format ImageFormat to check
 * @return Number of channels (1-4), or 0 if unknown format.
 */
static uint32_t getChannelCount(ImageFormat::Type format)
{
	switch (format)
	{
	case ImageFormat::Type::LDR_RGBA8:
	case ImageFormat::Type::HDR_RGBA16F:
		return 4;
	case ImageFormat::Type::LDR_RGB8:
	case ImageFormat::Type::HDR_RGB16F:
		return 3;
	case ImageFormat::Type::LDR_RG8:
	case ImageFormat::Type::HDR_RG16F:
		return 2;
	case ImageFormat::Type::LDR_R8:
	case ImageFormat::Type::HDR_R16F:
		return 1;
	default:
		return 0;
	}
	return 0;
}

/**
 * @brief Gets the image format from the number of channels and HDR flag.
 * @param channels Number of color channels (1-4)
 * @param hdr Whether the image is HDR (floating point)
 * @return Corresponding ImageFormat enum value.
 */
static ImageFormat::Type formatFromChannels(uint32_t channels, bool hdr)
{
	switch (channels)
	{
	case 1:
		return hdr ? ImageFormat::Type::HDR_R16F : ImageFormat::Type::LDR_R8;
	case 2:
		return hdr ? ImageFormat::Type::HDR_RG16F : ImageFormat::Type::LDR_RG8;
	case 3:
		return hdr ? ImageFormat::Type::HDR_RGB16F : ImageFormat::Type::LDR_RGB8;
	case 4:
		return hdr ? ImageFormat::Type::HDR_RGBA16F : ImageFormat::Type::LDR_RGBA8;
	}
	return ImageFormat::Type::Unknown;
}

/**
 * @brief Checks if the image format is an LDR format.
 * @param format ImageFormat to check
 * @return True if the format is LDR, false otherwise.
 */
static bool isLDRFormat(ImageFormat::Type format)
{
	return format == ImageFormat::Type::LDR_RGBA8 || format == ImageFormat::Type::LDR_RGB8 || format == ImageFormat::Type::LDR_RG8 || format == ImageFormat::Type::LDR_R8;
}

/**
 * @brief Checks if the given format is an HDR format.
 * @param format ImageFormat to check
 * @return True if the format is HDR, false otherwise.
 */
static bool isHDRFormat(ImageFormat::Type format)
{
	return format == ImageFormat::Type::HDR_RGBA16F || format == ImageFormat::Type::HDR_RGB16F || format == ImageFormat::Type::HDR_RG16F || format == ImageFormat::Type::HDR_R16F;
}
ENUM_END()

using ImageFormatType = ImageFormat::Type;

class Image
{
  public:
	using Ptr = std::shared_ptr<Image>;

	Image() = default;

	// Create empty image with given format
	Image(uint32_t width, uint32_t height, ImageFormat::Type format) : m_width(width), m_height(height), m_format(format)
	{
	}

	// Load from pixel data (copy)
	Image(uint32_t width, uint32_t height, ImageFormat::Type format, std::vector<uint8_t> &&ldrPixels) : m_width(width), m_height(height), m_format(format), m_ldrPixels(std::move(ldrPixels))
	{
	}
	Image(uint32_t width, uint32_t height, ImageFormat::Type format, std::vector<float> &&hdrPixels) : m_width(width), m_height(height), m_format(format), m_hdrPixels(std::move(hdrPixels))
	{
	}

	Image(Image &&) = default;
	Image &operator=(Image &&) = default;

	Image(const Image &) = delete;
	Image &operator=(const Image &) = delete;

	[[nodiscard]] uint32_t getWidth() const { return m_width; }
	[[nodiscard]] uint32_t getHeight() const { return m_height; }
	[[nodiscard]] ImageFormat::Type getFormat() const { return m_format; }
	[[nodiscard]] uint32_t getChannelCount() const
	{
		return ImageFormat::getChannelCount(m_format);
	}
	[[nodiscard]] bool isLDR() const
	{
		return ImageFormat::isLDRFormat(m_format);
	}
	[[nodiscard]] bool isHDR() const
	{
		return ImageFormat::isHDRFormat(m_format);
	}
	[[nodiscard]] bool isEmpty() const { return m_width == 0 || m_height == 0; }

	[[nodiscard]] const std::vector<uint8_t> &getPixels8() const
	{
		assert(!isHDR());
		return m_ldrPixels;
	}
	[[nodiscard]] const std::vector<float> &getPixelsF() const
	{
		assert(isHDR());
		return m_hdrPixels;
	}
	/**
	 * @brief Replace the image data with new pixel data.
	 * @param width New image width
	 * @param height New image height
	 * @param format New image format
	 * @param ldrPixels New LDR pixel data (if format is LDR)
	 * @throws std::runtime_error if format is not LDR
	 */
	void replaceData(uint32_t width, uint32_t height, ImageFormat::Type format, std::vector<uint8_t> &&ldrPixels)
	{
		if (ImageFormat::isHDRFormat(format))
		{
			throw std::runtime_error("Image::replaceData: format does not match LDR pixel data");
		}
		m_width = width;
		m_height = height;
		m_format = format;
		m_ldrPixels = std::move(ldrPixels);
		m_hdrPixels.clear();
	}
	/**
	 * @brief Replace the image data with new pixel data.
	 * @param width New image width
	 * @param height New image height
	 * @param format New image format
	 * @param hdrPixels New HDR pixel data (if format is HDR)
	 * @throws std::runtime_error if format is not HDR
	 */
	void replaceData(uint32_t width, uint32_t height, ImageFormat::Type format, std::vector<float> &&hdrPixels)
	{
		if (ImageFormat::isLDRFormat(format))
		{
			throw std::runtime_error("Image::replaceData: format does not match HDR pixel data");
		}
		m_width = width;
		m_height = height;
		m_format = format;
		m_hdrPixels = std::move(hdrPixels);
		m_ldrPixels.clear();
	}

  private:
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	ImageFormat::Type m_format = ImageFormat::Type::Unknown;

	// Depending on format, one of these stores the pixels
	std::vector<uint8_t> m_ldrPixels;
	std::vector<float> m_hdrPixels;
};

} // namespace engine::resources
