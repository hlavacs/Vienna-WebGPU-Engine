#pragma once

#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/cache/CacheRegistry.h"
#include "engine/rendering/webgpu/DeviceLimitsConfig.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUDepthStencilStateFactory.h"
#include "engine/rendering/webgpu/WebGPUDepthTextureFactory.h"
#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassFactory.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUSurfaceManager.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <sdl3webgpu.h>

// Forward declarations
namespace engine::lighting
{
class LightManager;
class SceneLightBuffer;
} // namespace engine::lighting

namespace engine::rendering
{
class ClusterManager;
class FrameProfiler;
} // namespace engine::rendering

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
	WebGPUContext(const WebGPUContext &) = delete;			  // Remove copy constructor
	WebGPUContext &operator=(const WebGPUContext &) = delete; // Remove copy assignment
	WebGPUContext(WebGPUContext &&) = default;				  // Add move constructor
	WebGPUContext &operator=(WebGPUContext &&) = delete;	  // Add move assignment

	/**
	 * @brief Initialize the WebGPU context. Must be called once at startup.
	 * @param windowHandle Platform-specific window handle (e.g., SDL_Window*)
	 * @param enableVSync If true, use Fifo present mode (VSync), otherwise use Immediate
	 * @param limits Device limits configuration. If not provided, standard limits will be used.
	 */
	void initialize(void *windowHandle, bool enableVSync = true, const std::optional<DeviceLimitsConfig> &limits = std::nullopt);

	/**
	 * @brief Update the present mode (VSync setting) at runtime.
	 * @param enableVSync If true, use Fifo present mode (VSync), otherwise use Immediate
	 */
	void updatePresentMode(bool enableVSync);

	/**
	 * @brief Releases and nulls the surface. Safe to call multiple times.
	 */
	void terminateSurface();

	/** @brief Returns a valid surface, re-initializing if needed. */
	[[nodiscard]] wgpu::Surface getSurface();
	/** @brief Returns the WebGPU instance. */
	[[nodiscard]] wgpu::Instance getInstance() const { return m_instance; }
	/** @brief Returns the WebGPU adapter. */
	[[nodiscard]] wgpu::Adapter getAdapter() const { return m_adapter; }
	/** @brief Returns the WebGPU device. */
	[[nodiscard]] wgpu::Device getDevice() const { return m_device; }
	/** @brief Returns the WebGPU queue. */
	[[nodiscard]] wgpu::Queue getQueue() const { return m_queue; }
	/** @brief Returns the swap chain format. */
	[[nodiscard]] wgpu::TextureFormat getSwapChainFormat() const { return m_swapChainFormat; }

	/** @brief Returns the hardware limits of the device. */
	[[nodiscard]] wgpu::SupportedLimits getHardwareLimits() const;
	/** @brief Returns the resolved device limits. */
	[[nodiscard]] const wgpu::Limits &resolvedLimits() const { return m_resolvedLimits; }
	/** @brief Returns the device limits configuration. */
	[[nodiscard]] const DeviceLimitsConfig &limitsConfig() const { return m_limitsConfig; }

	/** @brief True iff the device was created with the `timestamp-query` feature.
	 *  FrameProfiler checks this before allocating its query set. */
	[[nodiscard]] bool supportsTimestampQuery() const { return m_supportsTimestampQuery; }

	/** @brief Renderer-owned FrameProfiler (set during Renderer::initialize).
	 *  Passes use this to write GPU timestamps without taking a profiler
	 *  reference themselves. Null until Renderer wires it up. */
	void setFrameProfiler(engine::rendering::FrameProfiler *profiler) { m_frameProfiler = profiler; }
	[[nodiscard]] engine::rendering::FrameProfiler *frameProfiler() const { return m_frameProfiler; }

	/** @brief Returns the surface manager. */
	[[nodiscard]] WebGPUSurfaceManager &surfaceManager();
	/** @brief Returns the mesh factory. */
	[[nodiscard]] WebGPUMeshFactory &meshFactory();
	/** @brief Returns the texture factory. */
	[[nodiscard]] WebGPUTextureFactory &textureFactory();
	/** @brief Returns the material factory. */
	[[nodiscard]] WebGPUMaterialFactory &materialFactory();
	/** @brief Returns the sampler factory. */
	[[nodiscard]] WebGPUSamplerFactory &samplerFactory();
	/** @brief Returns the buffer factory. */
	[[nodiscard]] WebGPUBufferFactory &bufferFactory();
	/** @brief Returns the bind group factory. */
	[[nodiscard]] WebGPUBindGroupFactory &bindGroupFactory();
	// [[nodiscard]] WebGPUSwapChainFactory &swapChainFactory();
	/** @brief Returns the depth texture factory. */
	[[nodiscard]] WebGPUDepthTextureFactory &depthTextureFactory();
	/** @brief Returns the depth-stencil state factory. */
	[[nodiscard]] WebGPUDepthStencilStateFactory &depthStencilStateFactory();
	/** @brief Returns the render pass factory. */
	[[nodiscard]] WebGPURenderPassFactory &renderPassFactory();
	/** @brief Returns the model factory. */
	[[nodiscard]] WebGPUModelFactory &modelFactory();
	/** @brief Returns the shader factory. */
	[[nodiscard]] WebGPUShaderFactory &shaderFactory();
	/** @brief Returns the shader registry. */
	[[nodiscard]] ShaderRegistry &shaderRegistry();
	/** @brief Returns the pipeline manager. */
	[[nodiscard]] WebGPUPipelineManager &pipelineManager();
	/** @brief Returns the pipeline factory for low-level / uncached pipeline
	 *  creation (compute pipelines, one-shot bakes). Prefer pipelineManager()
	 *  for cached render pipelines. */
	[[nodiscard]] WebGPUPipelineFactory &pipelineFactory();
	/** @brief Central registry for every factory's resource cache. Renderer
	 *  pumps notifyFrameAll() each frame and clearAll() on resize/scene
	 *  reload; factories register themselves on construction. */
	[[nodiscard]] engine::rendering::cache::CacheRegistry &cacheRegistry();
	/** @brief Returns the light manager. */
	[[nodiscard]] std::shared_ptr<engine::lighting::LightManager> lightManager() const;
	/** @brief Returns the scene light buffer. */
	[[nodiscard]] std::shared_ptr<engine::lighting::SceneLightBuffer> sceneLightBuffer() const;
	/** @brief Returns the cluster manager for deferred rendering. */
	[[nodiscard]] std::shared_ptr<engine::rendering::ClusterManager> clusterManager() const;

	/**
	 * @brief Create a command encoder with an optional label.
	 * @param label Optional label for debugging.
	 * @return Created command encoder.
	 */
	wgpu::CommandEncoder createCommandEncoder(const char *label = nullptr)
	{
		wgpu::CommandEncoderDescriptor desc{};
		desc.label = label;
		return getDevice().createCommandEncoder(desc);
	}

	/**
	 * @brief Create a query set (e.g. GPU timestamp queries for profiling).
	 *
	 * A query set has no cached identity, so it lives as a context helper
	 * alongside createCommandEncoder rather than getting its own factory.
	 * @param desc Query set descriptor.
	 * @return Created query set.
	 */
	wgpu::QuerySet createQuerySet(const wgpu::QuerySetDescriptor &desc)
	{
		return getDevice().createQuerySet(desc);
	}

	/**
	 * @brief Submit a command encoder to the queue and release it.
	 * @param encoder Command encoder to submit.
	 * @param label Optional label for the command buffer.
	 */
	void submitCommandEncoder(wgpu::CommandEncoder &encoder, const char *label = nullptr)
	{
		wgpu::CommandBufferDescriptor cmdDesc{};
		cmdDesc.label = label;
		wgpu::CommandBuffer cmdBuffer = encoder.finish(cmdDesc);
		encoder.release();
		getQueue().submit(cmdBuffer);
		cmdBuffer.release();
	}

  private:
	void initSurface(void *windowHandle);
	void initAdapter();
	void initDevice(const std::optional<DeviceLimitsConfig> &limits);

	template <typename T>
	static T clampLimit(const char *name, T requested, T supported);

	wgpu::Instance m_instance = nullptr;
	wgpu::Surface m_surface = nullptr;
	wgpu::Adapter m_adapter = nullptr;
	wgpu::Device m_device = nullptr;
	wgpu::Queue m_queue = nullptr;
	wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
	wgpu::Sampler m_defaultSampler = nullptr;

	wgpu::Limits m_resolvedLimits{};
	bool m_supportsTimestampQuery = false;
	engine::rendering::FrameProfiler *m_frameProfiler = nullptr;
	DeviceLimitsConfig m_limitsConfig{};

	void *m_lastWindowHandle = nullptr;

	// Declared BEFORE the factories so it outlives them — factories register
	// (and may unregister on destruction) CacheViews with the registry, and
	// C++ destroys fields in reverse declaration order. The registry must
	// still exist while factories are tearing down.
	engine::rendering::cache::CacheRegistry m_cacheRegistry;

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

	// Light management
	std::shared_ptr<engine::lighting::LightManager> m_lightManager;
	std::shared_ptr<engine::lighting::SceneLightBuffer> m_sceneLightBuffer;

	// Deferred rendering
	std::shared_ptr<engine::rendering::ClusterManager> m_clusterManager;
};

} // namespace engine::rendering::webgpu
