#pragma once

#include "engine/core/Identifiable.h"
#include "engine/rendering/webgpu/WebGPUDepthTexture.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include <memory>
#include <webgpu/webgpu.hpp>
#include <optional>

namespace engine::rendering::webgpu
{

/**
 * @brief Encapsulates a WebGPU render pass configuration, managing color and depth textures and their attachments.
 *
 * This buffer holds references to color and depth textures, and maintains a mutable
 * WebGPU RenderPassDescriptor for use in rendering. It provides methods to update
 * attachments, query configuration, and validate texture compatibility.
 */
struct WebGPURenderPassContext : public engine::core::Identifiable<WebGPURenderPassContext>
{
  protected:
	std::vector<std::shared_ptr<WebGPUTexture>> m_colorTextures;
	std::shared_ptr<WebGPUDepthTexture> m_depthTexture;

	std::vector<wgpu::RenderPassColorAttachment> m_colorAttachmentCopies;
	std::optional<wgpu::RenderPassDepthStencilAttachment> m_depthAttachmentCopy;
	wgpu::RenderPassDescriptor m_renderPassDesc;

  public:
	/**
	 * @brief Default constructor. Creates an empty render pass buffer.
	 */
	WebGPURenderPassContext() = default;

	/**
	 * @brief Constructs a render pass buffer with color and depth textures and a descriptor.
	 * @param colorTextures Vector of shared pointers to color textures.
	 * @param depth Shared pointer to depth texture.
	 * @param descriptor Fully configured WebGPU RenderPassDescriptor.
	 *
	 * Copies the attachments from the descriptor and updates their views to match the provided textures.
	 */
	WebGPURenderPassContext(
		std::vector<std::shared_ptr<WebGPUTexture>> colorTextures,
		std::shared_ptr<WebGPUDepthTexture> depth,
		const wgpu::RenderPassDescriptor &descriptor
	) : m_colorTextures(std::move(colorTextures)), m_depthTexture(std::move(depth)), m_renderPassDesc(descriptor)
	{
		m_colorAttachmentCopies.resize(m_renderPassDesc.colorAttachmentCount);
		for (size_t i = 0; i < m_renderPassDesc.colorAttachmentCount; ++i)
		{
			m_colorAttachmentCopies[i] = m_renderPassDesc.colorAttachments[i];
			if (i < m_colorTextures.size() && m_colorTextures[i])
				m_colorAttachmentCopies[i].view = m_colorTextures[i]->getTextureView();
		}
		m_renderPassDesc.colorAttachments = m_colorAttachmentCopies.data();

		if (descriptor.depthStencilAttachment)
		{
			m_depthAttachmentCopy = *descriptor.depthStencilAttachment;
			if (m_depthTexture)
				m_depthAttachmentCopy->view = m_depthTexture->getTextureView();
			m_renderPassDesc.depthStencilAttachment = &(*m_depthAttachmentCopy);
		}
	}

	/**
	 * @brief Returns the color texture at the given index.
	 * @param index Index of the color texture (default 0).
	 * @return Shared pointer to the color texture, or nullptr if out of range.
	 */
	std::shared_ptr<WebGPUTexture> getColorTexture(size_t index = 0) const
	{
		return (index < m_colorTextures.size()) ? m_colorTextures[index] : nullptr;
	}

	/**
	 * @brief Returns the depth texture.
	 * @return Shared pointer to the depth texture, or nullptr if not set.
	 */
	std::shared_ptr<WebGPUDepthTexture> getDepthTexture() const { return m_depthTexture; }

	/**
	 * @brief Returns a mutable reference to the underlying RenderPassDescriptor.
	 * @return Reference to the RenderPassDescriptor.
	 */
	wgpu::RenderPassDescriptor &getRenderPassDescriptor() { return m_renderPassDesc; }

	/**
	 * @brief Returns a const reference to the underlying RenderPassDescriptor.
	 * @return Const reference to the RenderPassDescriptor.
	 */
	const wgpu::RenderPassDescriptor &getRenderPassDescriptor() const { return m_renderPassDesc; }

	/**
	 * @brief Returns the number of color attachments.
	 * @return Number of color attachments in the descriptor.
	 */
	size_t getColorAttachmentCount() const { return m_renderPassDesc.colorAttachmentCount; }

	/**
	 * @brief Checks if a depth attachment is present.
	 * @return True if a depth attachment is configured, false otherwise.
	 */
	bool hasDepthAttachment() const { return m_renderPassDesc.depthStencilAttachment != nullptr; }

