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
	std::shared_ptr<WebGPUShaderInfo> shaderInfo,
	wgpu::TextureFormat colorFormat,
	wgpu::TextureFormat depthFormat,
	engine::rendering::Topology::Type topology,
	wgpu::CullMode cullMode,
	uint32_t sampleCount
)
{
	if (!shaderInfo)
	{
		spdlog::error("Shader info is required to create a render pipeline");
		return nullptr;
	}

	bool hasFragment = shaderInfo->hasFragmentStage();
	bool hasColor = colorFormat != wgpu::TextureFormat::Undefined;
	bool hasDepth = shaderInfo->isDepthEnabled() && depthFormat != wgpu::TextureFormat::Undefined;

	if (hasColor && !hasFragment)
		spdlog::warn("Creating render pipeline with color attachment but no fragment shader");

	// Vertex layout
	auto vertexLayout = shaderInfo->getVertexLayout();
	std::vector<wgpu::VertexAttribute> vertexAttributes;
	auto vertexBufferLayout = createVertexLayoutFromEnum(vertexLayout, vertexAttributes);

	// Fragment state
	wgpu::ColorTargetState colorTarget{};
	wgpu::FragmentState fragmentState{};
	if (hasFragment)
	{
		if (hasColor)
		{
			colorTarget.format = colorFormat;
			colorTarget.blend = shaderInfo->isBlendEnabled() ? &m_defaultBlendState : nullptr;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;
			fragmentState.targets = &colorTarget;
			fragmentState.targetCount = 1;
		}
		fragmentState.module = shaderInfo->getModule();
		fragmentState.entryPoint = shaderInfo->getFragmentEntryPoint().c_str();
	}

	// Pipeline descriptor
	wgpu::RenderPipelineDescriptor desc{};

	// Vertex stage
	desc.vertex.module = shaderInfo->getModule();
	desc.vertex.entryPoint = shaderInfo->getVertexEntryPoint().c_str();
	if (vertexLayout != engine::rendering::VertexLayout::None)
	{
		desc.vertex.bufferCount = 1;
		desc.vertex.buffers = &vertexBufferLayout;
	}

	// Primitive
	desc.primitive.topology = convertTopology(topology);
	desc.primitive.stripIndexFormat = (desc.primitive.topology == wgpu::PrimitiveTopology::TriangleStrip
									   || desc.primitive.topology == wgpu::PrimitiveTopology::LineStrip)
										  ? wgpu::IndexFormat::Uint32
										  : wgpu::IndexFormat::Undefined;
	desc.primitive.frontFace = wgpu::FrontFace::CCW;
	desc.primitive.cullMode = cullMode;

	// Multisample
	desc.multisample.count = sampleCount;
	desc.multisample.mask = ~0u;

	// Fragment stage (optional)
	if (hasFragment)
		desc.fragment = &fragmentState;

	// Depth-stencil (optional)
	wgpu::DepthStencilState depthStencil{};
	if (hasDepth)
	{
		depthStencil.format = depthFormat;
		depthStencil.depthWriteEnabled = true;
		depthStencil.depthCompare = wgpu::CompareFunction::Less;
		depthStencil.stencilFront = {wgpu::CompareFunction::Always, wgpu::StencilOperation::Keep, wgpu::StencilOperation::Keep, wgpu::StencilOperation::Keep};
		depthStencil.stencilBack = depthStencil.stencilFront;
		desc.depthStencil = &depthStencil;
	}

	// Bind group layouts
	auto layouts = shaderInfo->getBindGroupLayoutVector();
	std::vector<wgpu::BindGroupLayout> layoutArray;
	layoutArray.reserve(layouts.size());
	for (const auto &layoutInfo : layouts)
		layoutArray.push_back(layoutInfo->getLayout());

	wgpu::PipelineLayout layout = createPipelineLayout(layoutArray.data(), layoutArray.size());
	desc.layout = layout;

	// Create pipeline
	wgpu::RenderPipeline pipeline = m_context.getDevice().createRenderPipeline(desc);
	if (!pipeline)
	{
		spdlog::error("Failed to create render pipeline");
		layout.release();
		return nullptr;
	}

	return std::make_shared<WebGPUPipeline>(
		pipeline,
		layout,
		std::move(desc),
		std::move(vertexAttributes),
		std::move(vertexBufferLayout),
		hasColor ? std::move(colorTarget) : wgpu::ColorTargetState{},
		hasDepth ? std::move(depthStencil) : wgpu::DepthStencilState{},
		hasFragment ? std::move(fragmentState) : wgpu::FragmentState{},
		vertexLayout,
		std::move(shaderInfo)
	);
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
	// Handle None layout (procedural vertex generation)
	if (layout == engine::rendering::VertexLayout::None)
	{
		wgpu::VertexBufferLayout emptyLayout{};
		emptyLayout.stepMode = wgpu::VertexStepMode::Vertex;
		emptyLayout.arrayStride = 0;
		emptyLayout.attributeCount = 0;
		emptyLayout.attributes = nullptr;
		return emptyLayout;
	}

	engine::rendering::VertexAttribute attribs = engine::rendering::Vertex::requiredAttributes(layout);
	size_t arrayStride = 0;
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
