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
		wgpu::Buffer vertexBuffer,
		wgpu::Buffer indexBuffer,
		uint32_t vertexCount,
		uint32_t indexCount = 0,
		std::vector<WebGPUSubmesh> submeshes = {},
		WebGPUMeshOptions options = {}
	) :
		WebGPUSyncObject<engine::rendering::Mesh>(context, meshHandle),
		m_vertexBuffer(vertexBuffer),
		m_indexBuffer(indexBuffer),
		m_vertexCount(vertexCount),
		m_indexCount(indexCount),
		m_submeshes(std::move(submeshes)),
		m_options(std::move(options))
	{
	}

	/**
	 * @brief Set vertex and index buffers for rendering.
	 * @param renderPass The render pass encoder.
	 * @param layout The vertex layout to use for stride calculation.
	 */
	void bindBuffers(wgpu::RenderPassEncoder &renderPass, VertexLayout layout) const;

	/**
	 * @brief Get the vertex buffer.
	 * @return The vertex buffer.
	 */
	wgpu::Buffer getVertexBuffer() const { return m_vertexBuffer; }

	/**
	 * @brief Get the index buffer.
	 * @return The index buffer.
	 */
	wgpu::Buffer getIndexBuffer() const { return m_indexBuffer; }

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
		wgpu::Buffer m_vertexBuffer;
		wgpu::Buffer m_indexBuffer;
		uint32_t m_vertexCount;
		uint32_t m_indexCount;
		std::vector<WebGPUSubmesh> m_submeshes;
		WebGPUMeshOptions m_options;
};

} // namespace engine::rendering::webgpu
