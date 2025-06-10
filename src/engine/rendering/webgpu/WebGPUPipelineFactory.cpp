#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include <webgpu/webgpu.hpp>
#include <vector>
#include <array>
#include <iostream>

namespace engine::rendering::webgpu
{

	WebGPUPipelineFactory::WebGPUPipelineFactory(WebGPUContext &context)
		: m_context(context), m_defaultPipelineInitialized(false)
	{ }

	wgpu::RenderPipeline WebGPUPipelineFactory::createRenderPipeline(const wgpu::RenderPipelineDescriptor &descriptor)
	{
		wgpu::RenderPipelineDescriptor desc = descriptor;
		// The layout must be set by the caller before calling this, or after descriptor creation
		return m_context.getDevice().createRenderPipeline(desc);
	}

	wgpu::RenderPipelineDescriptor WebGPUPipelineFactory::createRenderPipelineDescriptor(
		const wgpu::VertexBufferLayout *vertexBuffers,
		uint32_t vertexBufferCount,
		const wgpu::ColorTargetState *colorTargets,
		uint32_t colorTargetCount,
		const wgpu::DepthStencilState *depthStencil,
		const wgpu::PrimitiveState *primitive,
		const wgpu::MultisampleState *multisample,
		const WebGPUShaderInfo *vertexShader,
		const WebGPUShaderInfo *fragmentShader)
	{
		wgpu::RenderPipelineDescriptor desc = {};
		if (vertexShader)
		{
			desc.vertex.module = vertexShader->module;
			desc.vertex.entryPoint = vertexShader->entryPoint.c_str();
		}
		desc.vertex.bufferCount = vertexBufferCount;
		desc.vertex.buffers = vertexBuffers;
		desc.vertex.constantCount = 0;
		desc.vertex.constants = nullptr;
		if (fragmentShader && fragmentShader->module)
		{
			static wgpu::FragmentState fragmentState = {};
			fragmentState.module = fragmentShader->module;
			fragmentState.entryPoint = fragmentShader->entryPoint.c_str();
			fragmentState.targetCount = colorTargetCount;
			fragmentState.targets = colorTargets;
			fragmentState.constantCount = 0;
			fragmentState.constants = nullptr;
			desc.fragment = &fragmentState;
		}
		else
		{
			desc.fragment = nullptr;
		}

		if (depthStencil)
			desc.depthStencil = depthStencil;
		if (primitive)
			desc.primitive = *primitive;
		else
		{
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
			desc.primitive.frontFace = wgpu::FrontFace::CCW;
			desc.primitive.cullMode = wgpu::CullMode::None;
		}

		if (multisample)
			desc.multisample = *multisample;
		else
		{
			desc.multisample.count = 1;
			desc.multisample.mask = ~0u;
			desc.multisample.alphaToCoverageEnabled = false;
		}

		return desc;
	}

