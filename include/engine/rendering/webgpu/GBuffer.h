#pragma once

#include <memory>
#include <array>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUTexture;

/**
 * @class GBuffer
 * @brief Container for deferred rendering G-buffers (4 render targets).
 *
 * Holds the following render target textures:
 * - Position: R32G32B32A32Float (world space position, depth in A for clustering)
 * - Normal: R32G32B32A32Float (normalized world-space normal)
 * - Albedo: R8G8B8A8Unorm (sRGB base color)
 * - Material: R8G8B8A8Unorm (roughness, metallic, AO packed into channels)
 *
 * Also manages a depth texture for depth testing during geometry rendering.
 */
class GBuffer
{
  public:
	explicit GBuffer(WebGPUContext &context, uint32_t width, uint32_t height);
	~GBuffer() = default;

	/**
	 * @brief Resize all G-buffers to new dimensions.
	 * Recreates all textures if size changes.
	 *
	 * @param width New width in pixels
	 * @param height New height in pixels
	 */
	void resize(uint32_t width, uint32_t height);

	/**
	 * @brief Get width of G-buffers.
	 */
	[[nodiscard]] uint32_t getWidth() const { return m_width; }

	/**
	 * @brief Get height of G-buffers.
	 */
	[[nodiscard]] uint32_t getHeight() const { return m_height; }

	/**
	 * @brief Get position texture (R32G32B32A32Float).
	 */
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getPositionTexture() const { return m_positionTexture; }

	/**
	 * @brief Get normal texture (R32G32B32A32Float).
	 */
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getNormalTexture() const { return m_normalTexture; }

	/**
	 * @brief Get albedo texture (R8G8B8A8Unorm, sRGB).
	 */
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getAlbedoTexture() const { return m_albedoTexture; }

	/**
	 * @brief Get material texture (R8G8B8A8Unorm: R=roughness, G=metallic, B=AO, A=unused).
	 */
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getMaterialTexture() const { return m_materialTexture; }

	/**
	 * @brief Get depth texture.
	 */
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getDepthTexture() const { return m_depthTexture; }

	/**
	 * @brief Get color attachment descriptor for position.
	 */
	[[nodiscard]] wgpu::RenderPassColorAttachment getPositionAttachment() const;

	/**
	 * @brief Get color attachment descriptor for normal.
	 */
	[[nodiscard]] wgpu::RenderPassColorAttachment getNormalAttachment() const;

	/**
	 * @brief Get color attachment descriptor for albedo.
	 */
	[[nodiscard]] wgpu::RenderPassColorAttachment getAlbedoAttachment() const;

	/**
	 * @brief Get color attachment descriptor for material.
	 */
	[[nodiscard]] wgpu::RenderPassColorAttachment getMaterialAttachment() const;

	/**
	 * @brief Get all color attachments as array.
	 */
	[[nodiscard]] std::array<wgpu::RenderPassColorAttachment, 4> getAllColorAttachments() const;

	/**
	 * @brief Get depth attachment descriptor.
	 */
	[[nodiscard]] wgpu::RenderPassDepthStencilAttachment getDepthAttachment() const;

  private:
	WebGPUContext &m_context;
	uint32_t m_width;
	uint32_t m_height;

	std::shared_ptr<WebGPUTexture> m_positionTexture;
	std::shared_ptr<WebGPUTexture> m_normalTexture;
	std::shared_ptr<WebGPUTexture> m_albedoTexture;
	std::shared_ptr<WebGPUTexture> m_materialTexture;
	std::shared_ptr<WebGPUTexture> m_depthTexture;

	void createTextures();
};

} // namespace engine::rendering::webgpu
