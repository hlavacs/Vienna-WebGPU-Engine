#pragma once
#include <memory>
#include <webgpu/webgpu.hpp>
#include "engine/core/Identifiable.h"

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
class WebGPUPipeline : public engine::core::Identifiable<WebGPUPipeline>
{
  public:
	using Handle = engine::core::Handle<WebGPUPipeline>;
	
	/**
	 * @brief Constructs a WebGPUPipeline from a pipeline, layout, and descriptor.
	 *
	 * @param pipeline The GPU-side render pipeline.
	 * @param layout The pipeline layout (can be released after pipeline creation if desired).
	 * @param desc The render pipeline descriptor used to create the pipeline.
	 * @param shaderInfo Optional shader info for bind group layout access.
	 *
	 * @throws Assertion failure if pipeline is invalid.
	 */
	WebGPUPipeline(
		wgpu::RenderPipeline pipeline,
		wgpu::PipelineLayout layout,
		const wgpu::RenderPipelineDescriptor &desc,
		std::shared_ptr<WebGPUShaderInfo> shaderInfo = nullptr
	) : m_pipeline(pipeline),
		m_layout(layout),
		m_descriptor(desc),
		m_shaderInfo(shaderInfo)
	{
		assert(m_pipeline && "Pipeline must be valid");
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
	}

	// Delete copy constructor and assignment
	WebGPUPipeline(const WebGPUPipeline &) = delete;
	WebGPUPipeline &operator=(const WebGPUPipeline &) = delete;

	// Allow move semantics
	WebGPUPipeline(WebGPUPipeline &&other) noexcept
		: m_pipeline(other.m_pipeline),
		  m_layout(other.m_layout),
		  m_descriptor(other.m_descriptor)
	{
		other.m_pipeline = nullptr;
		other.m_layout = nullptr;
	}

	WebGPUPipeline &operator=(WebGPUPipeline &&other) noexcept
	{
		if (this != &other)
		{
			if (m_pipeline)
				m_pipeline.release();
			if (m_layout)
				m_layout.release();

			m_pipeline = other.m_pipeline;
			m_layout = other.m_layout;
			m_descriptor = other.m_descriptor;

			other.m_pipeline = nullptr;
			other.m_layout = nullptr;
		}
		return *this;
	}

	/**
	 * @brief Gets the underlying WebGPU render pipeline.
	 * @return The render pipeline.
	 */
	wgpu::RenderPipeline getPipeline() const { return m_pipeline; }

	/**
	 * @brief Gets the pipeline layout.
	 * @return The pipeline layout.
	 */
	wgpu::PipelineLayout getLayout() const { return m_layout; }

	/**
	 * @brief Gets the render pipeline descriptor.
	 * @return The descriptor used to create this pipeline.
	 */
	const wgpu::RenderPipelineDescriptor &getDescriptor() const { return m_descriptor; }

	/**
	 * @brief Checks if the pipeline is valid.
	 * @return True if pipeline is not null.
	 */
	bool isValid() const { return m_pipeline != nullptr; }

	/**
	 * @brief Gets the shader info associated with this pipeline.
	 * @return Shared pointer to WebGPUShaderInfo, or nullptr if not set.
	 */
	std::shared_ptr<WebGPUShaderInfo> getShaderInfo() const { return m_shaderInfo; }

	/**
	 * @brief Implicit conversion to wgpu::RenderPipeline for convenience.
	 */
	operator wgpu::RenderPipeline() const { return m_pipeline; }

  private:
	wgpu::RenderPipeline m_pipeline;
	wgpu::PipelineLayout m_layout;
	wgpu::RenderPipelineDescriptor m_descriptor;
	std::shared_ptr<WebGPUShaderInfo> m_shaderInfo;
};

} // namespace engine::rendering::webgpu