	wgpu::RenderPipelineDescriptor WebGPUPipelineFactory::createRenderPipelineDescriptor(
		const WebGPUShaderInfo *vertexShader,
		const WebGPUShaderInfo *fragmentShader,
		wgpu::TextureFormat colorFormat,
		wgpu::TextureFormat depthFormat,
		bool enableDepth)
	{
		std::vector<wgpu::VertexAttribute> defaultAttribs(6);
		defaultAttribs[0].shaderLocation = 0;
		defaultAttribs[0].format = wgpu::VertexFormat::Float32x3;
		defaultAttribs[0].offset = offsetof(Vertex, position);
		defaultAttribs[1].shaderLocation = 1;
		defaultAttribs[1].format = wgpu::VertexFormat::Float32x3;
		defaultAttribs[1].offset = offsetof(Vertex, normal);
		defaultAttribs[2].shaderLocation = 2;
		defaultAttribs[2].format = wgpu::VertexFormat::Float32x3;
		defaultAttribs[2].offset = offsetof(Vertex, color);
		defaultAttribs[3].shaderLocation = 3;
		defaultAttribs[3].format = wgpu::VertexFormat::Float32x2;
		defaultAttribs[3].offset = offsetof(Vertex, uv);
		defaultAttribs[4].shaderLocation = 4;
		defaultAttribs[4].format = wgpu::VertexFormat::Float32x3;
		defaultAttribs[4].offset = offsetof(Vertex, tangent);
		defaultAttribs[5].shaderLocation = 5;
		defaultAttribs[5].format = wgpu::VertexFormat::Float32x3;
		defaultAttribs[5].offset = offsetof(Vertex, bitangent);

		wgpu::VertexBufferLayout defaultVertexBufferLayout = {};
		defaultVertexBufferLayout.arrayStride = sizeof(Vertex);
		defaultVertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
		defaultVertexBufferLayout.attributeCount = static_cast<uint32_t>(defaultAttribs.size());
		defaultVertexBufferLayout.attributes = defaultAttribs.data();

		wgpu::ColorTargetState defaultColorTarget = {};
		defaultColorTarget.format = colorFormat;
		defaultColorTarget.blend = nullptr;
		defaultColorTarget.writeMask = wgpu::ColorWriteMask::All;

		wgpu::DepthStencilState defaultDepthStencil = {};
		defaultDepthStencil.format = depthFormat;
		defaultDepthStencil.depthWriteEnabled = enableDepth;
		defaultDepthStencil.depthCompare = wgpu::CompareFunction::Less;
		defaultDepthStencil.stencilReadMask = 0;
		defaultDepthStencil.stencilWriteMask = 0;
		defaultDepthStencil.stencilFront.compare = wgpu::CompareFunction::Always;
		defaultDepthStencil.stencilFront.depthFailOp = wgpu::StencilOperation::Keep;
		defaultDepthStencil.stencilFront.failOp = wgpu::StencilOperation::Keep;
		defaultDepthStencil.stencilFront.passOp = wgpu::StencilOperation::Keep;
		defaultDepthStencil.stencilBack = defaultDepthStencil.stencilFront;
		// defaultDepthStencil.depthBias = 0;
		// defaultDepthStencil.depthBiasSlopeScale = 0.0f;
		// defaultDepthStencil.depthBiasClamp = 0.0f;

		wgpu::RenderPipelineDescriptor desc = {};
		if (vertexShader)
		{
			desc.vertex.module = vertexShader->module;
			desc.vertex.entryPoint = vertexShader->entryPoint.c_str();
		}
		desc.vertex.bufferCount = 1;
		desc.vertex.buffers = &defaultVertexBufferLayout;
		desc.vertex.constantCount = 0;
		desc.vertex.constants = nullptr;
		if (fragmentShader && fragmentShader->module)
		{
			static wgpu::FragmentState fragmentState = {};
			fragmentState.module = fragmentShader->module;
			fragmentState.entryPoint = fragmentShader->entryPoint.c_str();
			fragmentState.targetCount = 1;
			fragmentState.targets = &defaultColorTarget;
			fragmentState.constantCount = 0;
			fragmentState.constants = nullptr;
			desc.fragment = &fragmentState;
		}
		else
		{
			desc.fragment = nullptr;
		}
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
		desc.primitive.frontFace = wgpu::FrontFace::CCW;
		desc.primitive.cullMode = wgpu::CullMode::None;
		desc.depthStencil = (enableDepth && depthFormat != wgpu::TextureFormat::Undefined) ? &defaultDepthStencil : nullptr;
		desc.multisample.count = 1;
		desc.multisample.mask = ~0u;
		desc.multisample.alphaToCoverageEnabled = false;
		// layout must be set by the caller after descriptor creation
		return desc;
	}

	wgpu::RenderPipeline WebGPUPipelineFactory::getDefaultRenderPipeline()
	{
		if (!m_defaultPipelineInitialized)
		{
			// You must call this with valid shader modules and layout before use!
			// This is just a placeholder and will not work until properly set up.
			std::cerr << "WebGPUPipelineFactory::getDefaultRenderPipeline: Not implemented. Use createRenderPipeline instead." << std::endl;
			return nullptr;
		}
		return m_defaultPipeline;
	}

} // namespace engine::rendering::webgpu
