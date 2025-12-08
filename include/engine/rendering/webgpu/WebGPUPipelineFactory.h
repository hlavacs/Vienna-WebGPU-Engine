#pragma once

#include <memory>
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUPipelineFactory
{
  public:
	explicit WebGPUPipelineFactory(WebGPUContext &context);

	/**
	 * @brief Creates a WebGPUPipeline wrapper from descriptor and bind group layouts.
	 * @param descriptor The render pipeline descriptor.
	 * @param layouts Array of bind group layouts for the pipeline.
	 * @param layoutCount Number of bind group layouts.
	 * @param shaderInfo Optional shader info to store in the pipeline.
	 * @return Shared pointer to WebGPUPipeline wrapper.
	 */
	std::shared_ptr<WebGPUPipeline> createPipeline(
		const wgpu::RenderPipelineDescriptor &descriptor,
		const wgpu::BindGroupLayout *layouts,
		uint32_t layoutCount,
		std::shared_ptr<WebGPUShaderInfo> shaderInfo = nullptr
	);

	// Create pipeline descriptor
	wgpu::RenderPipelineDescriptor createRenderPipelineDescriptor(
		const wgpu::VertexBufferLayout *vertexBuffers = nullptr,
		uint32_t vertexBufferCount = 1,
		const wgpu::ColorTargetState *colorTargets = nullptr,
		uint32_t colorTargetCount = 0,
		const wgpu::DepthStencilState *depthStencil = nullptr,
		const wgpu::PrimitiveState *primitive = nullptr,
		const wgpu::MultisampleState *multisample = nullptr,
		const WebGPUShaderInfo *vertexShader = nullptr,
		const WebGPUShaderInfo *fragmentShader = nullptr
	);

	// Create pipeline descriptor with only minimum required, using defaults for the rest
	wgpu::RenderPipelineDescriptor createRenderPipelineDescriptor(
		const WebGPUShaderInfo *vertexShader = nullptr,
		const WebGPUShaderInfo *fragmentShader = nullptr,
		wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined,
		wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined,
		bool enableDepth = true
	);

	// Returns a default pipeline (created on first call, then cached)
	std::shared_ptr<WebGPUPipeline> getDefaultRenderPipeline();

	// Helper to create a pipeline layout from bind group layouts
	wgpu::PipelineLayout createPipelineLayout(const wgpu::BindGroupLayout *layouts, uint32_t layoutCount);

  private:
	WebGPUContext &m_context;
	std::shared_ptr<WebGPUPipeline> m_defaultPipeline = nullptr; // Cached default pipeline
	bool m_defaultPipelineInitialized = false;
};

} // namespace engine::rendering::webgpu
