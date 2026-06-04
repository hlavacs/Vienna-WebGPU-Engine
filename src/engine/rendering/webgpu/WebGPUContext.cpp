#include "engine/rendering/webgpu/WebGPUContext.h"

#include <cassert>
#include <iostream>

#include "engine/rendering/Vertex.h"
#include "engine/lighting/LightManager.h"
#include "engine/rendering/SceneLightBuffer.h"
#include "engine/rendering/ClusterManager.h"

namespace engine::rendering::webgpu
{

template <typename T>
T WebGPUContext::clampLimit(const char *name, T requested, T supported)
{
	if (requested > supported)
	{
		spdlog::warn("[WebGPU] Limit '{}': requested {} exceeds hardware max {}, clamping to {}.", name, requested, supported, supported);
		return supported;
	}
	return requested;
}

WebGPUContext::WebGPUContext() :
	m_surface(nullptr) {}

void WebGPUContext::initialize(void *windowHandle, bool enableVSync, const std::optional<DeviceLimitsConfig> &limits)
{
	m_lastWindowHandle = windowHandle;

	m_surfaceManager = std::make_unique<WebGPUSurfaceManager>(*this);
	m_bufferFactory = std::make_unique<WebGPUBufferFactory>(*this);
	m_meshFactory = std::make_unique<WebGPUMeshFactory>(*this);
	m_textureFactory = std::make_unique<WebGPUTextureFactory>(*this);
	m_materialFactory = std::make_unique<WebGPUMaterialFactory>(*this);
	m_bindGroupFactory = std::make_unique<WebGPUBindGroupFactory>(*this);
	m_samplerFactory = std::make_unique<WebGPUSamplerFactory>(*this);
	// m_swapChainFactory = std::make_unique<WebGPUSwapChainFactory>(*this);
	m_depthTextureFactory = std::make_unique<WebGPUDepthTextureFactory>(*this);
	m_modelFactory = std::make_unique<WebGPUModelFactory>(*this);
	m_depthStencilStateFactory = std::make_unique<WebGPUDepthStencilStateFactory>();
	m_renderPassFactory = std::make_unique<WebGPURenderPassFactory>(*this);
	m_shaderFactory = std::make_unique<WebGPUShaderFactory>(*this);
	m_pipelineManager = std::make_unique<WebGPUPipelineManager>(*this);

	// Uniform registration: every factory now exposes the same cache
	// lifecycle surface (cleanup / cacheSize / notifyFrame / evictStale /
	// setMaxIdleFrames). The template wires all of it up — no per-factory
	// boilerplate. Add a new factory in three lines: add cacheSize() +
	// the eviction methods (BaseWebGPUFactory derivatives get them for
	// free), construct it here, register it.
	m_cacheRegistry.registerFactoryCache(*m_pipelineManager, "PipelineManager");
	m_cacheRegistry.registerFactoryCache(*m_samplerFactory,  "SamplerFactory");
	m_cacheRegistry.registerFactoryCache(*m_textureFactory,  "TextureFactory");
	m_cacheRegistry.registerFactoryCache(*m_meshFactory,     "MeshFactory");
	m_cacheRegistry.registerFactoryCache(*m_materialFactory, "MaterialFactory");
	m_cacheRegistry.registerFactoryCache(*m_modelFactory,    "ModelFactory");

	// Sensible defaults — values in frames; assume ~60fps target. The
	// principle: cheaper-to-rebuild + smaller-resource caches keep stuff
	// longer; bigger / more-expensive resources evict faster so memory
	// doesn't accumulate.
	//  - Pipelines (heavy to rebuild, but smallish per-instance): 10s
	//  - Textures (huge memory per entry, fast to recreate from disk):  60s
	//  - Meshes (largest GPU resource we own):                          60s
	//  - Models (lightweight wrappers):                                 60s
	//  - Materials (tiny structs):                                     120s
	//  - Samplers (tiny wgpu handles, churn is wasteful):                0 (never evict)
	//
	// Override these per-scene from any caller via
	// `context.cacheRegistry().setMaxIdleFramesFor("TextureFactory", N);`
	// or apply a uniform window with `setMaxIdleFramesForAll(N)`.
	m_cacheRegistry.setMaxIdleFramesFor("PipelineManager",  600);
	m_cacheRegistry.setMaxIdleFramesFor("TextureFactory", 3600);
	m_cacheRegistry.setMaxIdleFramesFor("MeshFactory",    3600);
	m_cacheRegistry.setMaxIdleFramesFor("ModelFactory",   3600);
	m_cacheRegistry.setMaxIdleFramesFor("MaterialFactory",7200);
	m_cacheRegistry.setMaxIdleFramesFor("SamplerFactory",    0);

	m_lightManager = std::make_shared<engine::lighting::LightManager>();
	m_clusterManager = std::make_shared<engine::rendering::ClusterManager>(*this);

#ifdef __EMSCRIPTEN__
	m_instance = wgpu::wgpuCreateInstance(nullptr);
#else
	m_instance = wgpu::createInstance(wgpu::InstanceDescriptor{});
#endif
	if (!m_instance)
	{
		spdlog::critical("[WebGPU] Failed to create WebGPU instance.");
		assert(false);
	}

	// Order matters: surface must exist before adapter so compatibleSurface is set correctly
	initSurface(windowHandle);
	initAdapter();
	initDevice(limits);

	// Initialize ShaderRegistry after device is ready
	m_shaderRegistry = std::make_unique<ShaderRegistry>(*this);
	if(!m_shaderRegistry->initializeDefaultShaders())
	{
		spdlog::critical("[WebGPU] Failed to initialize default shaders.");
		assert(false);
	}

	// SceneLightBuffer depends on the light bind group layout that is registered
	// during shader initialization, so create it after the shader registry is ready.
	m_sceneLightBuffer = std::make_shared<engine::lighting::SceneLightBuffer>(*this);

	// Initialize ClusterManager for clustered light assignment
	if (!m_clusterManager->initialize())
	{
		spdlog::critical("[WebGPU] Failed to initialize ClusterManager.");
		assert(false);
	}

	// ToDo: Move this to the surface manager
	auto *sdlWindow = static_cast<SDL_Window *>(windowHandle);
	int width = 0, height = 0;
	SDL_GL_GetDrawableSize(sdlWindow, &width, &height);

	WebGPUSurfaceManager::Config config;
	config.format = getSwapChainFormat();
	config.width = width;
	config.height = height;
	config.presentMode = enableVSync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate;
	m_surfaceManager->reconfigure(config);

	spdlog::info("[WebGPU] Initialized. Surface {}x{}, vsync: {}.", width, height, enableVSync);
}

void WebGPUContext::initSurface(void *windowHandle)
{
	if (m_surface)
		return;

	auto *sdlWindow = static_cast<SDL_Window *>(windowHandle);

	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	if (!SDL_GetWindowWMInfo(sdlWindow, &wmInfo))
	{
		spdlog::critical("[WebGPU] SDL_GetWindowWMInfo failed: {}", SDL_GetError());
		assert(false);
	}

	WGPUSurface rawSurface = nullptr;

#if defined(SDL_VIDEO_DRIVER_WAYLAND)
	if (wmInfo.subsystem == SDL_SYSWM_WAYLAND)
	{
		spdlog::info("[WebGPU] Creating Wayland surface.");
		WGPUSurfaceDescriptorFromWaylandSurface waylandDesc{};
		waylandDesc.chain.sType = WGPUSType_SurfaceDescriptorFromWaylandSurface;
		waylandDesc.chain.next = nullptr;
		waylandDesc.display = wmInfo.info.wl.display;
		waylandDesc.surface = wmInfo.info.wl.surface;

		WGPUSurfaceDescriptor surfaceDesc{};
		surfaceDesc.nextInChain = &waylandDesc.chain;
		rawSurface = wgpuInstanceCreateSurface(m_instance, &surfaceDesc);
	}
#endif

#if defined(SDL_VIDEO_DRIVER_X11)
	if (!rawSurface && wmInfo.subsystem == SDL_SYSWM_X11)
	{
		spdlog::info("[WebGPU] Creating X11 surface.");
		WGPUSurfaceDescriptorFromXlibWindow xlibDesc{};
		xlibDesc.chain.sType = WGPUSType_SurfaceDescriptorFromXlibWindow;
		xlibDesc.chain.next = nullptr;
		xlibDesc.display = wmInfo.info.x11.display;
		xlibDesc.window = wmInfo.info.x11.window;

		WGPUSurfaceDescriptor surfaceDesc{};
		surfaceDesc.nextInChain = &xlibDesc.chain;
		rawSurface = wgpuInstanceCreateSurface(m_instance, &surfaceDesc);
	}
#endif
	if (!rawSurface)
	{
		rawSurface = SDL_GetWGPUSurface(m_instance, sdlWindow);
	}

	if (!rawSurface)
	{
		spdlog::critical("[WebGPU] Failed to create WebGPU surface.");
		assert(false);
	}

	m_surface = wgpu::Surface(rawSurface);
}

void WebGPUContext::initAdapter()
{
	wgpu::RequestAdapterOptions adapterOpts{};
	adapterOpts.compatibleSurface = m_surface;
	m_adapter = m_instance.requestAdapter(adapterOpts);

	if (!m_adapter)
	{
		spdlog::critical("[WebGPU] Failed to acquire a compatible adapter.");
		assert(false);
	}

	spdlog::info("[WebGPU] Adapter acquired.");
}

void WebGPUContext::initDevice(const std::optional<DeviceLimitsConfig> &limits)
{
	// --------------- Query supported limits ---------------
	wgpu::SupportedLimits supportedLimits{};
#ifdef WEBGPU_BACKEND_WGPU
	m_adapter.getLimits(&supportedLimits);
#else
	supportedLimits.limits.minStorageBufferOffsetAlignment = 256;
	supportedLimits.limits.minUniformBufferOffsetAlignment = 256;
#endif

	spdlog::info("[WebGPU] Adapter limits: maxBindGroups={}, maxBindingsPerBindGroup={}, maxSampledTexturesPerShaderStage={}",
	             supportedLimits.limits.maxBindGroups,
	             supportedLimits.limits.maxBindingsPerBindGroup,
	             supportedLimits.limits.maxSampledTexturesPerShaderStage);

	// --------------- Clamp requested limits against hardware ---------------
	wgpu::RequiredLimits requiredLimits = wgpu::Default;
	const DeviceLimitsConfig resolved = limits
											? limits->clamped(supportedLimits)
											: DeviceLimitsConfig::fromSupported(supportedLimits);
	m_limitsConfig = resolved;
	resolved.applyTo(requiredLimits);

	// Alignment limits are hardware-fixed — must always use the adapter's value
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

	// Store what was actually resolved for later inspection via resolvedLimits()
	m_resolvedLimits = requiredLimits.limits;

	// --------------- Optional features ---------------
	// timestamp-query enables wgpu::QuerySet of type Timestamp + encoder.writeTimestamp(),
	// which the FrameProfiler uses for real per-pass GPU times. Falls back to
	// CPU-only timing on adapters that don't advertise the feature.
	std::vector<WGPUFeatureName> requiredFeatures;
	m_supportsTimestampQuery = m_adapter.hasFeature(wgpu::FeatureName::TimestampQuery);
	if (m_supportsTimestampQuery)
		requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);

