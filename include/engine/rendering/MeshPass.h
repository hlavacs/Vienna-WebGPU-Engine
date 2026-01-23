#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/core/Handle.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

namespace engine::rendering
{
class RenderCollector;
struct FrameCache;

namespace webgpu
{
class WebGPUContext;
} // namespace webgpu

// Forward declaration - defined in RenderTarget.h
struct RenderItemGPU;

/**
 * @class MeshPass
 * @brief Generic mesh rendering pass for models with materials.
 *
 * Handles the main rendering of scene geometry with materials, lighting,
 * and textures. Supports arbitrary render targets and depth buffers.
 */
class MeshPass : public RenderPass
{
  public:
	/**
	 * @brief Construct a mesh rendering pass.
	 * @param context The WebGPU context.
	 */
	explicit MeshPass(std::shared_ptr<webgpu::WebGPUContext> context);

	~MeshPass() override = default;

	/**
	 * @brief Initialize the mesh pass resources.
	 * @return True if initialization succeeded.
	 */
	bool initialize() override;

	/**
	 * @brief Set the render pass context to render into.
	 * @param context Render pass context (color + depth targets, clear flags).
	 */
	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &context)
	{
		m_renderPassContext = context;
	}

	/**
	 * @brief Set the frame uniforms (camera matrices, position, time).
	 * @param uniforms Camera/frame uniforms.
	 */
	void setFrameUniforms(const FrameUniforms &uniforms)
	{
		m_frameUniforms = uniforms;
	}

	/**
	 * @brief Set the camera ID for bind group caching.
	 * @param id Camera identifier.
	 */
	void setCameraId(uint64_t id)
	{
		m_cameraId = id;
	}

	/**
	 * @brief Set visible indices for this render pass.
	 * @param indices Indices of items visible to the camera.
	 */
	void setVisibleIndices(const std::vector<size_t> &indices)
	{
		m_visibleIndices = indices;
	}

	/**
	 * @brief Set shadow resources bind group.
	 * @param bindGroup Shadow uniform and texture bind group.
	 */
	void setShadowBindGroup(const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup)
	{
		m_shadowBindGroup = bindGroup;
	}

	/**
	 * @brief Render meshes using data from FrameCache.
	 * Accesses: frameCache.gpuRenderItems, frameCache.lightUniforms
	 * Additional data from setters: renderPassContext, frameUniforms, cameraId, visibleIndices, shadowBindGroup
	 *
	 * @param frameCache Frame-wide data (GPU items, lights, etc.)
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Clear cached resources (call on scene changes or major updates).
	 */
	void cleanup() override;

	/**
	 * @brief Clear frame-specific bind group cache.
	 */
	void clearFrameBindGroupCache();

  private:
	/**
	 * @brief Bind frame uniforms (camera, view-projection, time).
	 * @param renderPass The render pass encoder.
	 * @param cameraId The camera ID for caching.
	 * @param frameUniforms The frame uniforms data.
	 */
	void bindFrameUniforms(
		wgpu::RenderPassEncoder renderPass,
		uint64_t cameraId,
		const FrameUniforms &frameUniforms
	);

	/**
	 * @brief Bind light uniforms.
	 * @param renderPass The render pass encoder.
	 */
	void bindLightUniforms(wgpu::RenderPassEncoder renderPass);

	/**
	 * @brief Update light data for the frame.
	 * @param lights The light data.
	 */
	void updateLights(const std::vector<LightStruct> &lights);

	/**
	 * @brief Draw all prepared render items.
	 * @param encoder The command encoder.
	 * @param renderPass The render pass encoder.
	 * @param renderPassContext The render pass context.
	 * @param gpuItems The GPU render items to draw.
	 * @param indicesToRender The indices of items to render.
	 */
	void drawItems(
		wgpu::CommandEncoder &encoder,
		wgpu::RenderPassEncoder renderPass,
		const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext,
		const std::vector<std::optional<RenderItemGPU>> &gpuItems,
		const std::vector<size_t> &indicesToRender
	);

	// External dependencies (set via setters)
	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;
	FrameUniforms m_frameUniforms{};
	uint64_t m_cameraId = 0;
	std::vector<size_t> m_visibleIndices;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowBindGroup;

	// Bind group layouts
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_frameBindGroupLayout;
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_lightBindGroupLayout;
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_objectBindGroupLayout;

	// Bind groups
	std::shared_ptr<webgpu::WebGPUBindGroup> m_lightBindGroup;

	// Caching
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_frameBindGroupCache;
};

} // namespace engine::rendering
