#pragma once

#include "engine/rendering/LightUniforms.h"
#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUBindGroup;
class WebGPUBuffer;
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
	 * @brief Get the wrapped lights storage buffer. The Scene bind group
	 *        constructed by Renderer::updateSceneBindGroup uses this as its
	 *        @binding(0).
	 */
	std::shared_ptr<engine::rendering::webgpu::WebGPUBuffer> getBufferWrapped() const { return m_bufferWrapped; }

	/**
	 * @brief Get current light count as GPU-readable uint.
	 * @return Number of lights last updated
	 */
	uint32_t getLightCount() const { return m_lightCount; }

	/**
	 * @brief 64-bit fingerprint of the last upload's payload (count + light
	 *        struct bytes). Returns 0 when no upload has occurred yet.
	 *        Consumers (e.g. ClusterManager) use this to short-circuit
	 *        re-computation when the light data hasn't changed.
	 */
	uint64_t getLastUploadHash() const { return m_lastUploadValid ? m_lastUploadHash : 0; }

  private:
	engine::rendering::webgpu::WebGPUContext &m_context;
	wgpu::Buffer m_storageBuffer = nullptr;
	std::shared_ptr<engine::rendering::webgpu::WebGPUBuffer>    m_bufferWrapped;
	std::shared_ptr<engine::rendering::webgpu::WebGPUBindGroup> m_bindGroup; ///< Retained nullptr after Scene consolidation; getBindGroup kept as a transition shim.
	uint32_t m_lightCount{0};
	size_t m_bufferCapacity{0};

	/// Fingerprint of the last upload's (count + LightStruct bytes). Used by
	/// updateFromLights to skip the queue.writeBuffer when the payload is
	/// identical to the previous frame's — common in static scenes and any
	/// time the camera moves but the lights don't. The hash is a 64-bit
	/// FNV-1a over 8-byte blocks, cheap (~10 µs for 1000 lights) compared
	/// to the queue submission it replaces (64 KB of host → GPU traffic).
	uint64_t m_lastUploadHash{0};
	bool     m_lastUploadValid{false};
};

} // namespace engine::lighting
