#include "engine/rendering/webgpu/WebGPUContext.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "engine/lighting/LightManager.h"
#include "engine/rendering/ClusterManager.h"
#include "engine/rendering/SceneLightBuffer.h"
#include "engine/rendering/Vertex.h"

namespace engine::rendering::webgpu
{

namespace
{
const char *presentModeName(wgpu::PresentMode mode)
{
	switch (mode)
	{
	case wgpu::PresentMode::Fifo:
		return "Fifo (vsync)";
	case wgpu::PresentMode::FifoRelaxed:
		return "FifoRelaxed";
	case wgpu::PresentMode::Immediate:
		return "Immediate (uncapped)";
	case wgpu::PresentMode::Mailbox:
		return "Mailbox";
	default:
		return "unknown";
	}
}
} // namespace

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
	m_cacheRegistry.registerFactoryCache(*m_samplerFactory, "SamplerFactory");
	m_cacheRegistry.registerFactoryCache(*m_textureFactory, "TextureFactory");
	m_cacheRegistry.registerFactoryCache(*m_meshFactory, "MeshFactory");
	m_cacheRegistry.registerFactoryCache(*m_materialFactory, "MaterialFactory");
	m_cacheRegistry.registerFactoryCache(*m_modelFactory, "ModelFactory");

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
	m_cacheRegistry.setMaxIdleFramesFor("PipelineManager", 600);
	m_cacheRegistry.setMaxIdleFramesFor("TextureFactory", 3600);
	m_cacheRegistry.setMaxIdleFramesFor("MeshFactory", 3600);
	m_cacheRegistry.setMaxIdleFramesFor("ModelFactory", 3600);
	m_cacheRegistry.setMaxIdleFramesFor("MaterialFactory", 7200);
	m_cacheRegistry.setMaxIdleFramesFor("SamplerFactory", 0);

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
	if (!m_shaderRegistry->initializeDefaultShaders())
	{
		spdlog::critical("[WebGPU] Failed to initialize default shaders.");
		assert(false);
	}
	// Register the shader cache too. ShaderRegistry exposes the same
	// six-method surface as every other factory now that it sits on
	// SlotCache, so the debug overlay can list it alongside the
	// Pipeline/Sampler/Texture caches and the global "soft clear" UI flow
	// reaches it through a single iteration.
	m_cacheRegistry.registerFactoryCache(*m_shaderRegistry, "ShaderRegistry");
	m_cacheRegistry.setMaxIdleFramesFor("ShaderRegistry", 0);

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
	SDL_GetWindowSizeInPixels(sdlWindow, &width, &height);

	WebGPUSurfaceManager::Config config;
	config.format = getSwapChainFormat();
	config.width = width;
	config.height = height;
	// Mailbox (uncapped) > Immediate > Fifo. Dawn compat offers no Mailbox and
	// ignores Immediate (present is ~vsync either way — measured ~40 FPS with both,
	// it's compat render overhead, not a bypassable block), so use FifoRelaxed
	// there: the adaptive-vsync mode Dawn actually honors.
	config.presentMode = enableVSync ? wgpu::PresentMode::Fifo
									 : (m_mailboxSupported ? wgpu::PresentMode::Mailbox
									 : ((m_compatibilityMode && m_fifoRelaxedSupported) ? wgpu::PresentMode::FifoRelaxed
									 : (m_immediateSupported ? wgpu::PresentMode::Immediate : wgpu::PresentMode::Fifo)));
	m_surfaceManager->reconfigure(config);

	spdlog::info("[WebGPU] Initialized. Surface {}x{}, vsync: {}, present mode: {}.", width, height, enableVSync, presentModeName(config.presentMode));
}

