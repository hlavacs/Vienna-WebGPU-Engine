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
	m_context(context)
{

	m_defaultBlendState = wgpu::BlendState{};
	m_defaultBlendState.alpha = wgpu::BlendComponent{};
	m_defaultBlendState.color = wgpu::BlendComponent{};
	m_defaultBlendState.alpha.srcFactor = wgpu::BlendFactor::One;
	m_defaultBlendState.alpha.dstFactor = wgpu::BlendFactor::Zero;
	m_defaultBlendState.alpha.operation = wgpu::BlendOperation::Add;
	m_defaultBlendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	m_defaultBlendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	m_defaultBlendState.color.operation = wgpu::BlendOperation::Add;
}

std::shared_ptr<WebGPUPipeline> WebGPUPipelineFactory::createRenderPipeline(
	const WebGPUShaderInfo *vertexShader,
	const WebGPUShaderInfo *fragmentShader,
	wgpu::TextureFormat colorFormat,
	wgpu::TextureFormat depthFormat,
	engine::rendering::Topology::Type topology,
	wgpu::CullMode cullMode,
	uint32_t sampleCount
)
{
	if (vertexShader == nullptr)
	{
		throw std::runtime_error("Vertex shader must be provided to create render pipeline descriptor");
	}
	bool enableDepth = vertexShader->isDepthEnabled()
					   && (fragmentShader && fragmentShader->isDepthEnabled())
					   && depthFormat != wgpu::TextureFormat::Undefined;
	wgpu::PrimitiveTopology primitiveTopology = convertTopology(topology);

	auto vertexLayout = vertexShader->getVertexLayout();

	std::vector<wgpu::VertexAttribute> vertexAttributes;
	auto vertexBufferLayout = createVertexLayoutFromEnum(vertexLayout, vertexAttributes);

	wgpu::ColorTargetState colorTarget{};
	colorTarget.format = colorFormat;
	colorTarget.blend = fragmentShader->isBlendEnabled() ? &m_defaultBlendState : nullptr;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;

	wgpu::RenderPipelineDescriptor desc = {};
	desc.vertex.module = vertexShader->getModule();
	desc.vertex.entryPoint = vertexShader->getVertexEntryPoint().c_str();
	desc.vertex.bufferCount = 1;
	desc.vertex.buffers = &vertexBufferLayout;
	desc.vertex.constantCount = 0;
	desc.vertex.constants = nullptr;
	desc.primitive.topology = primitiveTopology;
	desc.primitive.stripIndexFormat =
		primitiveTopology == wgpu::PrimitiveTopology::TriangleStrip
				|| primitiveTopology == wgpu::PrimitiveTopology::LineStrip
			? wgpu::IndexFormat::Uint32
			: wgpu::IndexFormat::Undefined;
	desc.primitive.frontFace = wgpu::FrontFace::CCW;
	desc.primitive.cullMode = cullMode;
	desc.multisample.count = sampleCount;
	desc.multisample.mask = ~0u;
	desc.multisample.alphaToCoverageEnabled = false;

	wgpu::FragmentState fragmentState = {};
	if (fragmentShader && fragmentShader->getModule())
	{
		fragmentState.module = fragmentShader->getModule();
		fragmentState.entryPoint = fragmentShader->getFragmentEntryPoint().c_str();
		fragmentState.targetCount = 1;
		fragmentState.targets = &colorTarget;
		fragmentState.constantCount = 0;
		fragmentState.constants = nullptr;
		desc.fragment = &fragmentState;
	} else {
		desc.fragment = nullptr;
	}

	wgpu::DepthStencilState depthStencil = {};
	if (enableDepth)
	{
		depthStencil;
		depthStencil.format = depthFormat;
		depthStencil.depthWriteEnabled = true;
		depthStencil.depthCompare = wgpu::CompareFunction::Less;
		depthStencil.stencilReadMask = 0;
		depthStencil.stencilWriteMask = 0;

		depthStencil.stencilFront.compare = wgpu::CompareFunction::Always;
		depthStencil.stencilFront.failOp = wgpu::StencilOperation::Keep;
		depthStencil.stencilFront.depthFailOp = wgpu::StencilOperation::Keep;
		depthStencil.stencilFront.passOp = wgpu::StencilOperation::Keep;
		depthStencil.stencilBack = depthStencil.stencilFront;
		desc.depthStencil = &depthStencil;
	} else {
		desc.depthStencil = nullptr;
	}

	auto layouts = vertexShader->getBindGroupLayoutVector();
	if (fragmentShader)
	{
		auto fragLayouts = fragmentShader->getBindGroupLayoutVector();
		layouts.insert(layouts.end(), fragLayouts.begin(), fragLayouts.end());
	}
	std::vector<wgpu::BindGroupLayout> layoutArray;
	for (const auto &layoutInfo : layouts)
	{
		layoutArray.push_back(layoutInfo->getLayout());
	}
	wgpu::PipelineLayout layout = createPipelineLayout(layoutArray.data(), layoutArray.size());
	desc.layout = layout;

	wgpu::RenderPipeline pipeline = m_context.getDevice().createRenderPipeline(desc);
	if (!pipeline)
	{
		std::cerr << "Failed to create render pipeline" << std::endl;
		layout.release();
		return nullptr;
	}

	// Wrap in WebGPUPipeline with shader info (layout ownership transferred)
	auto pipelinePtr = std::make_shared<WebGPUPipeline>(
		pipeline,
		layout,
		std::move(desc),
		std::move(vertexAttributes),
		std::move(vertexBufferLayout),
		std::move(colorTarget),
		std::move(depthStencil),
		std::move(fragmentState),
		vertexShader,
		fragmentShader
	);

	return pipelinePtr;
}

