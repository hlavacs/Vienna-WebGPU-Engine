#pragma once
#include <memory>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"

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
class WebGPUMesh : public WebGPURenderObject<engine::rendering::Mesh>
{
  public:
	/**
	 * @brief Construct a WebGPUMesh from a Mesh handle and GPU buffers.
	 * @param context The WebGPU context.
	 * @param meshHandle Handle to the CPU-side Mesh.
	 * @param vertexBuffer The GPU-side vertex buffer.
	 * @param indexBuffer The GPU-side index buffer (may be null for non-indexed meshes).
	 * @param vertexCount Number of vertices.
	 * @param indexCount Number of indices (0 for non-indexed meshes).
	 * @param options Optional mesh options.
	 */
	WebGPUMesh(
		WebGPUContext &context,
		const engine::rendering::Mesh::Handle &meshHandle,
		wgpu::Buffer vertexBuffer,
		wgpu::Buffer indexBuffer,
		uint32_t vertexCount,
		uint32_t indexCount = 0,
		WebGPUMeshOptions options = {}
	);

	/**
	 * @brief Render the mesh.
	 * @param encoder The command encoder.
	 * @param renderPass The render pass.
	 */
	void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass) override;

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
	 * @brief Get the mesh options.
	 * @return The mesh options.
	 */
	const WebGPUMeshOptions &getOptions() const { return m_options; }

  protected:
	/**
	 * @brief Update GPU resources when CPU mesh changes.
	 */
	void updateGPUResources() override;

  private:
	wgpu::Buffer m_vertexBuffer;
	wgpu::Buffer m_indexBuffer;
	uint32_t m_vertexCount;
	uint32_t m_indexCount;
	WebGPUMeshOptions m_options;
};

} // namespace engine::rendering::webgpu
