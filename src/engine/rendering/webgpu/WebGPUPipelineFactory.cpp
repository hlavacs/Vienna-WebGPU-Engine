#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"

#include <array>
#include <iostream>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

#ifdef None
#undef None
#endif
#ifdef Always
#undef Always
#endif

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
	bool blendEnabled,
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

	// Fragment state.
	//
	// Two sources of truth for color attachment formats:
	//   1. shaderInfo->getColorTargetFormats() - declared via
	//      WebGPUShaderBuilder::addColorTarget() for shaders that write to
	//      multiple targets or need a fixed format (e.g. G-buffer geometry).
	//      Wins when non-empty.
	//   2. colorFormat parameter - the single-target fallback used by every
	//      plain forward / post-process pipeline.
	wgpu::ColorTargetState colorTarget{};
	wgpu::FragmentState fragmentState{};
	std::vector<wgpu::ColorTargetState> multiTargets;
	if (hasFragment)
	{
		const auto &declaredFormats = shaderInfo->getColorTargetFormats();
		if (!declaredFormats.empty())
		{
			multiTargets.resize(declaredFormats.size());
			for (size_t i = 0; i < declaredFormats.size(); ++i)
			{
				multiTargets[i].format = declaredFormats[i];
				multiTargets[i].blend = blendEnabled ? &m_defaultBlendState : nullptr;
				multiTargets[i].writeMask = wgpu::ColorWriteMask::All;
			}
			fragmentState.targets = multiTargets.data();
			fragmentState.targetCount = static_cast<uint32_t>(multiTargets.size());
		}
		else if (hasColor)
		{
			colorTarget.format = colorFormat;
			colorTarget.blend = blendEnabled ? &m_defaultBlendState : nullptr;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;
			fragmentState.targets = &colorTarget;
			fragmentState.targetCount = 1;
		}
		fragmentState.module = shaderInfo->getModule();
		fragmentState.entryPoint = shaderInfo->getFragmentEntryPoint().c_str();
	}

	// Pipeline descriptor
	wgpu::RenderPipelineDescriptor desc{};
	desc.label = shaderInfo->getName().c_str();

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

	// Depth-stencil (optional). depthCompare / depthWriteEnabled come from the
	// shader info so passes that need LessEqual + read-only depth (skybox after
	// the geometry pass is the canonical example) can opt in without touching
	// the factory's signature. Additionally: any pipeline with alpha blending
	// enabled must NOT write depth - otherwise translucent surfaces occlude
	// other translucent surfaces drawn later and the alpha math breaks.
	wgpu::DepthStencilState depthStencil{};
	if (hasDepth)
	{
		depthStencil.format = depthFormat;
		depthStencil.depthWriteEnabled = blendEnabled ? false : shaderInfo->isDepthWriteEnabled();
		depthStencil.depthCompare = shaderInfo->getDepthCompare();
		depthStencil.stencilFront = {wgpu::CompareFunction::Always, wgpu::StencilOperation::Keep, wgpu::StencilOperation::Keep, wgpu::StencilOperation::Keep};
		depthStencil.stencilBack = depthStencil.stencilFront;
		desc.depthStencil = &depthStencil;
	}

	// Bind group layouts. Slots between the highest declared group and a custom
	// @group(20)+ are filled with a shared empty layout — wgpu requires a real
	// (non-null) BindGroupLayout at every index up to the highest one the shader
	// uses, even if the shader doesn't sample that slot.
	auto layouts = shaderInfo->getBindGroupLayoutVector();
	std::vector<wgpu::BindGroupLayout> layoutArray;
	layoutArray.reserve(layouts.size());
	for (const auto &layoutInfo : layouts)
	{
		if (layoutInfo)
			layoutArray.push_back(layoutInfo->getLayout());
		else
			layoutArray.push_back(getOrCreateEmptyBindGroupLayout());
	}

	wgpu::PipelineLayout layout = createPipelineLayout(layoutArray.data(), static_cast<uint32_t>(layoutArray.size()));
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

