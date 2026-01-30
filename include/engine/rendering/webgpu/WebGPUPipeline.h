#pragma once

#include <memory>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Vertex.h"

namespace engine::rendering::webgpu
{

class WebGPUShaderInfo;

/**
 * @class WebGPUPipeline
 * @brief GPU-side render pipeline: wraps a WebGPU render pipeline, layout, and descriptor.
 *
 * This class encapsulates a WebGPU render pipeline along with its layout and the descriptor
 * used to create it. It provides accessors for all relevant properties and ensures resource
 * cleanup via RAII. Used for all GPU-side render pipelines.
 *
 * Implements Identifiable so materials can reference pipelines via Handle<WebGPUPipeline>.
 */
class WebGPUPipeline
{
  public:
	/**
	 * @brief Constructs a WebGPUPipeline from a pipeline, layout, and descriptor.
	 *
	 * @param pipeline The GPU-side render pipeline.
	 * @param layout The pipeline layout (can be released after pipeline creation if desired).
	 * @param desc The render pipeline descriptor used to create the pipeline.
	 * @param vertexAttributes Vertex attributes used in the pipeline.
	 * @param vertexBufferLayout Vertex buffer layout used in the pipeline.
	 * @param colorTarget Color target state used in the pipeline.
	 * @param depthStencil Depth stencil state used in the pipeline.
	 * @param fragmentState Fragment state used in the pipeline.
	 * @param vertexLayout The vertex layout (baked-in from shader, stored for reference).
	 * @param shaderInfo The shader info associated with this pipeline.
	 *
	 * @throws Assertion failure if pipeline is invalid.
	 */
	WebGPUPipeline(
		wgpu::RenderPipeline pipeline,
		wgpu::PipelineLayout layout,
		wgpu::RenderPipelineDescriptor desc,
		std::vector<wgpu::VertexAttribute> vertexAttributes,
		wgpu::VertexBufferLayout vertexBufferLayout,
		wgpu::ColorTargetState colorTarget,
		wgpu::DepthStencilState depthStencil,
		wgpu::FragmentState fragmentState,
		engine::rendering::VertexLayout vertexLayout,
		std::shared_ptr<WebGPUShaderInfo> shaderInfo
	) : m_pipeline(pipeline),
		m_layout(layout),
		m_descriptor(desc),
		m_vertexAttributes(std::move(vertexAttributes)),
		m_vertexBufferLayout(std::move(vertexBufferLayout)),
		m_colorTarget(std::move(colorTarget)),
		m_depthStencil(std::move(depthStencil)),
		m_fragmentState(std::move(fragmentState)),
		m_vertexLayout(vertexLayout),
		m_shaderInfo(std::move(shaderInfo))
	{
		assert(m_pipeline && "Pipeline must be valid");
		assert(m_layout && "Pipeline layout must be valid");
	}

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 *
	 * Releases the pipeline and layout to prevent memory leaks.
	 */
	~WebGPUPipeline()
	{
		if (m_pipeline)
		{
			m_pipeline.release();
		}
		if (m_layout)
		{
			m_layout.release();
		}
		if (!m_vertexAttributes.empty())
		{
			m_vertexAttributes.clear();
		}
	}

	// Delete copy constructor and assignment
	WebGPUPipeline(const WebGPUPipeline &) = delete;
	WebGPUPipeline &operator=(const WebGPUPipeline &) = delete;

	// Allow move semantics
	WebGPUPipeline(WebGPUPipeline &&other) noexcept
		: m_pipeline(other.m_pipeline),
		  m_layout(other.m_layout),
		  m_descriptor(other.m_descriptor),
		  m_vertexAttributes(std::move(other.m_vertexAttributes)),
		  m_vertexBufferLayout(std::move(other.m_vertexBufferLayout)),
		  m_colorTarget(std::move(other.m_colorTarget)),
		  m_depthStencil(std::move(other.m_depthStencil)),
		  m_fragmentState(std::move(other.m_fragmentState)),
		  m_vertexLayout(other.m_vertexLayout)
	{
		other.m_pipeline = nullptr;
		other.m_layout = nullptr;
	}

	WebGPUPipeline &operator=(WebGPUPipeline &&other) noexcept
	{
		if (this != &other)
		{
			// Release current resources
			if (m_pipeline)
			{
				m_pipeline.release();
			}
			if (m_layout)
			{
				m_layout.release();
			}
			m_vertexAttributes.clear();

			// Move from other
			m_pipeline = other.m_pipeline;
			m_layout = other.m_layout;
			m_descriptor = other.m_descriptor;
			m_vertexAttributes = std::move(other.m_vertexAttributes);
			m_vertexBufferLayout = std::move(other.m_vertexBufferLayout);
			m_colorTarget = std::move(other.m_colorTarget);
			m_depthStencil = std::move(other.m_depthStencil);
			m_fragmentState = std::move(other.m_fragmentState);
			m_vertexLayout = other.m_vertexLayout;

			// Clear source
			other.m_pipeline = nullptr;
			other.m_layout = nullptr;
		}
		return *this;
	}

	/**
	 * @brief Gets the underlying WebGPU render pipeline.
	 * @return The render pipeline.
	 */
	[[nodiscard]] wgpu::RenderPipeline getPipeline() const { return m_pipeline; }

	/**
	 * @brief Gets the pipeline layout.
	 * @return The pipeline layout.
	 */
	[[nodiscard]] wgpu::PipelineLayout getLayout() const { return m_layout; }

	/**
	 * @brief Gets the render pipeline descriptor.
	 * @return The descriptor used to create this pipeline.
	 */
	[[nodiscard]] const wgpu::RenderPipelineDescriptor &getDescriptor() const { return m_descriptor; }

	/**
	 * @brief Checks if the pipeline is valid.
	 * @return True if pipeline is not null.
	 */
	[[nodiscard]] bool isValid() const { return m_pipeline != nullptr; }

	/**
	 * @brief Gets the vertex layout baked into this pipeline.
	 * @return The vertex layout enum.
	 */
	[[nodiscard]] engine::rendering::VertexLayout getVertexLayout() const { return m_vertexLayout; }

	/**
	 * @brief Gets the shader info associated with this pipeline.
	 * @return The shader info.
	 */
	[[nodiscard]] std::shared_ptr<WebGPUShaderInfo> getShaderInfo() const { return m_shaderInfo; }

	/**
	 * @brief Implicit conversion to wgpu::RenderPipeline for convenience.
	 */
	operator wgpu::RenderPipeline() const { return m_pipeline; }

  private:
	wgpu::RenderPipeline m_pipeline;
	wgpu::PipelineLayout m_layout;
	wgpu::RenderPipelineDescriptor m_descriptor;
	std::vector<wgpu::VertexAttribute> m_vertexAttributes;
	engine::rendering::VertexLayout m_vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor;
	std::shared_ptr<WebGPUShaderInfo> m_shaderInfo;

	wgpu::VertexBufferLayout m_vertexBufferLayout = {};
	wgpu::ColorTargetState m_colorTarget = {};
	wgpu::DepthStencilState m_depthStencil = {};
	wgpu::FragmentState m_fragmentState = {};
};

} // namespace engine::rendering::webgpu
