#include "engine/rendering/Texture.h"

namespace engine::rendering
{

Texture::Texture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixelsData) :
	Identifiable<Texture>(),
	m_width(width),
	m_height(height),
	m_channels(channels),
	pixels(std::move(pixelsData)) {}

Texture::Texture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixelsData, const std::string &filePath) :
	engine::core::Identifiable<Texture>(),
	m_width(width),
	m_height(height),
	m_channels(channels),
	m_filePath(std::move(filePath)),
	pixels(std::move(pixelsData)) {}

Texture::Texture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixelsData, const std::string &filePath, const std::string &name) :
	engine::core::Identifiable<Texture>(std::move(name)),
	m_width(width),
	m_height(height),
	m_channels(channels),
	m_filePath(std::move(filePath)),
	pixels(std::move(pixelsData)) {}

bool Texture::empty() const
{
	return pixels.empty();
}

uint32_t Texture::getWidth() const
{
	return m_width;
}

uint32_t Texture::getHeight() const
{
	return m_height;
}

uint32_t Texture::getChannels() const
{
	return m_channels;
}

uint32_t Texture::getMipLevelCount() const
{
	return m_mipmapCount;
}

bool Texture::isMipped() const
{
	return m_isMipped;
}

const std::vector<uint8_t> &Texture::getPixels() const
{
	return m_isMipped ? mips[0] : pixels;
}

const std::vector<std::vector<uint8_t>> &Texture::getMipmaps() const
{
	return mips;
}

const std::string &Texture::getFilePath() const
{
	return m_filePath;
}

void Texture::setFilePath(const std::string &filepath)
{
	m_filePath = filepath;
	incrementVersion();
}

void Texture::generateMipmaps()
{
	if (m_isMipped)
		return;

	mips.clear();
	mips.push_back(pixels);
	pixels.clear();

	incrementVersion();

	// Calculate total mip levels (including base)
	const uint32_t maxDim = std::max(m_width, m_height);
	const uint32_t mipCount = 1 + static_cast<uint32_t>(std::floor(std::log2(maxDim)));

	uint32_t mipWidth = m_width;
	uint32_t mipHeight = m_height;
	m_mipmapCount = 1;

	for (uint32_t level = 1; level < mipCount; ++level)
	{
		const uint32_t nextWidth = std::max(1u, mipWidth / 2);
		const uint32_t nextHeight = std::max(1u, mipHeight / 2);

		const auto &prevMip = mips.back();
		std::vector<uint8_t> mipData(m_channels * nextWidth * nextHeight);

		for (uint32_t y = 0; y < nextHeight; ++y)
		{
			for (uint32_t x = 0; x < nextWidth; ++x)
			{
				for (uint32_t c = 0; c < m_channels; ++c)
				{
					uint32_t sum = 0;
					uint32_t count = 0;

					// Average up to 4 texels from previous level, clamp edges if needed
					for (uint32_t dy = 0; dy < 2; ++dy)
					{
						for (uint32_t dx = 0; dx < 2; ++dx)
						{
							const uint32_t srcX = 2 * x + dx;
							const uint32_t srcY = 2 * y + dy;

							if (srcX < mipWidth && srcY < mipHeight)
							{
								sum += prevMip[m_channels * (srcY * mipWidth + srcX) + c];
								++count;
							}
						}
					}

					mipData[m_channels * (y * nextWidth + x) + c] = static_cast<uint8_t>(sum / count);
				}
			}
		}

		mips.push_back(std::move(mipData));
		mipWidth = nextWidth;
		mipHeight = nextHeight;
		++m_mipmapCount;
	}

	m_isMipped = true;
}

uint32_t Texture::bit_width(uint32_t m)
{
	if (m == 0)
		return 0;

	uint32_t w = 0;
	while (m >>= 1)
		++w;
	return w;
}

} // namespace engine::rendering
