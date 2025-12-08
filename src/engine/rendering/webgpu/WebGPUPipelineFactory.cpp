#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"

#include <array>
#include <iostream>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUPipelineFactory::WebGPUPipelineFactory(WebGPUContext &context) :
	m_context(context), m_defaultPipelineInitialized(false)
{
}

std::shared_ptr<WebGPUPipeline> WebGPUPipelineFactory::createPipeline(
	const wgpu::RenderPipelineDescriptor &descriptor,
	const wgpu::BindGroupLayout *layouts,
	uint32_t layoutCount,
	std::shared_ptr<WebGPUShaderInfo> shaderInfo
)
{
	// Create the pipeline layout
	wgpu::PipelineLayout layout = createPipelineLayout(layouts, layoutCount);
	if (!layout)
	{
		std::cerr << "Failed to create pipeline layout" << std::endl;
		return nullptr;
	}

	// Create a copy of the descriptor and set the layout
	wgpu::RenderPipelineDescriptor desc = descriptor;
	desc.layout = layout;

	// Create the pipeline
	wgpu::RenderPipeline pipeline = m_context.getDevice().createRenderPipeline(desc);
	if (!pipeline)
	{
		std::cerr << "Failed to create render pipeline" << std::endl;
		layout.release();
		return nullptr;
	}

	// Wrap in WebGPUPipeline with shader info (layout ownership transferred)
	return std::make_shared<WebGPUPipeline>(pipeline, layout, desc, shaderInfo);
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
	const WebGPUShaderInfo *fragmentShader
)
{
	wgpu::RenderPipelineDescriptor desc = {};
	if (vertexShader)
	{
		desc.vertex.module = vertexShader->getModule();
		desc.vertex.entryPoint = vertexShader->getVertexEntryPoint().c_str();
	}
	desc.vertex.bufferCount = vertexBufferCount;
	desc.vertex.buffers = vertexBuffers;
	desc.vertex.constantCount = 0;
	desc.vertex.constants = nullptr;
	if (fragmentShader && fragmentShader->getModule())
	{
		static wgpu::FragmentState fragmentState = {};
		fragmentState.module = fragmentShader->getModule();
		fragmentState.entryPoint = fragmentShader->getFragmentEntryPoint().c_str();
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
	bool enableDepth
)
{
	static wgpu::VertexBufferLayout defaultVertexBufferLayout = {};
	static std::vector<wgpu::VertexAttribute> defaultAttribs(6);
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

	defaultVertexBufferLayout.arrayStride = sizeof(Vertex);
	defaultVertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
	defaultVertexBufferLayout.attributeCount = static_cast<uint32_t>(defaultAttribs.size());
	defaultVertexBufferLayout.attributes = defaultAttribs.data();

	static wgpu::ColorTargetState defaultColorTarget = {};
	defaultColorTarget.format = colorFormat;
	defaultColorTarget.blend = nullptr;
	defaultColorTarget.writeMask = wgpu::ColorWriteMask::All;

	static wgpu::DepthStencilState defaultDepthStencil = {};
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
		desc.vertex.module = vertexShader->getModule();
		desc.vertex.entryPoint = vertexShader->getVertexEntryPoint().c_str();
	}
	desc.vertex.bufferCount = 1;
	desc.vertex.buffers = &defaultVertexBufferLayout;
	desc.vertex.constantCount = 0;
	desc.vertex.constants = nullptr;
	if (fragmentShader && fragmentShader->getModule())
	{
		static wgpu::FragmentState fragmentState = {};
		fragmentState.module = fragmentShader->getModule();
		fragmentState.entryPoint = fragmentShader->getFragmentEntryPoint().c_str();
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

std::shared_ptr<WebGPUPipeline> WebGPUPipelineFactory::getDefaultRenderPipeline()
{
	if (!m_defaultPipelineInitialized)
	{
		// You must call this with valid shader modules and layout before use!
		// This is just a placeholder and will not work until properly set up.
		std::cerr << "WebGPUPipelineFactory::getDefaultRenderPipeline: Not implemented. Use createPipeline instead." << std::endl;
		return nullptr;
	}
	return m_defaultPipeline;
}

wgpu::PipelineLayout WebGPUPipelineFactory::createPipelineLayout(const wgpu::BindGroupLayout *layouts, uint32_t layoutCount)
{
	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = layoutCount;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)layouts;
	return m_context.getDevice().createPipelineLayout(layoutDesc);
}

} // namespace engine::rendering::webgpu