	/**
	 * @brief Updates all color texture views and optionally the depth texture view.
	 * @param newColorTextures Vector of new color textures.
	 * @param newDepthTexture Optional new depth texture.
	 * @return True if update succeeded, false if attachment count mismatches or invalid textures.
	 */
	bool updateViews(const std::vector<std::shared_ptr<WebGPUTexture>> &newColorTextures, std::shared_ptr<WebGPUDepthTexture> newDepthTexture = nullptr)
	{
		if (!newColorTextures.empty())
		{
			if (newColorTextures.size() != m_colorAttachmentCopies.size())
				return false; // mismatch in attachment count

			m_colorTextures = newColorTextures;
			for (size_t i = 0; i < m_colorAttachmentCopies.size(); ++i)
			{
				if (!m_colorTextures[i])
					return false; // invalid texture
				m_colorAttachmentCopies[i].view = m_colorTextures[i]->getTextureView();
			}
		}

		if (newDepthTexture)
		{
			if (!m_depthAttachmentCopy)
				return false; // no depth attachment configured
			m_depthTexture = newDepthTexture;
			m_depthAttachmentCopy->view = m_depthTexture->getTextureView();
			m_renderPassDesc.depthStencilAttachment = &(*m_depthAttachmentCopy);
		}

		return true;
	}

	/**
	 * @brief Updates a single color texture view at the given index, and optionally the depth texture view.
	 * @param newColorTexture New color texture to set.
	 * @param newDepthTexture Optional new depth texture to set.
	 * @param colorIndex Index of the color attachment to update (default 0).
	 * @return True if update succeeded, false if index out of range or invalid texture.
	 */
	bool updateView(std::shared_ptr<WebGPUTexture> newColorTexture, std::shared_ptr<WebGPUDepthTexture> newDepthTexture = nullptr, size_t colorIndex = 0)
	{
		if (colorIndex >= m_colorAttachmentCopies.size() || !newColorTexture)
			return false;

		// --- Update color view ---
		m_colorTextures[colorIndex] = newColorTexture;
		m_colorAttachmentCopies[colorIndex].view = newColorTexture->getTextureView();

		// IMPORTANT: rebind color attachments in the descriptor
		m_renderPassDesc.colorAttachments = m_colorAttachmentCopies.data();
		m_renderPassDesc.colorAttachmentCount = m_colorAttachmentCopies.size();

		// --- Update depth view (if provided) ---
		if (newDepthTexture)
		{
			m_depthTexture = newDepthTexture;
			if (m_depthAttachmentCopy)
			{
				m_depthAttachmentCopy->view = newDepthTexture->getTextureView();
				m_renderPassDesc.depthStencilAttachment = &(*m_depthAttachmentCopy);
			}
		}

		return true;
	}

	/**
	 * @brief Updates color texture view from a raw TextureView (for swapchain textures).
	 * @param newColorView Raw texture view (typically from swapchain).
	 * @param newDepthTexture Optional new depth texture to set.
	 * @param colorIndex Index of the color attachment to update (default 0).
	 * @return True if update succeeded.
	 */
	bool updateViewRaw(wgpu::TextureView newColorView, std::shared_ptr<WebGPUDepthTexture> newDepthTexture = nullptr, size_t colorIndex = 0)
	{
		if (colorIndex >= m_colorAttachmentCopies.size())
			return false;

		// --- Update color view directly ---
		m_colorAttachmentCopies[colorIndex].view = newColorView;

		// IMPORTANT: rebind color attachments in the descriptor
		m_renderPassDesc.colorAttachments = m_colorAttachmentCopies.data();
		m_renderPassDesc.colorAttachmentCount = m_colorAttachmentCopies.size();

		// --- Update depth view (if provided) ---
		if (newDepthTexture)
		{
			m_depthTexture = newDepthTexture;
			if (m_depthAttachmentCopy)
			{
				m_depthAttachmentCopy->view = newDepthTexture->getTextureView();
				m_renderPassDesc.depthStencilAttachment = &(*m_depthAttachmentCopy);
			}
		}

		return true;
	}

	/**
	 * @brief Checks if the color texture at the given index matches the specified dimensions and format.
	 * @param index Index of the color texture.
	 * @param w Width to check.
	 * @param h Height to check.
	 * @param f Texture format to check.
	 * @return True if the texture matches, false otherwise.
	 */
	bool colorMatches(size_t index, uint32_t w, uint32_t h, wgpu::TextureFormat f) const
	{
		return index < m_colorTextures.size() && m_colorTextures[index] && m_colorTextures[index]->matches(w, h, f);
	}

	/**
	 * @brief Checks if the depth texture matches the specified dimensions and format.
	 * @param w Width to check.
	 * @param h Height to check.
	 * @param f Texture format to check.
	 * @return True if the depth texture matches, false otherwise.
	 */
	bool depthMatches(uint32_t w, uint32_t h, wgpu::TextureFormat f) const
	{
		return m_depthTexture && m_depthTexture->matches(w, h, f);
	}
};

} // namespace engine::rendering::webgpu