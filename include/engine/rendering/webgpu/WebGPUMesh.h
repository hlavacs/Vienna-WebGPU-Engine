#pragma once
/**
 * @file WebGPUMesh.h
 * @brief GPU-side mesh: wraps vertex/index buffers.
 */
#include <webgpu/webgpu.hpp>
#include "engine/rendering/Mesh.h"

namespace engine::rendering::webgpu {

/**
 * @class WebGPUMesh
 * @brief Uploads Mesh data to GPU buffers.
 */
class WebGPUMesh {
public:
    /**
     * @brief Construct from a CPU-side Mesh.
     */
    WebGPUMesh(WebGPUContext& context, const engine::rendering::Mesh& mesh);
    /** @brief Get vertex buffer. */
    wgpu::Buffer getVertexBuffer() const;
    /** @brief Get index buffer (may be null). */
    wgpu::Buffer getIndexBuffer() const;
    /** @brief Get index count. */
    uint32_t getIndexCount() const;
    /** @brief Get vertex count. */
    uint32_t getVertexCount() const;
private:
    WebGPUContext& m_context;
    // ...internal GPU buffer members...
};

} // namespace engine::rendering::webgpu
