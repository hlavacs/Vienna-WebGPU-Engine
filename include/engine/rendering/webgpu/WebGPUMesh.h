#pragma once
#include <memory>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUSyncObject.h"

namespace engine::rendering::webgpu
{
/**
 * @brief Options for configuring a WebGPUMesh.
 */
struct WebGPUMeshOptions
{
};

/**
 * @class WebGPUMesh
 * @brief GPU-side mesh: wraps vertex and index buffers for a mesh.
 */
class WebGPUMesh : public WebGPUSyncObject<engine::rendering::Mesh>
{
  public:
	struct WebGPUSubmesh
	{
		uint32_t indexOffset;
		uint32_t indexCount;
		std::shared_ptr<WebGPUMaterial> material; // Can be null if no material
	};
	struct VertexBufferEntry
	{
		wgpu::Buffer buffer = nullptr;
		uint32_t count;
	};

	/**
	 * @brief Construct a WebGPUMesh from a Mesh handle and GPU buffers.
	 * @param context The WebGPU context.
	 * @param meshHandle Handle to the CPU-side Mesh.
	 * @param vertexBuffer The GPU-side vertex buffer.
	 * @param indexBuffer The GPU-side index buffer (may be null for non-indexed meshes).
	 * @param vertexCount Number of vertices.
	 * @param indexCount Number of indices (0 for non-indexed meshes).
	 * @param submeshes List of submeshes.
	 * @param options Optional mesh options.
	 */
	WebGPUMesh(
		WebGPUContext &context,
		const engine::rendering::Mesh::Handle &meshHandle,
		uint32_t vertexCount,
		uint32_t indexCount = 0,
		std::vector<WebGPUSubmesh> submeshes = {},
		WebGPUMeshOptions options = {}
	) :
		WebGPUSyncObject<engine::rendering::Mesh>(context, meshHandle),
		m_vertexCount(vertexCount),
		m_indexCount(indexCount),
		m_submeshes(std::move(submeshes)),
		m_options(std::move(options))
	{
	}

	~WebGPUMesh()
	{
		for (auto &[layout, entry] : m_vertexBuffers)
		{
			if (entry.buffer)
			{
				entry.buffer.destroy();
				entry.buffer.release();
			}
		}
		if (m_indexBuffer)
		{
			m_indexBuffer.destroy();
			m_indexBuffer.release();
		}
		if (!m_vertexBuffers.empty())
			m_vertexBuffers.clear();
		m_submeshes.clear();
	}

	/**
	 * @brief Set vertex and index buffers for rendering.
	 * @param renderPass The render pass encoder.
	 * @param layout The vertex layout to use for stride calculation.
	 */
	void bindBuffers(wgpu::RenderPassEncoder &renderPass, VertexLayout layout) const;

	/**
	 * @brief Ensure the mesh has a vertex buffer for the specified layout.
	 * @param layout The vertex layout.
	 * @param cpuMesh The CPU-side mesh to sync from if needed.
	 * @return The vertex buffer entry for the layout.
	 */
	const VertexBufferEntry &ensureBufferForLayout(VertexLayout layout) const;

	/**
	 * @brief Get the vertex count.
	 * @return The number of vertices.
	 */
	uint32_t getVertexCount() const { return m_vertexCount; }

	/**
	 * @brief Get the index count.
	 * @return The number of indices.
	 */
	uint32_t getIndexCount() const { return m_indexCount; }

	/**
	 * @brief Check if the mesh is indexed.
	 * @return True if the mesh has an index buffer.
	 */
	bool isIndexed() const { return m_indexCount > 0; }

	/**
	 * @brief Get the submeshes.
	 * @return The list of submeshes.
	 */
	const std::vector<WebGPUSubmesh> &getSubmeshes() const { return m_submeshes; }

	/**
	 * @brief Set the submeshes.
	 * @param submeshes The list of submeshes.
	 */
	void setSubmeshes(std::vector<WebGPUSubmesh> submeshes)
	{
		m_submeshes = std::move(submeshes);
	}

	/**
	 * @brief Get the mesh options.
	 * @return The mesh options.
	 */
	const WebGPUMeshOptions &getOptions() const { return m_options; }

  protected:
	/**
	 * @brief Sync GPU resources from CPU mesh.
	 * For immutable meshes, this typically does nothing.
	 */
	void syncFromCPU(const Mesh &cpuMesh) override;

  private:
	mutable std::unordered_map<VertexLayout, VertexBufferEntry> m_vertexBuffers;
	uint32_t m_indexCount;
	wgpu::Buffer m_indexBuffer = nullptr;
	uint32_t m_vertexCount;
	std::vector<WebGPUSubmesh> m_submeshes;
	WebGPUMeshOptions m_options;
};

} // namespace engine::rendering::webgpu