void WebGPUContext::initSurface(void *windowHandle)
{
	if (m_surface)
		return;

	auto *sdlWindow = static_cast<SDL_Window *>(windowHandle);

	// sdl3webgpu does the per-platform native-handle extraction (Win32 / X11 /
	// Wayland / Cocoa). SDL3 removed SDL_GetWindowWMInfo in favour of the
	// window-properties API, which sdl3webgpu reads internally — so the old
	// manual per-driver surface descriptors are gone.
	WGPUSurface rawSurface = SDL_GetWGPUSurface(m_instance, sdlWindow);

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
#ifdef WEBGPU_BACKEND_DAWN
	// Desktop Dawn defaults to Core (compat's ~40 FPS is render overhead, not a
	// present block; Core gives Mailbox + lifts cube-array/depth restrictions).
	// DAWN_COMPAT=1 forces the Compatibility profile for local WASM-parity testing.
	if (const char *v = std::getenv("DAWN_COMPAT"); v && std::string(v) == "1")
		m_preferCompatibility = true;
	adapterOpts.featureLevel = m_preferCompatibility ? WGPUFeatureLevel_Compatibility : WGPUFeatureLevel_Core;
	m_compatibilityMode = m_preferCompatibility;
#endif
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
	wgpu::Limits supportedLimits{};
	m_adapter.getLimits(&supportedLimits);

	spdlog::info("[WebGPU] Adapter limits: maxBindGroups={}, maxBindingsPerBindGroup={}, maxSampledTexturesPerShaderStage={}", supportedLimits.maxBindGroups, supportedLimits.maxBindingsPerBindGroup, supportedLimits.maxSampledTexturesPerShaderStage);

	// --------------- Clamp requested limits against hardware ---------------
	// Start from the adapter's full supported set so every limit (incl. Dawn's
	// granular per-stage limits, which default to 0) is requested; applyTo then
	// caps the engine's tuned subset. See doc/WebGPUv24Migration.md.
	wgpu::Limits requiredLimits = supportedLimits;
	const DeviceLimitsConfig resolved = limits
											? limits->clamped(supportedLimits)
											: DeviceLimitsConfig::fromSupported(supportedLimits);
	m_limitsConfig = resolved;
	resolved.applyTo(requiredLimits);

	// Alignment limits are hardware-fixed — must always use the adapter's value
	requiredLimits.minUniformBufferOffsetAlignment = supportedLimits.minUniformBufferOffsetAlignment;
	requiredLimits.minStorageBufferOffsetAlignment = supportedLimits.minStorageBufferOffsetAlignment;

	// Request the adapter's supported max (Dawn honors it; wgpu-native reports 0
	// here and caps MRT at 32 regardless). See doc/WebGPUv24Migration.md.
	requiredLimits.maxColorAttachmentBytesPerSample = supportedLimits.maxColorAttachmentBytesPerSample;
	requiredLimits.maxColorAttachments = supportedLimits.maxColorAttachments;

	// Store what was actually resolved for later inspection via resolvedLimits()
	m_resolvedLimits = requiredLimits;

	// --------------- Optional features ---------------
	// timestamp-query enables wgpu::QuerySet of type Timestamp + encoder.writeTimestamp(),
	// which the FrameProfiler uses for real per-pass GPU times. Falls back to
	// CPU-only timing on adapters that don't advertise the feature.
	std::vector<WGPUFeatureName> requiredFeatures;
	m_supportsTimestampQuery = m_adapter.hasFeature(wgpu::FeatureName::TimestampQuery);
	if (m_supportsTimestampQuery)
	{
		requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);
#ifdef WEBGPU_BACKEND_WGPU
		// v24 gates encoder.writeTimestamp behind this native feature; require it
		// when available, else disable GPU timing. See doc/WebGPUv24Migration.md.
		const auto insideEncoders = static_cast<WGPUFeatureName>(WGPUNativeFeature_TimestampQueryInsideEncoders);
		if (m_adapter.hasFeature(static_cast<wgpu::FeatureName>(insideEncoders)))
			requiredFeatures.push_back(insideEncoders);
		else
			m_supportsTimestampQuery = false;
#endif
	}

	// --------------- Request device ---------------
	wgpu::DeviceDescriptor deviceDesc{};
	deviceDesc.label = wgpu::StringView("WebGPUContext Device");
	deviceDesc.requiredFeatureCount = static_cast<uint32_t>(requiredFeatures.size());
	deviceDesc.requiredFeatures = requiredFeatures.empty() ? nullptr : requiredFeatures.data();
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.defaultQueue.label = wgpu::StringView("Default Queue");

	// v24: uncaptured-error callback goes in the descriptor (captureless fn ptr).
	// See doc/WebGPUv24Migration.md.
	deviceDesc.uncapturedErrorCallbackInfo.callback =
		[](WGPUDevice const *, WGPUErrorType type, WGPUStringView message, void *, void *)
	{
		spdlog::error("[WebGPU] Device error (type {}): {}", static_cast<int>(type), message.data ? std::string(message.data, message.length) : std::string("unknown"));
	};

	// v24: device-lost callback goes in the descriptor. Logs the reason so a
	// driver-level device loss (AMD TDR, OOM) is diagnosable instead of silent.
	deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
	deviceDesc.deviceLostCallbackInfo.callback =
		[](WGPUDevice const *, WGPUDeviceLostReason reason, WGPUStringView message, void *, void *)
	{
		const auto lvl = (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_InstanceDropped)
							 ? spdlog::level::info
							 : spdlog::level::critical;
		spdlog::log(lvl, "[WebGPU] Device lost (reason {}): {}", static_cast<int>(reason), message.data ? std::string(message.data, message.length) : std::string("unknown"));
	};

