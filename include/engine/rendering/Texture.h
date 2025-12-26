#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include <string>
#include <vector>

namespace engine::rendering
{
struct Texture : public engine::core::Identifiable<Texture>, public engine::core::Versioned
{
  public:
	using Handle = engine::core::Handle<Texture>;
	using Ptr = std::shared_ptr<Texture>;

	Texture() = default;

	// Construct with pixel data once
	Texture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixelsData);

	// Construct with pixel data and file path (default empty)
	Texture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixelsData, const std::string &filePath);

	// Construct with pixel data, file path and name (both default empty)
	Texture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixelsData, const std::string &filePath, const std::string &name);

	// Allow move
	Texture(Texture &&) noexcept = default;
	Texture &operator=(Texture &&) noexcept = default;

	// Disallow copy to avoid accidental modifications
	Texture(const Texture &) = delete;
	Texture &operator=(const Texture &) = delete;

	bool empty() const;

	uint32_t getWidth() const;
	uint32_t getHeight() const;
	uint32_t getChannels() const;
	uint32_t getMipLevelCount() const;
	bool isMipped() const;

	// Provide const accessors to pixel data and mipmaps
	const std::vector<uint8_t> &getPixels() const;
	const std::vector<std::vector<uint8_t>> &getMipmaps() const;

	// Get file path (empty string if none)
	const std::string &getFilePath() const;

	// Set file path
	void setFilePath(const std::string &filepath);

	/**
	 * @brief Replace the pixel data and dimensions of the texture.
	 * @param newPixels New pixel data.
	 * @param newWidth New width in pixels.
	 * @param newHeight New height in pixels.
	 * @param newChannels New number of channels.
	 * @note This invalidates any existing mipmaps.
	 */
	void replaceData(const std::vector<uint8_t> &newPixels, uint32_t newWidth, uint32_t newHeight, uint32_t newChannels) {
		m_width = newWidth;
		m_height = newHeight;
		m_channels = newChannels;
		pixels = newPixels;
		mips.clear();
		m_isMipped = false;
		incrementVersion();
	}

	// Generates mipmaps, modifies only mips vector
	void generateMipmaps();

  private:
	static uint32_t bit_width(uint32_t m);

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_channels = 0;
	uint32_t m_mipmapCount = 0;

	bool m_isMipped = false;

	std::vector<uint8_t> pixels;			// base level pixel data (RGBA or RGB)
	std::vector<std::vector<uint8_t>> mips; // mipmap levels

	std::string m_filePath; // empty if not set
};
} // namespace engine::rendering
