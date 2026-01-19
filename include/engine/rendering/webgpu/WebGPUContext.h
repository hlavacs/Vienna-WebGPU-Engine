#pragma once

#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUDepthStencilStateFactory.h"
#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassFactory.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUSurfaceManager.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"
#include "engine/rendering/webgpu/WebGPUDepthTextureFactory.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <sdl2webgpu.h>

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
	WebGPUContext(const WebGPUContext &) = delete;				   // Remove copy constructor
	WebGPUContext &operator=(const WebGPUContext &) = delete;	   // Remove copy assignment
	WebGPUContext(WebGPUContext &&) noexcept = default;			   // Add move constructor
	WebGPUContext &operator=(WebGPUContext &&) noexcept = default; // Add move assignment
	/**
	 * @brief Initialize the WebGPU context. Must be called once at startup.
	 * @param windowHandle Platform-specific window handle (e.g., SDL_Window*)
	 * @param enableVSync If true, use Fifo present mode (VSync), otherwise use Immediate
	 */
	void initialize(void *windowHandle, bool enableVSync = true);

	/**
	 * @brief Update the present mode (VSync setting) at runtime.
	 * @param enableVSync If true, use Fifo present mode (VSync), otherwise use Immediate
	 */
	void updatePresentMode(bool enableVSync);

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

	// === Surface Manager Accessor ===
	WebGPUSurfaceManager &surfaceManager();

	// === Factory Accessors ===
	WebGPUMeshFactory &meshFactory();
	WebGPUTextureFactory &textureFactory();
	WebGPUMaterialFactory &materialFactory();
	WebGPUSamplerFactory &samplerFactory();
	WebGPUBufferFactory &bufferFactory();
	WebGPUBindGroupFactory &bindGroupFactory();
	// WebGPUSwapChainFactory &swapChainFactory();
	WebGPUDepthTextureFactory &depthTextureFactory();
	WebGPUDepthStencilStateFactory &depthStencilStateFactory();
	WebGPURenderPassFactory &renderPassFactory();
	WebGPUModelFactory &modelFactory();
	WebGPUShaderFactory &shaderFactory();
	ShaderRegistry &shaderRegistry();
	WebGPUPipelineManager &pipelineManager();

  private:
	void initDevice();
	void initSurface(void *windowHandle);

	void *m_lastWindowHandle = nullptr;
	wgpu::Instance m_instance = nullptr;
	wgpu::Surface m_surface = nullptr;
	wgpu::Adapter m_adapter = nullptr;
	wgpu::Device m_device = nullptr;
	wgpu::Queue m_queue = nullptr;
	wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
	wgpu::Sampler m_defaultSampler = nullptr;

	// Surface manager
	std::unique_ptr<WebGPUSurfaceManager> m_surfaceManager;

	// Factory members
	std::unique_ptr<WebGPUMeshFactory> m_meshFactory;
	std::unique_ptr<WebGPUTextureFactory> m_textureFactory;
	std::unique_ptr<WebGPUMaterialFactory> m_materialFactory;
	std::unique_ptr<WebGPUSamplerFactory> m_samplerFactory;
	std::unique_ptr<WebGPUBufferFactory> m_bufferFactory;
	std::unique_ptr<WebGPUBindGroupFactory> m_bindGroupFactory;
	// std::unique_ptr<WebGPUSwapChainFactory> m_swapChainFactory;
	std::unique_ptr<WebGPUDepthTextureFactory> m_depthTextureFactory;
	std::unique_ptr<WebGPUDepthStencilStateFactory> m_depthStencilStateFactory;
	std::unique_ptr<WebGPURenderPassFactory> m_renderPassFactory;
	std::unique_ptr<WebGPUModelFactory> m_modelFactory;
	std::unique_ptr<WebGPUShaderFactory> m_shaderFactory;
	std::unique_ptr<ShaderRegistry> m_shaderRegistry;
	std::unique_ptr<WebGPUPipelineManager> m_pipelineManager;
};

} // namespace engine::rendering::webgpu