	// --------------- Request device ---------------
	wgpu::DeviceDescriptor deviceDesc{};
	deviceDesc.label = "WebGPUContext Device";
	deviceDesc.requiredFeatureCount = static_cast<uint32_t>(requiredFeatures.size());
	deviceDesc.requiredFeatures = requiredFeatures.empty() ? nullptr : requiredFeatures.data();
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.defaultQueue.label = "Default Queue";
	m_device = m_adapter.requestDevice(deviceDesc);

	if (!m_device)
	{
		spdlog::critical("[WebGPU] Failed to create device.");
		assert(false);
	}

	// --------------- Error callback ---------------
	static std::unique_ptr<wgpu::ErrorCallback> errorCallback;
	errorCallback = m_device.setUncapturedErrorCallback(
		[](wgpu::ErrorType type, char const *message)
		{ spdlog::error("[WebGPU] Device error (type {}): {}", static_cast<int>(type), message ? message : "unknown"); }
	);

	// --------------- Queue ---------------
	m_queue = m_device.getQueue();
	if (!m_queue)
	{
		spdlog::critical("[WebGPU] Failed to get device queue.");
		assert(false);
	}

	// --------------- Swap chain format ---------------
#ifdef WEBGPU_BACKEND_WGPU
	m_swapChainFormat = m_surface.getPreferredFormat(m_adapter);
#else
	m_swapChainFormat = wgpu::TextureFormat::BGRA8Unorm;
#endif