#ifdef WEBGPU_BACKEND_DAWN
	// Dawn gates encoder.writeTimestamp behind an unsafe-API toggle; enable it so
	// the FrameProfiler's per-pass GPU timers work. See doc/WebGPUv24Migration.md.
	const char *const dawnEnabledToggles[] = {"allow_unsafe_apis"};
	WGPUDawnTogglesDescriptor dawnToggles{};
	dawnToggles.chain.sType = WGPUSType_DawnTogglesDescriptor;
	dawnToggles.enabledToggleCount = 1;
	dawnToggles.enabledToggles = dawnEnabledToggles;
	deviceDesc.nextInChain = &dawnToggles.chain;
#endif

	m_device = m_adapter.requestDevice(deviceDesc);

	if (!m_device)
	{
		spdlog::critical("[WebGPU] Failed to create device.");
		assert(false);
	}

	// --------------- Error callback ---------------
	// Registered via DeviceDescriptor::uncapturedErrorCallbackInfo in the
	// requestDevice block above (wgpu-native v24 removed setUncapturedErrorCallback).

	// --------------- Queue ---------------
	m_queue = m_device.getQueue();
	if (!m_queue)
	{
		spdlog::critical("[WebGPU] Failed to get device queue.");
		assert(false);
	}

	// --------------- Swap chain format ---------------
	// Prefer an sRGB surface so the composite's linear output is gamma-encoded on
	// present. Dawn's compat surface lists linear RGBA8Unorm first (looked dark vs
	// wgpu-native); flag when none exists so the composite can encode gamma itself.
	wgpu::SurfaceCapabilities caps{};
	m_surface.getCapabilities(m_adapter, &caps);

	m_swapChainFormat = wgpu::TextureFormat::Undefined;
	for (size_t i = 0; i < caps.formatCount; ++i)
	{
		const auto format = static_cast<wgpu::TextureFormat>(caps.formats[i]);
		if (format == wgpu::TextureFormat::RGBA8UnormSrgb || format == wgpu::TextureFormat::BGRA8UnormSrgb)
		{
			m_swapChainFormat = format;
			break;
		}
	}

	m_surfaceIsSrgb = (m_swapChainFormat != wgpu::TextureFormat::Undefined);
	if (!m_surfaceIsSrgb && caps.formatCount > 0) // No sRGB surface: fall back; composite encodes gamma.
		m_swapChainFormat = static_cast<wgpu::TextureFormat>(caps.formats[0]);
	// Record non-Fifo present support. Prefer Mailbox (low-latency, no tearing,
	// non-blocking) then Immediate, else Fifo. Dawn's D3D12 surface often ignores
	// Immediate (it then blocks ~vsync -> ~40 FPS), so Mailbox is what uncaps it.
	for (size_t i = 0; i < caps.presentModeCount; ++i)
	{
		const auto m = static_cast<wgpu::PresentMode>(caps.presentModes[i]);
		spdlog::info("[WebGPU] Surface present mode available: {}", presentModeName(m));
		if (m == wgpu::PresentMode::Mailbox)
			m_mailboxSupported = true;
		if (m == wgpu::PresentMode::Immediate)
			m_immediateSupported = true;
		if (m == wgpu::PresentMode::FifoRelaxed)
			m_fifoRelaxedSupported = true;
	}
	caps.freeMembers();

	if (m_swapChainFormat == wgpu::TextureFormat::Undefined)
	{
		spdlog::critical("[WebGPU] Could not determine swap chain format.");
		assert(false);
	}
	spdlog::info("[WebGPU] Surface format {} (sRGB present: {})",
	             static_cast<int>(m_swapChainFormat), m_surfaceIsSrgb);

	m_adapter.release();
	spdlog::info("[WebGPU] Device created successfully.");
}

wgpu::Limits WebGPUContext::getHardwareLimits() const
{
	wgpu::Limits limits{};
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
	currentConfig.presentMode = enableVSync ? wgpu::PresentMode::Fifo
											: (m_mailboxSupported ? wgpu::PresentMode::Mailbox
											: ((m_compatibilityMode && m_fifoRelaxedSupported) ? wgpu::PresentMode::FifoRelaxed
											: (m_immediateSupported ? wgpu::PresentMode::Immediate : wgpu::PresentMode::Fifo)));
	m_surfaceManager->reconfigure(currentConfig);
	spdlog::info("[WebGPU] Present mode updated: vsync: {}, present mode: {}.", enableVSync, presentModeName(currentConfig.presentMode));
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

WebGPUPipelineFactory &WebGPUContext::pipelineFactory()
{
	return pipelineManager().factory();
}

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
