#pragma once

#include "engine/rendering/MeshPass.h"
#include <memory>

namespace engine::rendering::webgpu
{
class GBuffer;
}

namespace engine::rendering
{

/**
 * @class GBufferPass
 * @brief Render pass for deferred shading G-buffer generation.
 *
 * Inherits from MeshPass and uses the G-buffer shader instead of forward PBR.
 * Outputs geometry data to 4 render targets: Position, Normal, Albedo, Material.
 * Reuses material bind groups and mesh batching from MeshPass.
 */
class GBufferPass : public MeshPass
{
  public:
	explicit GBufferPass(std::shared_ptr<webgpu::WebGPUContext> context);
	~GBufferPass() override = default;

	/**
	 * @brief Initialize the G-buffer pass.
	 * Creates G-buffer render targets and validates shader setup.
	 *
	 * @return True if initialization succeeded, false otherwise.
	 */
	bool initialize() override;

	/**
	 * @brief Get the G-buffer instance (read-only).
	 */
	[[nodiscard]] std::shared_ptr<webgpu::GBuffer> getGBuffer() const { return m_gBuffer; }

	/**
	 * @brief Resize G-buffers to match render target dimensions.
	 * Called during camera resize or viewport changes.
	 *
	 * @param width New width
	 * @param height New height
	 */
	void resizeGBuffer(uint32_t width, uint32_t height);

  private:
	std::shared_ptr<webgpu::GBuffer> m_gBuffer;
};

} // namespace engine::rendering
