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
		std::shared_ptr<WebGPUShaderInfo> vertexShader,
		std::shared_ptr<WebGPUShaderInfo> fragmentShader = nullptr,
		wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined,
		wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined,
		engine::rendering::Topology::Type topology = engine::rendering::Topology::Type::Triangles,
		wgpu::CullMode cullMode = wgpu::CullMode::Back,
		uint32_t sampleCount = 1
	);

	// Helper to create a pipeline layout from bind group layouts
	wgpu::PipelineLayout createPipelineLayout(const wgpu::BindGroupLayout *layouts, uint32_t layoutCount);

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
		return wgpu::PrimitiveTopology::TriangleList;
	}

	wgpu::VertexBufferLayout createVertexLayoutFromEnum(engine::rendering::VertexLayout layout, std::vector<wgpu::VertexAttribute> &attributes) const;

  private:
	WebGPUContext &m_context;
	wgpu::BlendState m_defaultBlendState;
};

} // namespace engine::rendering::webgpu
