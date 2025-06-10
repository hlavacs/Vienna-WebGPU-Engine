#pragma once
/**
 * @file WebGPUContext.h
 * @brief Singleton for WebGPU device, queue, swapchain format, and GPU utilities.
 */

#include <webgpu/webgpu.hpp>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <sdl2webgpu.h>

#include <memory>
#include <vector>

#include "engine/rendering/webgpu/WebGPUTextureFactory.h"
#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"

namespace engine::rendering::webgpu
{
	// Forward declarations for factories
	class WebGPUMeshFactory;
	class WebGPUTextureFactory;
	class WebGPUMaterialFactory;
}

namespace engine::rendering::webgpu
{
	/**
	 * @class WebGPUContext
	 * @brief Singleton managing the WebGPU device, queue, swapchain format, and GPU helper utilities.
	 */
	class WebGPUContext
	{
	public:
		explicit WebGPUContext();
		WebGPUContext(const WebGPUContext&) = delete; // Remove copy constructor
		WebGPUContext& operator=(const WebGPUContext&) = delete; // Remove copy assignment
		WebGPUContext(WebGPUContext&&) noexcept = default; // Add move constructor
		WebGPUContext& operator=(WebGPUContext&&) noexcept = default; // Add move assignment
		/**
		 * @brief Initialize the WebGPU context. Must be called once at startup.
		 * @param windowHandle Platform-specific window handle (e.g., SDL_Window*)
		 */
		void initialize(void *windowHandle);

		/**
		 * @brief Releases and nulls the surface. Safe to call multiple times.
		 */
		void terminateSurface();

		/**
		 * @brief Returns a valid surface, re-initializing if needed.
		 */
		wgpu::Surface getSurface();

		wgpu::Instance getInstance() const { return m_instance; }
		wgpu::Adapter getAdapter() const { return m_adapter; }
		wgpu::Device getDevice() const { return m_device; }
		wgpu::Queue getQueue() const { return m_queue; }
		wgpu::TextureFormat getSwapChainFormat() const { return m_swapChainFormat; }
		wgpu::Sampler getDefaultSampler() const { return m_defaultSampler; }

		// === Buffer Utilities ===

		/**
		 * @brief Creates a GPU buffer with the given descriptor.
		 */
		wgpu::Buffer createBuffer(const wgpu::BufferDescriptor &desc);

		/**
		 * @brief Creates and uploads data to a GPU buffer.
		 * @param data Pointer to data to upload.
		 * @param size Size in bytes.
		 * @param usage Buffer usage flags.
		 */
		wgpu::Buffer createBufferWithData(const void *data, size_t size, wgpu::BufferUsage usage);

		/**
		 * @brief Creates and uploads data from a std::vector<T>.
		 */
		template <typename T>
		wgpu::Buffer createBufferWithData(const std::vector<T> &vec, wgpu::BufferUsage usage)
		{
			return createBufferWithData(vec.data(), vec.size() * sizeof(T), usage);
		}

		// === Texture Utility ===
		wgpu::Texture createTexture(const wgpu::TextureDescriptor &desc);

		// === Factory Accessors ===
		WebGPUMeshFactory &meshFactory();
		WebGPUTextureFactory &textureFactory();
		WebGPUMaterialFactory &materialFactory();

	private:
		void initDevice();
		void initSurface(void* windowHandle);

		void* m_lastWindowHandle = nullptr;
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
