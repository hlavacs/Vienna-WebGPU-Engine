#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/resources/Image.h"
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace engine::resources
{
class TextureManager; // forward declaration
}

namespace engine::rendering
{
/**
 * @brief Represents a texture resource in the rendering engine.
 */
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

	/**
	 * @brief Checks if a GPU-to-CPU readback is in progress.
	 * @return True if readback is pending.
	 */
	bool isReadbackPending() const
	{
		return m_readbackFuture.valid() && m_readbackFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
	}

	/**
	 * @brief Checks if the last readback completed successfully.
	 * @return True if readback completed and succeeded, false otherwise.
	 */
	bool isReadbackComplete() const
	{
		return m_readbackFuture.valid() && m_readbackFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	/**
	 * @brief Gets the result of the readback operation (blocks if not ready).
	 * @return True if readback succeeded, false if failed or no readback was initiated.
	 */
	bool getReadbackResult()
	{
		if (!m_readbackFuture.valid())
			return false;
		return m_readbackFuture.get();
	}

	/**
	 * @brief Requests a GPU-to-CPU readback on the next render.
	 * Call this when you want to capture the texture data (e.g., for screenshots).
	 * Check isReadbackComplete() later to see if it's ready.
	 */
	void requestReadback()
	{
		m_readbackRequested = true;
	}

	/**
	 * @brief Checks if a readback has been requested.
	 * @return True if readback is requested.
	 */
	bool isReadbackRequested() const
	{
		return m_readbackRequested;
	}

	/**
	 * @brief Sets the readback future (for internal use by WebGPUTexture).
	 * @param future The future representing the async readback operation.
	 */
	void setReadbackFuture(std::future<bool> future)
	{
		m_readbackFuture = std::move(future);
		m_readbackRequested = false; // Clear the request flag
	}

  protected:
	friend class engine::resources::TextureManager;

	Texture(Type type, engine::resources::Image::Ptr image, std::filesystem::path filePath = {}) :
		m_type(type),
		m_image(std::move(image)),
		m_filePath(std::move(filePath))
	{
		if (m_image)
		{
			m_width = m_image->getWidth();
			m_height = m_image->getHeight();
			m_channels = m_image->getChannelCount();
		}
		else
		{
			throw std::runtime_error("Texture constructor: image is null");
		}
	}

	Texture(Type type, uint32_t width, uint32_t height, uint32_t channels, std::filesystem::path filePath = {}) :
		m_type(type),
		m_width(width),
		m_height(height),
		m_channels(channels),
		m_image(nullptr),
		m_filePath(std::move(filePath))
	{
	}

  private:
	Type m_type;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_channels = 0;

	engine::resources::Image::Ptr m_image;
	std::filesystem::path m_filePath;

	mutable std::future<bool> m_readbackFuture; // Future for async GPU-to-CPU readback
	bool m_readbackRequested = false;			// Flag to request readback on next render
};

} // namespace engine::rendering
