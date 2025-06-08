#pragma once
/**
 * @file WebGPUContext.h
 * @brief Singleton for WebGPU device, queue, swapchain format, and GPU utilities.
 */

#include <webgpu/webgpu.hpp>
#include <memory>
#include <SDL2/SDL.h>
#include <external/sdl2webgpu/sdl2webgpu.h>
#include <vector>
#include <span>

namespace engine::rendering::webgpu {
// Forward declarations for factories
class WebGPUMeshFactory;
class WebGPUTextureFactory;
class WebGPUMaterialFactory;
}

/**
 * @class WebGPUContext
 * @brief Singleton managing the WebGPU device, queue, swapchain format, and GPU helper utilities.
 */
class engine::rendering::webgpu::WebGPUContext {
public:
    WebGPUContext();
    /**
     * @brief Initialize the WebGPU context. Must be called once at startup.
     * @param windowHandle Platform-specific window handle (e.g., SDL_Window*)
     */
    void initialize(void* windowHandle);

    wgpu::Instance getInstance() const { return m_instance; }
    wgpu::Surface getSurface() const { return m_surface; }
    wgpu::Adapter getAdapter() const { return m_adapter; }
    wgpu::Device getDevice() const { return m_device; }
    wgpu::Queue getQueue() const { return m_queue; }
    wgpu::TextureFormat getSwapChainFormat() const { return m_swapChainFormat; }
    wgpu::Sampler getDefaultSampler() const { return m_defaultSampler; }

    // === Buffer Utilities ===

    /**
     * @brief Creates a GPU buffer with the given descriptor.
     */
    wgpu::Buffer createBuffer(const wgpu::BufferDescriptor& desc) const;

    /**
     * @brief Creates and uploads data to a GPU buffer.
     * @param data Pointer to data to upload.
     * @param size Size in bytes.
     * @param usage Buffer usage flags.
     */
    wgpu::Buffer createBufferWithData(const void* data, size_t size, wgpu::BufferUsage usage) const;

    /**
     * @brief Creates and uploads data from a std::vector<T>.
     */
    template <typename T>
    wgpu::Buffer createBufferWithData(const std::vector<T>& vec, wgpu::BufferUsage usage) const {
        return createBufferWithData(vec.data(), vec.size() * sizeof(T), usage);
    }

    /**
     * @brief Creates and uploads data from a std::span<T>.
     */
    template <typename T>
    wgpu::Buffer createBufferWithData(std::span<T> span, wgpu::BufferUsage usage) const {
        return createBufferWithData(span.data(), span.size_bytes(), usage);
    }

    // === Texture Utility ===
    wgpu::Texture createTexture(const wgpu::TextureDescriptor& desc) const;

    // === Factory Accessors ===
    WebGPUMeshFactory& meshFactory();
    WebGPUTextureFactory& textureFactory();
    WebGPUMaterialFactory& materialFactory();

private:
    void initDevice();

    wgpu::Instance m_instance = nullptr;
    wgpu::Surface m_surface = nullptr;
    wgpu::Adapter m_adapter = nullptr;
    wgpu::Device m_device = nullptr;
    wgpu::Queue m_queue = nullptr;
    wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
    wgpu::Sampler m_defaultSampler = nullptr;

    std::unique_ptr<WebGPUMeshFactory> m_meshFactory;
    std::unique_ptr<WebGPUTextureFactory> m_textureFactory;
    std::unique_ptr<WebGPUMaterialFactory> m_materialFactory;
};

} // namespace engine::rendering::webgpu