wgpu::ComputePipeline WebGPUPipelineFactory::createComputePipeline(
	wgpu::PipelineLayout layout,
	wgpu::ShaderModule module,
	const char *entryPoint,
	const char *label
)
{
	wgpu::ComputePipelineDescriptor desc{};
	desc.label                 = label;
	desc.layout                = layout;
	desc.compute.module        = module;
	desc.compute.entryPoint    = entryPoint;
	desc.compute.constantCount = 0;
	desc.compute.constants     = nullptr;
	return m_context.getDevice().createComputePipeline(desc);
}

wgpu::RenderPipeline WebGPUPipelineFactory::createRenderPipeline(const wgpu::RenderPipelineDescriptor &desc)
{
	return m_context.getDevice().createRenderPipeline(desc);
}

wgpu::BindGroupLayout WebGPUPipelineFactory::getOrCreateEmptyBindGroupLayout()
{
	// Single device-wide empty layout reused as a placeholder for every
	// unused bind-group slot in every pipeline. wgpu refuses null entries in
	// the pipeline layout array, so even a shader that only uses @group(4)
	// needs slots 0..3 to be real BindGroupLayouts. Sharing one instance
	// keeps the GPU memory footprint at one descriptor.
	if (!m_emptyBindGroupLayout)
	{
		wgpu::BindGroupLayoutDescriptor desc{};
		desc.entryCount = 0;
		desc.entries    = nullptr;
		m_emptyBindGroupLayout = m_context.getDevice().createBindGroupLayout(desc);
	}
	return m_emptyBindGroupLayout;
}

wgpu::BindGroup WebGPUPipelineFactory::getOrCreateEmptyBindGroup()
{
	if (!m_emptyBindGroup)
	{
		auto layout = getOrCreateEmptyBindGroupLayout();
		wgpu::BindGroupDescriptor desc{};
		desc.layout     = layout;
		desc.entryCount = 0;
		desc.entries    = nullptr;
		m_emptyBindGroup = m_context.getDevice().createBindGroup(desc);
	}
	return m_emptyBindGroup;
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

	// Attribute offsets must match the *packed* layout produced by
	// Vertex::repackVertices() - not offsetof() into the (padded) C++ struct.
	// repackVertices writes attributes back-to-back in this fixed order:
	//   Position -> Normal -> UV -> Tangent -> Color
	// We mirror that order here and accumulate the offset as we go.
	engine::rendering::VertexAttribute attribs = engine::rendering::Vertex::requiredAttributes(layout);
	const size_t arrayStride = engine::rendering::Vertex::getStride(layout);

	size_t cursor = 0;
	auto pushAttr = [&](wgpu::VertexFormat fmt, size_t fieldBytes)
	{
		wgpu::VertexAttribute a{};
		a.format = fmt;
		a.offset = cursor;
		attributes.push_back(a);
		cursor += fieldBytes;
	};

	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Position))
		pushAttr(wgpu::VertexFormat::Float32x3, sizeof(engine::rendering::Vertex::position));
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Normal))
		pushAttr(wgpu::VertexFormat::Float32x3, sizeof(engine::rendering::Vertex::normal));
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::UV))
		pushAttr(wgpu::VertexFormat::Float32x2, sizeof(engine::rendering::Vertex::uv));
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Tangent))
		pushAttr(wgpu::VertexFormat::Float32x4, sizeof(engine::rendering::Vertex::tangent));
	if (engine::rendering::Vertex::has(attribs, engine::rendering::VertexAttribute::Color))
		pushAttr(wgpu::VertexFormat::Float32x3, sizeof(engine::rendering::Vertex::color));

	// Sanity: the accumulated offsets must add up to the same stride that
	// repackVertices/getStride agreed on, otherwise the GPU would read past
	// the end of each vertex.
	assert(cursor == arrayStride && "Packed vertex offsets disagree with Vertex::getStride()");

	for (auto i = 0; i < attributes.size(); ++i)
		attributes[i].shaderLocation = static_cast<uint32_t>(i);

	wgpu::VertexBufferLayout vertexLayout{};
	vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
	vertexLayout.arrayStride = arrayStride;
	vertexLayout.attributeCount = attributes.size();
	vertexLayout.attributes = attributes.data();
	return vertexLayout;
}
} // namespace engine::rendering::webgpu