	if (m_swapChainFormat == wgpu::TextureFormat::Undefined)
	{
		spdlog::critical("[WebGPU] Could not determine swap chain format.");
		assert(false);
	}

	m_adapter.release();
	spdlog::info("[WebGPU] Device created successfully.");
}

wgpu::SupportedLimits WebGPUContext::getHardwareLimits() const
{
	wgpu::SupportedLimits limits{};
	wgpuDeviceGetLimits(m_device, &limits);
	return limits;
}

void WebGPUContext::updatePresentMode(bool enableVSync)
{
	if (!m_surfaceManager)
	{
		spdlog::error("[WebGPU] Cannot update present mode: Surface manager not initialized.");
		assert(false);
	}

	auto currentConfig = m_surfaceManager->currentConfig();
	currentConfig.presentMode = enableVSync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate;
	m_surfaceManager->reconfigure(currentConfig);
}

void WebGPUContext::terminateSurface()
{
	if (m_surface)
	{
		m_surface.unconfigure();
		m_surface.release();
		m_surface = nullptr;
	}
}

wgpu::Surface WebGPUContext::getSurface()
{
	if (!m_surface)
	{
		initSurface(m_lastWindowHandle);
	}
	return m_surface;
}

WebGPUSurfaceManager &WebGPUContext::surfaceManager()
{
	if (!m_surfaceManager)
	{
		throw std::runtime_error("WebGPUSurfaceManager not initialized!");
	}
	return *m_surfaceManager;
}

