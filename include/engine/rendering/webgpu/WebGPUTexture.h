#pragma once
#include "engine/rendering/Texture.h"
#include "engine/resources/Image.h"
#include <memory>
#include <webgpu/webgpu.hpp>
#include <future>

namespace engine::rendering::webgpu
{

class WebGPUContext; // forward declaration

/**
 * @class WebGPUTexture
 * @brief GPU-side texture: wraps a WebGPU texture and its view, descriptors, and provides accessors.
 *
 * This class encapsulates a WebGPU texture and its associated view, along with the descriptors
 * used to create them. Supports standard textures, render targets, surface textures, and depth textures.
 */
class WebGPUTexture
{
	using ImageFormatType = engine::resources::ImageFormat::Type;

  public:
	/**
	 * @brief Constructs a WebGPUTexture from descriptors and GPU objects.
	 *
	 * @param texture The GPU-side texture (nullptr for Surface textures).
	 * @param textureView The GPU-side texture view.
	 * @param textureDesc The texture descriptor used to create the texture.
	 * @param viewDesc The texture view descriptor used to create the view.
	 * @param type The type of texture (Standard, RenderTarget, Surface, DepthStencil).
	 * @param cpuHandle Optional CPU-side Texture handle.
	 */
	WebGPUTexture(
		wgpu::Texture texture,
		wgpu::TextureView textureView,
		const wgpu::TextureDescriptor &textureDesc,
		const wgpu::TextureViewDescriptor &viewDesc,
		Texture::Type type = Texture::Type::Image,
		std::shared_ptr<Texture> cpuHandle = nullptr
	) : m_texture(texture),
		m_textureView(textureView),
		m_textureDesc(textureDesc),
		m_viewDesc(viewDesc),
		m_type(type),
		m_cpuHandle(cpuHandle)
	{
		assert((type == Texture::Type::Surface || texture) && "WebGPUTexture: Texture cannot be null for non-surface types.");
		assert(m_textureView && "WebGPUTexture: TextureView cannot be null.");
	}

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
	 * @brief Returns true if this is a surface texture (only view is relevant).
	 */
	bool isSurfaceTexture() const { return m_type == Texture::Type::Surface; }

	/**
	 * @brief Returns true if this is a depth texture.
	 */
	bool isDepthTexture() const { return m_type == Texture::Type::DepthStencil; }

	/**
	 * @brief Returns the CPU-side texture handle if available.
	 */
	std::shared_ptr<Texture> getCPUHandle() const { return m_cpuHandle; }

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
	std::future<bool> readbackToCPUAsync(WebGPUContext &context, std::shared_ptr<Texture> outTexture);

	/**
	 * @brief Resizes the texture to the new dimensions if needed.
	 *        Recreates the texture and view if the size or format changes.
	 * @param context The WebGPU context for resource creation.
	 * @param newWidth The new width in pixels.
	 * @param newHeight The new height in pixels.
	 */
	bool resize(WebGPUContext &context, uint32_t newWidth, uint32_t newHeight);

	/**
	 * @brief Maps an ImageFormat::Type to a WebGPU texture format.
	 */
	static wgpu::TextureFormat mapImageFormatToGPU(ImageFormatType format)
	{
		switch (format)
		{
		case ImageFormatType::LDR_R8:
			return wgpu::TextureFormat::R8Unorm;
		case ImageFormatType::LDR_RG8:
			return wgpu::TextureFormat::RG8Unorm;
		case ImageFormatType::LDR_RGBA8:
			return wgpu::TextureFormat::RGBA8Unorm;
		case ImageFormatType::HDR_R16F:
			return wgpu::TextureFormat::RGBA32Float;
		case ImageFormatType::HDR_RG16F:
			return wgpu::TextureFormat::RGBA32Float;
		case ImageFormatType::HDR_RGBA16F:
			return wgpu::TextureFormat::RGBA16Float;
		default:
			assert(false && "Unsupported ImageFormat for GPU mapping");
			return wgpu::TextureFormat::RGBA8Unorm;
		}
	}

	/**
	 * @brief Maps a WebGPU texture format to the corresponding ImageFormat::Type.
	 */
	static ImageFormatType mapGPUFormatToImageFormat(wgpu::TextureFormat format)
	{
		switch (format)
		{
		case wgpu::TextureFormat::R8Unorm:
			return ImageFormatType::LDR_R8;
		case wgpu::TextureFormat::RG8Unorm:
			return ImageFormatType::LDR_RG8;
		case wgpu::TextureFormat::RGBA8Unorm:
			return ImageFormatType::LDR_RGBA8;
		case wgpu::TextureFormat::R16Float:
			return ImageFormatType::HDR_R16F;
		case wgpu::TextureFormat::RG16Float:
			return ImageFormatType::HDR_RG16F;
		case wgpu::TextureFormat::RGBA16Float:
			return ImageFormatType::HDR_RGBA16F;
		default:
			assert(false && "Unsupported GPU texture format for ImageFormat mapping");
			return ImageFormatType::LDR_RGBA8;
		}
	}

  protected:
	Texture::Type m_type = Texture::Type::Image;	//< The type of texture.
	std::shared_ptr<Texture> m_cpuHandle = nullptr; //< Optional CPU-side texture handle.
	wgpu::Texture m_texture;						//< The underlying WebGPU texture resource.
	wgpu::TextureView m_textureView;				//< The view of the WebGPU texture.
	wgpu::TextureDescriptor m_textureDesc;			//< Descriptor used to create the texture.
	wgpu::TextureViewDescriptor m_viewDesc;			//< Descriptor used to create the texture view.
};

} // namespace engine::rendering::webgpu