wgpu::PipelineLayout WebGPUPipelineFactory::createPipelineLayout(const wgpu::BindGroupLayout *layouts, uint32_t layoutCount)
{
	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = layoutCount;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)layouts;
	return m_context.getDevice().createPipelineLayout(layoutDesc);
}

wgpu::VertexBufferLayout WebGPUPipelineFactory::createVertexLayoutFromEnum(engine::rendering::VertexLayout layout, std::vector<wgpu::VertexAttribute> &attributes) const
{
	engine::rendering::VertexAttribute attribs = engine::rendering::Vertex::requiredAttributes(layout);
	size_t arrayStride;
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Position))
	{
		wgpu::VertexAttribute positionAttr{};
		positionAttr.format = wgpu::VertexFormat::Float32x3;
		positionAttr.offset = offsetof(engine::rendering::Vertex, position);
		attributes.push_back(positionAttr);
		arrayStride += sizeof(engine::rendering::Vertex::position);
	}
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Normal))
	{
		wgpu::VertexAttribute normalAttr{};
		normalAttr.format = wgpu::VertexFormat::Float32x3;
		normalAttr.offset = offsetof(engine::rendering::Vertex, normal);
		attributes.push_back(normalAttr);
		arrayStride += sizeof(engine::rendering::Vertex::normal);
	}
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Color))
	{
		wgpu::VertexAttribute colorAttr{};
		colorAttr.format = wgpu::VertexFormat::Float32x4;
		colorAttr.offset = offsetof(engine::rendering::Vertex, color);
		attributes.push_back(colorAttr);
		arrayStride += sizeof(engine::rendering::Vertex::color);
	}
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::UV))
	{
		wgpu::VertexAttribute uvAttr{};
		uvAttr.format = wgpu::VertexFormat::Float32x2;
		uvAttr.offset = offsetof(engine::rendering::Vertex, uv);
		attributes.push_back(uvAttr);
		arrayStride += sizeof(engine::rendering::Vertex::uv);
	}
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Tangent))
	{
		wgpu::VertexAttribute tangentAttr{};
		tangentAttr.format = wgpu::VertexFormat::Float32x3;
		tangentAttr.offset = offsetof(engine::rendering::Vertex, tangent);
		attributes.push_back(tangentAttr);
		arrayStride += sizeof(engine::rendering::Vertex::tangent);
	}
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Bitangent))
	{
		wgpu::VertexAttribute bitangentAttr{};
		bitangentAttr.format = wgpu::VertexFormat::Float32x3;
		bitangentAttr.offset = offsetof(engine::rendering::Vertex, bitangent);
		attributes.push_back(bitangentAttr);
		arrayStride += sizeof(engine::rendering::Vertex::bitangent);
	}
	for (auto i = 0; i < attributes.size(); ++i)
	{
		attributes[i].shaderLocation = static_cast<uint32_t>(i);
	}

	wgpu::VertexBufferLayout vertexLayout{};
	vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
	vertexLayout.arrayStride = arrayStride;
	vertexLayout.attributeCount = attributes.size();
	vertexLayout.attributes = attributes.data();
	return vertexLayout;
}
} // namespace engine::rendering::webgpu
