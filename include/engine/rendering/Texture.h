#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/resources/Image.h"
#include <string>
#include <vector>

namespace engine::resources
{
class TextureManager; // forward declaration
}
namespace engine::rendering

{
struct Texture : public engine::core::Identifiable<Texture>, public engine::core::Versioned
{
	using Handle = engine::core::Handle<Texture>;
	using Ptr = std::shared_ptr<Texture>;

	enum class Type
	{
		Image,
		RenderTarget,
		Surface,
		Depth,
		DepthStencil
	};

	Type getType() const { return m_type; }
	engine::resources::Image::Ptr getImage() const { return m_image; }

	uint32_t getWidth() const { return m_image ? m_image->getWidth() : m_width; }
	uint32_t getHeight() const { return m_image ? m_image->getHeight() : m_height; }
	uint32_t getChannels() const { return m_image ? m_image->getChannelCount() : m_channels; }
	const std::filesystem::path &getFilePath() const { return m_filePath; }

	/* @brief Returns true if the texture can be resized (e.g., render targets).
	 * @return True if resizeable, false otherwise.
	 */
	bool isResizeable() const
	{
		return m_type == Type::RenderTarget || m_type == Type::Surface || m_type == Type::DepthStencil;
	}

	/* @brief Returns true if the texture data is readable on the CPU side.
	 * @return True if readable, false otherwise.
	 */
	bool isDataReadable() const
	{
		return m_type == Type::Image && m_image != nullptr;
	}

	/* @brief Resizes the texture if it is resizeable.
	 * @param newWidth New width in pixels.
	 * @param newHeight New height in pixels.
	 * @return True if resize succeeded, false otherwise.
	 */
	bool resize(uint32_t newWidth, uint32_t newHeight)
	{
		if (!isResizeable())
			return false;
		m_width = newWidth;
		m_height = newHeight;
		incrementVersion();
		return true;
	}

	/*
	 * @brief Replaces the texture's image data (for image textures).
	 * @param newImage New image data.
	 * @return True if replacement succeeded, false otherwise.
	 */
	bool replaceImageData(engine::resources::Image::Ptr newImage)
	{
		if (m_type != Type::Image || !newImage)
			return false;
		m_image = std::move(newImage);
		m_width = m_image->getWidth();
		m_height = m_image->getHeight();
		m_channels = m_image->getChannelCount();
		incrementVersion();
		return true;
	}

  private:
	friend class engine::resources::TextureManager;

	Texture(
		Type type,
		uint32_t width,
		uint32_t height,
		uint32_t channels,
		engine::resources::Image::Ptr image = nullptr,
		std::filesystem::path filePath = {}
	) : m_type(type), m_width(width), m_height(height), m_channels(channels),
		m_image(std::move(image)), m_filePath(std::move(filePath)) {}

	Type m_type;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_channels = 0;

	engine::resources::Image::Ptr m_image;
	std::filesystem::path m_filePath;
};

} // namespace engine::rendering
