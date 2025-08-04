#pragma once
#include <memory>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"

namespace engine::rendering::webgpu
{
/**
 * @brief Options for configuring a WebGPUTexture.
 */
struct WebGPUTextureOptions
{
};

/**
 * @class WebGPUTexture
 * @brief GPU-side texture: wraps a WebGPU texture and view.
 */
class WebGPUTexture : public WebGPURenderObject<engine::rendering::Texture>
{
  public:
	/**
	 * @brief Construct a WebGPUTexture from a Texture handle and GPU texture.
	 * @param context The WebGPU context.
	 * @param textureHandle Handle to the CPU-side Texture.
	 * @param texture The GPU-side texture.
	 * @param textureView The GPU-side texture view.
	 * @param width Width of the texture.
	 * @param height Height of the texture.
	 * @param format Format of the texture.
	 * @param options Optional texture options.
	 */
	WebGPUTexture(
		WebGPUContext &context,
		const engine::rendering::Texture::Handle &textureHandle,
		wgpu::Texture texture,
		wgpu::TextureView textureView,
		uint32_t width,
		uint32_t height,
		wgpu::TextureFormat format,
		WebGPUTextureOptions options = {}
	);

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 */
	~WebGPUTexture() override
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
	 * @brief Render the texture (used for debug visualization).
	 * @param encoder The command encoder.
	 * @param renderPass The render pass.
	 */
	void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass) override;

	/**
	 * @brief Get the WebGPU texture.
	 * @return The WebGPU texture.
	 */
	wgpu::Texture getTexture() const { return m_texture; }

	/**
	 * @brief Get the WebGPU texture view.
	 * @return The WebGPU texture view.
	 */
	wgpu::TextureView getTextureView() const { return m_textureView; }

	/**
	 * @brief Get the width of the texture.
	 * @return Width in pixels.
	 */
	uint32_t getWidth() const { return m_width; }

	/**
	 * @brief Get the height of the texture.
	 * @return Height in pixels.
	 */
	uint32_t getHeight() const { return m_height; }

	/**
	 * @brief Get the format of the texture.
	 * @return The WebGPU texture format.
	 */
	wgpu::TextureFormat getFormat() const { return m_format; }

	/**
	 * @brief Get the texture options.
	 * @return The texture options.
	 */
	const WebGPUTextureOptions &getOptions() const { return m_options; }

  protected:
	/**
	 * @brief Update GPU resources when CPU texture changes.
	 */
	void updateGPUResources() override;

  private:
	wgpu::Texture m_texture;
	wgpu::TextureView m_textureView;
	uint32_t m_width;
	uint32_t m_height;
	wgpu::TextureFormat m_format;
	WebGPUTextureOptions m_options;
};

} // namespace engine::rendering::webgpu
