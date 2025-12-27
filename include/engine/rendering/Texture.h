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
		DepthStencil
	};

	Type getType() const { return m_type; }
	engine::resources::Image::Ptr getImage() const { return m_image; }

	uint32_t getWidth() const { return m_image ? m_image->getWidth() : m_width; }
	uint32_t getHeight() const { return m_image ? m_image->getHeight() : m_height; }
	uint32_t getChannels() const { return m_image ? m_image->getChannelCount() : m_channels; }
	const std::filesystem::path &getFilePath() const { return m_filePath; }

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