WebGPUMeshFactory &WebGPUContext::meshFactory()
{
	if (!m_meshFactory)
	{
		throw std::runtime_error("WebGPUMeshFactory not initialized!");
	}
	return *m_meshFactory;
}

WebGPUTextureFactory &WebGPUContext::textureFactory()
{
	if (!m_textureFactory)
	{
		throw std::runtime_error("WebGPUTextureFactory not initialized!");
	}
	return *m_textureFactory;
}

WebGPUMaterialFactory &WebGPUContext::materialFactory()
{
	if (!m_materialFactory)
	{
		throw std::runtime_error("WebGPUMaterialFactory not initialized!");
	}
	return *m_materialFactory;
}

WebGPUSamplerFactory &WebGPUContext::samplerFactory()
{
	if (!m_samplerFactory)
	{
		throw std::runtime_error("WebGPUSamplerFactory not initialized!");
	}
	return *m_samplerFactory;
}

WebGPUBufferFactory &WebGPUContext::bufferFactory()
{
	if (!m_bufferFactory)
	{
		throw std::runtime_error("WebGPUBufferFactory not initialized!");
	}
	return *m_bufferFactory;
}

WebGPUBindGroupFactory &WebGPUContext::bindGroupFactory()
{
	if (!m_bindGroupFactory)
	{
		throw std::runtime_error("WebGPUBindGroupFactory not initialized!");
	}
	return *m_bindGroupFactory;
}

/* Todo: Implement WebGPUSwapChainFactory
WebGPUSwapChainFactory &WebGPUContext::swapChainFactory()
{
	if (!m_swapChainFactory)
	{
		throw std::runtime_error("WebGPUSwapChainFactory not initialized!");
	}
	return *m_swapChainFactory;
}*/

WebGPUModelFactory &WebGPUContext::modelFactory()
{
	if (!m_modelFactory)
	{
		throw std::runtime_error("WebGPUModelFactory not initialized!");
	}
	return *m_modelFactory;
}

WebGPUShaderFactory &WebGPUContext::shaderFactory()
{
	if (!m_shaderFactory)
	{
		throw std::runtime_error("WebGPUShaderFactory not initialized!");
	}
	return *m_shaderFactory;
}

ShaderRegistry &WebGPUContext::shaderRegistry()
{
	if (!m_shaderRegistry)
	{
		throw std::runtime_error("ShaderRegistry not initialized!");
	}
	return *m_shaderRegistry;
}

WebGPUDepthTextureFactory &WebGPUContext::depthTextureFactory()
{
	if (!m_depthTextureFactory)
	{
		throw std::runtime_error("WebGPUDepthTextureFactory not initialized!");
	}
	return *m_depthTextureFactory;
}

WebGPUDepthStencilStateFactory &WebGPUContext::depthStencilStateFactory()
{
	if (!m_depthStencilStateFactory)
	{
		throw std::runtime_error("WebGPUDepthStencilStateFactory not initialized!");
	}
	return *m_depthStencilStateFactory;
}

WebGPURenderPassFactory &WebGPUContext::renderPassFactory()
{
	if (!m_renderPassFactory)
	{
		throw std::runtime_error("WebGPURenderPassFactory not initialized!");
	}
	return *m_renderPassFactory;
}

WebGPUPipelineManager &WebGPUContext::pipelineManager()
{
	if (!m_pipelineManager)
	{
		throw std::runtime_error("WebGPUPipelineManager not initialized!");
	}
	return *m_pipelineManager;
}

engine::rendering::cache::CacheRegistry &WebGPUContext::cacheRegistry()
{
	// Default-constructed at WebGPUContext construction; no init guard needed.
	return m_cacheRegistry;
}

std::shared_ptr<engine::lighting::LightManager> WebGPUContext::lightManager() const
{
	if (!m_lightManager)
	{
		spdlog::warn("LightManager not initialized!");
		return nullptr;
	}
	return m_lightManager;
}

std::shared_ptr<engine::lighting::SceneLightBuffer> WebGPUContext::sceneLightBuffer() const
{
	if (!m_sceneLightBuffer)
	{
		spdlog::warn("SceneLightBuffer not initialized!");
		return nullptr;
	}
	return m_sceneLightBuffer;
}

std::shared_ptr<engine::rendering::ClusterManager> WebGPUContext::clusterManager() const
{
	if (!m_clusterManager)
	{
		spdlog::warn("ClusterManager not initialized!");
		return nullptr;
	}
	return m_clusterManager;
}
} // namespace engine::rendering::webgpu
