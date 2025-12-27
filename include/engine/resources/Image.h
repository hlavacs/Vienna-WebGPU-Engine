#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ToDo: Dokumentation
namespace engine::resources
{

enum class ImageFormat
{
	Unknown,
	LDR_RGBA8, // 8-bit per channel
	LDR_RGB8,
	LDR_R8,
	HDR_RGBA16F, // 16-bit float per channel
	HDR_RGB16F,
	HDR_R16F
};

class Image
{
  public:
	using Ptr = std::shared_ptr<Image>;

	Image() = default;

	// Create empty image with given format
	Image(uint32_t width, uint32_t height, ImageFormat format) {
		m_width = width;
		m_height = height;
		m_format = format;
	}

	// Load from pixel data (copy)
	Image(uint32_t width, uint32_t height, ImageFormat format, std::vector<uint8_t> &&ldrPixels) {
		m_width = width;
		m_height = height;
		m_format = format;
		m_ldrPixels = std::move(ldrPixels);
	}
	Image(uint32_t width, uint32_t height, ImageFormat format, std::vector<float> &&hdrPixels) {
		m_width = width;
		m_height = height;
		m_format = format;
		m_hdrPixels = std::move(hdrPixels);
	}

	Image(Image &&) noexcept = default;
	Image &operator=(Image &&) noexcept = default;

	Image(const Image &) = delete;
	Image &operator=(const Image &) = delete;

	uint32_t getWidth() const { return m_width; }
	uint32_t getHeight() const { return m_height; }
	ImageFormat getFormat() const { return m_format; }
	uint32_t getChannelCount() const {
		switch (m_format)
		{
		case ImageFormat::LDR_RGBA8:
		case ImageFormat::HDR_RGBA16F:
			return 4;
		case ImageFormat::LDR_RGB8:
		case ImageFormat::HDR_RGB16F:
			return 3;
		case ImageFormat::LDR_R8:
		case ImageFormat::HDR_R16F:
			return 1;
		default:
			return 0;
		}
	}

	bool isHDR() const {
		return m_format == ImageFormat::HDR_RGBA16F ||
			   m_format == ImageFormat::HDR_RGB16F ||
			   m_format == ImageFormat::HDR_R16F;
	}
	bool isEmpty() const { return m_width == 0 || m_height == 0; }

	const std::vector<uint8_t> &getPixels8() const
	{
		assert(!isHDR());
		return m_ldrPixels;
	}
	const std::vector<float> &getPixelsF() const
	{
		assert(isHDR());
		return m_hdrPixels;
	}

	void replaceData(uint32_t width, uint32_t height, ImageFormat format, std::vector<uint8_t> &&ldrPixels) {
		m_width = width;
		m_height = height;
		m_format = format;
		m_ldrPixels = std::move(ldrPixels);
		m_hdrPixels.clear();
	}
	void replaceData(uint32_t width, uint32_t height, ImageFormat format, std::vector<float> &&hdrPixels) {
		m_width = width;
		m_height = height;
		m_format = format;
		m_hdrPixels = std::move(hdrPixels);
		m_ldrPixels.clear();
	}

  private:
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	ImageFormat m_format = ImageFormat::Unknown;

	// Depending on format, one of these stores the pixels
	std::vector<uint8_t> m_ldrPixels;
	std::vector<float> m_hdrPixels;
};

} // namespace engine::resources
