#pragma once

#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering::webgpu
{
	class WebGPUContext;

	class WebGPUPipelineFactory
	{
	public:
		explicit WebGPUPipelineFactory(WebGPUContext &context);

		wgpu::RenderPipeline createRenderPipeline(const wgpu::RenderPipelineDescriptor &descriptor);

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
			const WebGPUShaderInfo *fragmentShader = nullptr);

		// Create pipeline descriptor with only minimum required, using defaults for the rest
		wgpu::RenderPipelineDescriptor createRenderPipelineDescriptor(
			const WebGPUShaderInfo *vertexShader = nullptr,
			const WebGPUShaderInfo *fragmentShader = nullptr,
			wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined,
			wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined,
			bool enableDepth = true);

		// Returns a default pipeline (created on first call, then cached)
		wgpu::RenderPipeline getDefaultRenderPipeline();

	private:
		WebGPUContext &m_context;
		wgpu::RenderPipeline m_defaultPipeline = nullptr; // Cached default pipeline
		bool m_defaultPipelineInitialized = false;
	};

} // namespace engine::rendering::webgpu
