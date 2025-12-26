#pragma once
#include "engine/rendering/Texture.h"
#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

class WebGPUContext; // forward declaration
/**
 * @class WebGPUTexture
 * @brief GPU-side texture: wraps a WebGPU texture and its view, descriptors, and provides accessors.
 *
 * This class encapsulates a WebGPU texture and its associated view, along with the descriptors
 * used to create them. It provides accessors for all relevant properties and ensures resource
 * cleanup. Used for all GPU-side textures, including color, normal, and depth textures.
 */
class WebGPUTexture
{
  public:
	/**
	 * @brief Constructs a WebGPUTexture from descriptors and GPU objects.
	 *
	 * @param texture The GPU-side texture.
	 * @param textureView The GPU-side texture view.
	 * @param textureDesc The texture descriptor used to create the texture.
	 * @param viewDesc The texture view descriptor used to create the view.
	 *
	 * @throws Assertion failure if width/height are zero or resources are invalid.
	 */
	WebGPUTexture(
		wgpu::Texture texture,
		wgpu::TextureView textureView,
		const wgpu::TextureDescriptor &textureDesc,
		const wgpu::TextureViewDescriptor &viewDesc
	) : m_texture(texture),
		m_textureView(textureView),
		m_textureDesc(textureDesc),
		m_viewDesc(viewDesc),
		m_isSurfaceTexture(texture == nullptr)
	{
		assert(m_textureDesc.size.width > 0 && "Texture width must be > 0");
		assert(m_textureDesc.size.height > 0 && "Texture height must be > 0");
		assert(m_textureView); // Should be valid
	}

	/**
	 * @brief Returns true if this is a surface texture (only view is relevant).
	 */
	bool isSurfaceTexture() const { return m_isSurfaceTexture; }

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 *
	 * Releases the texture view and texture to prevent memory leaks.
	 */
	~WebGPUTexture()
	{
		if (m_textureView)
		{
			m_textureView.release();
		}
		if (m_texture)
		{
			m_texture.release();
		}
	}

	/**
	 * @brief Checks if the buffer matches the given size and format.
	 * @param w Width to check.
	 * @param h Height to check.
	 * @param f Format to check.
	 * @return True if all parameters match, false otherwise.
	 */
	bool matches(uint32_t w, uint32_t h, wgpu::TextureFormat f) const
	{
		return getWidth() == w && getHeight() == h && getFormat() == f;
	}

	/**
	 * @brief Gets the underlying WebGPU texture.
	 * @return The WebGPU texture object.
	 */
	wgpu::Texture getTexture() const { return m_texture; }

	/**
	 * @brief Gets the WebGPU texture view.
	 * @return The WebGPU texture view object.
	 */
	wgpu::TextureView getTextureView() const { return m_textureView; }

	/**
	 * @brief Gets the width of the texture in pixels.
	 * @return Width in pixels.
	 */
	uint32_t getWidth() const { return m_textureDesc.size.width; }

	/**
	 * @brief Gets the height of the texture in pixels.
	 * @return Height in pixels.
	 */
	uint32_t getHeight() const { return m_textureDesc.size.height; }

	/**
	 * @brief Gets the format of the texture.
	 * @return The WebGPU texture format.
	 */
	wgpu::TextureFormat getFormat() const { return m_textureDesc.format; }

	/**
	 * @brief Gets the texture descriptor used for this texture.
	 * @return The WebGPU texture descriptor.
	 */
	const wgpu::TextureDescriptor &getTextureDescriptor() const { return m_textureDesc; }

	/**
	 * @brief Gets the texture view descriptor used for this texture view.
	 * @return The WebGPU texture view descriptor.
	 */
	const wgpu::TextureViewDescriptor &getTextureViewDescriptor() const { return m_viewDesc; }

	/**
	 * @brief Reads back the GPU texture into an existing CPU-side texture.
	 * @param context The WebGPU context.
	 * @param outTexture CPU-side texture to write into. Must have matching width/height/format.
	 * @return True on success, false on failure.
	 */
	bool readbackToCPU(WebGPUContext &context, std::shared_ptr<Texture> outTexture);

	/**
	 * @brief Resizes the texture to the new dimensions if needed.
	 *        Recreates the texture and view if the size or format changes.
	 * @param context The WebGPU context for resource creation.
	 * @param newWidth The new width in pixels.
	 * @param newHeight The new height in pixels.
	 */
	bool resize(WebGPUContext &context, uint32_t newWidth, uint32_t newHeight);

  protected:
	/**
	 * @brief True if this is a surface texture (only view is relevant).
	 */
	bool m_isSurfaceTexture = false;
	/**
	 * @brief True if this is a depth texture.
	 */
	bool m_isDepthTexture = false;
	/**
	 * @brief The underlying WebGPU texture resource.
	 */
	wgpu::Texture m_texture;

	/**
	 * @brief The view of the WebGPU texture.
	 */
	wgpu::TextureView m_textureView;

	/**
	 * @brief Descriptor used to create the texture.
	 */
	wgpu::TextureDescriptor m_textureDesc;

	/**
	 * @brief Descriptor used to create the texture view.
	 */
	wgpu::TextureViewDescriptor m_viewDesc;
};

} // namespace engine::rendering::webgpu
