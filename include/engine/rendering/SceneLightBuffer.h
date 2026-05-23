#pragma once

#include "engine/rendering/LightUniforms.h"
#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUBindGroup;
} // namespace engine::rendering::webgpu

namespace engine::lighting
{
class LightManager;

/**
 * @class SceneLightBuffer
 * @brief GPU-side light storage with bind group management.
 *
 * Manages GPU-side light storage buffer and associated bind group.
 * Updates from LightManager, provides access for rendering passes.
 * Replaces per-frame light binding in MeshPass for clustered deferred.
 */
class SceneLightBuffer
{
  public:
	explicit SceneLightBuffer(engine::rendering::webgpu::WebGPUContext &context);
	~SceneLightBuffer();

	/**
	 * @brief Update GPU light buffer from light manager.
	 * Writes light data to GPU storage buffer.
	 *
	 * @param lightManager Source of light data
	 */
	void updateFromManager(const LightManager &lightManager);

	/**
	 * @brief Update GPU light buffer from fully prepared light uniforms.
	 * Preserves shadow indices and shadow counts for deferred shading.
	 *
	 * @param lightData Light data already populated with shadow metadata.
	 */
	void updateFromLights(const std::vector<engine::rendering::LightStruct> &lightData);

	/**
	 * @brief Get the GPU storage buffer.
	 * @return WebGPU storage buffer containing all lights
	 */
	wgpu::Buffer getStorageBuffer() const { return m_storageBuffer; }

	/**
	 * @brief Get the bind group for this light buffer.
	 * @return WebGPU bind group (Group 1, for light access)
	 */
	std::shared_ptr<engine::rendering::webgpu::WebGPUBindGroup> getBindGroup() const { return m_bindGroup; }

	/**
	 * @brief Get current light count as GPU-readable uint.
	 * @return Number of lights last updated
	 */
	uint32_t getLightCount() const { return m_lightCount; }

  private:
	engine::rendering::webgpu::WebGPUContext &m_context;
	wgpu::Buffer m_storageBuffer = nullptr;
	std::shared_ptr<engine::rendering::webgpu::WebGPUBindGroup> m_bindGroup;
	uint32_t m_lightCount{0};
	size_t m_bufferCapacity{0};
};

} // namespace engine::lighting
