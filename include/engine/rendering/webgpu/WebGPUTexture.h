#pragma once
/**
 * @file WebGPUTexture.h
 * @brief GPU-side texture: wraps wgpu::Texture and wgpu::TextureView.
 */
#include <webgpu/webgpu.hpp>
#include <cstdint>

namespace engine::rendering::webgpu
{

	/**
	 * @class WebGPUTexture
	 * @brief Uploads Texture data to GPU and creates a view.
	 */
	class WebGPUTexture
	{
	public:
		/**
		 * @brief Construct from explicit properties.
		 */
		WebGPUTexture(wgpu::Texture texture, wgpu::TextureView view, uint32_t width, uint32_t height, wgpu::TextureFormat format);
		/** @brief Get the GPU texture. */
		wgpu::Texture getTexture() const;
		/** @brief Get the texture view. */
		wgpu::TextureView getTextureView() const;
		/** @brief Get the texture width. */
		uint32_t getWidth() const;
		/** @brief Get the texture height. */
		uint32_t getHeight() const;
		/** @brief Get the texture format. */
		wgpu::TextureFormat getFormat() const;

	private:
		wgpu::Texture m_texture;
		wgpu::TextureView m_view;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		wgpu::TextureFormat m_format = wgpu::TextureFormat::Undefined;
	};

} // namespace engine::rendering::webgpu
