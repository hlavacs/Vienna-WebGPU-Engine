#pragma once
/**
 * @file WebGPUMesh.h
 * @brief GPU-side mesh: wraps vertex/index buffers.
 */
#include <webgpu/webgpu.hpp>
#include <cstdint>

namespace engine::rendering::webgpu
{

	/**
	 * @class WebGPUMesh
	 * @brief Uploads Mesh data to GPU buffers.
	 */
	class WebGPUMesh
	{
	public:
		/**
		 * @brief Construct from vertex and index buffers, and their respective counts.
		 */
		WebGPUMesh(wgpu::Buffer vertexBuffer, uint32_t vertexCount, wgpu::Buffer indexBuffer, uint32_t indexCount);
		/** @brief Get vertex buffer. */
		wgpu::Buffer getVertexBuffer() const;
		/** @brief Get index buffer (may be null). */
		wgpu::Buffer getIndexBuffer() const;
		/** @brief Get index count. */
		uint32_t getIndexCount() const;
		/** @brief Get vertex count. */
		uint32_t getVertexCount() const;

	private:
		wgpu::Buffer m_vertexBuffer;
		wgpu::Buffer m_indexBuffer;
		uint32_t m_vertexCount = 0;
		uint32_t m_indexCount = 0;
	};

} // namespace engine::rendering::webgpu
