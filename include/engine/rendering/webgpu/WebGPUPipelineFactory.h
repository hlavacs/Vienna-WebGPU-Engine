#pragma once

#include "engine/rendering/Mesh.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include <memory>

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUPipelineFactory
{
  public:
	explicit WebGPUPipelineFactory(WebGPUContext &context);

	// Create pipeline descriptor with only minimum required, using defaults for the rest
	std::shared_ptr<WebGPUPipeline> createRenderPipeline(
		std::shared_ptr<WebGPUShaderInfo> shaderInfo,
		wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined,
		wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined,
		engine::rendering::Topology::Type topology = engine::rendering::Topology::Type::Triangles,
		wgpu::CullMode cullMode = wgpu::CullMode::Back,
		bool blendEnabled = false,
		uint32_t sampleCount = 1
	);

	// Helper to create a pipeline layout from bind group layouts
	wgpu::PipelineLayout createPipelineLayout(const wgpu::BindGroupLayout *layouts, uint32_t layoutCount);

	/// Build a compute pipeline from a pre-built pipeline layout + a shader
	/// module entry point. The single factory chokepoint for compute pipelines
	/// (the render path's getOrCreatePipeline is render-only), so consumers like
	/// ClusterManager don't call device.createComputePipeline directly.
	wgpu::ComputePipeline createComputePipeline(
		wgpu::PipelineLayout layout,
		wgpu::ShaderModule module,
		const char *entryPoint,
		const char *label = nullptr
	);

	/// Raw render-pipeline passthrough for one-off pipelines whose descriptor is
	/// hand-built (e.g. the IBL one-shot bake) and doesn't fit the shaderInfo +
	/// formats overload above. Keeps device.createRenderPipeline confined to the
	/// factory layer.
	wgpu::RenderPipeline createRenderPipeline(const wgpu::RenderPipelineDescriptor &desc);

	/// Shared empty `wgpu::BindGroupLayout`, lazily created on first request.
	/// Used to fill unused slots in a sparse pipeline layout (e.g. a shader
	/// that only declares @group(4) still needs slots 0..3 in the pipeline
	/// layout array; wgpu refuses null entries).
	wgpu::BindGroupLayout getOrCreateEmptyBindGroupLayout();

	/// Shared empty `wgpu::BindGroup` paired with the empty layout above.
	/// Render passes must bind something at every pipeline slot the shader's
	/// pipeline layout declares — utility shaders that only sample @group(4)
	/// still need an empty bind group at slots 0..3 to satisfy wgpu's
	/// "bind group at index N is unbound" validation.
	wgpu::BindGroup getOrCreateEmptyBindGroup();

	/**
	 * @brief Converts engine::rendering::Topology::Type to wgpu::PrimitiveTopology.
	 * @param topology The engine topology enum.
	 * @return Corresponding wgpu::PrimitiveTopology.
	 */
	[[nodiscard]] wgpu::PrimitiveTopology convertTopology(engine::rendering::Topology::Type topology) const
	{
		switch (topology)
		{
		case engine::rendering::Topology::Type::Points:
			return wgpu::PrimitiveTopology::PointList;
		case engine::rendering::Topology::Type::Lines:
			return wgpu::PrimitiveTopology::LineList;
		case engine::rendering::Topology::Type::LineStrip:
			return wgpu::PrimitiveTopology::LineStrip;
		case engine::rendering::Topology::Type::Triangles:
			return wgpu::PrimitiveTopology::TriangleList;
		case engine::rendering::Topology::Type::TriangleStrip:
			return wgpu::PrimitiveTopology::TriangleStrip;
		default:
			return wgpu::PrimitiveTopology::TriangleList;
		}
	}

	wgpu::VertexBufferLayout createVertexLayoutFromEnum(engine::rendering::VertexLayout layout, std::vector<wgpu::VertexAttribute> &attributes) const;

  private:
	WebGPUContext &m_context;
	wgpu::BlendState m_defaultBlendState;
	wgpu::BindGroupLayout m_emptyBindGroupLayout = nullptr;
	wgpu::BindGroup       m_emptyBindGroup       = nullptr;
};

} // namespace engine::rendering::webgpu
