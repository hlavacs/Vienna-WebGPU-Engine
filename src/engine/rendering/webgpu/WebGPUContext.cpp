#include "engine/rendering/webgpu/WebGPUContext.h"

#include <cassert>
#include <iostream>

#include "engine/rendering/Vertex.h"

namespace engine::rendering::webgpu
{

WebGPUContext::WebGPUContext() :
	m_surface(nullptr), m_lastWindowHandle(nullptr) {}

void WebGPUContext::initialize(void *windowHandle, bool enableVSync)
{
	m_surfaceManager = std::make_unique<WebGPUSurfaceManager>(*this);
	m_bufferFactory = std::make_unique<WebGPUBufferFactory>(*this);
	m_meshFactory = std::make_unique<WebGPUMeshFactory>(*this);
	m_textureFactory = std::make_unique<WebGPUTextureFactory>(*this);
	m_materialFactory = std::make_unique<WebGPUMaterialFactory>(*this);
	m_bindGroupFactory = std::make_unique<WebGPUBindGroupFactory>(*this);
	m_pipelineFactory = std::make_unique<WebGPUPipelineFactory>(*this);
	m_samplerFactory = std::make_unique<WebGPUSamplerFactory>(*this);
	// m_swapChainFactory = std::make_unique<WebGPUSwapChainFactory>(*this);
	m_depthTextureFactory = std::make_unique<WebGPUDepthTextureFactory>(*this);
	m_modelFactory = std::make_unique<WebGPUModelFactory>(*this);
	m_depthStencilStateFactory = std::make_unique<WebGPUDepthStencilStateFactory>();
	m_renderPassFactory = std::make_unique<WebGPURenderPassFactory>(*this);
	m_shaderFactory = std::make_unique<WebGPUShaderFactory>(*this);
	m_pipelineManager = std::make_unique<WebGPUPipelineManager>(*this);
	m_lastWindowHandle = windowHandle;
#ifdef __EMSCRIPTEN__
	m_instance = wgpu::wgpuCreateInstance(nullptr);
#else
	m_instance = wgpu::createInstance(wgpu::InstanceDescriptor{});
#endif
	assert(m_instance);
	initSurface(windowHandle);
	initDevice();

	// Initialize ShaderRegistry after device is ready
	m_shaderRegistry = std::make_unique<ShaderRegistry>(*this);
	m_shaderRegistry->initializeDefaultShaders();
	m_textureFactory->initializeMipmapPipeline();

	// ToDo: Move this to the surface manager
	SDL_Window *sdlWindow = static_cast<SDL_Window *>(windowHandle);
	int width, height;
	SDL_GL_GetDrawableSize(sdlWindow, &width, &height);

	WebGPUSurfaceManager::Config config;
	config.format = getSwapChainFormat();
	config.width = width;
	config.height = height;
	config.presentMode = enableVSync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate;
	m_surfaceManager->reconfigure(config);
}

void WebGPUContext::updatePresentMode(bool enableVSync)
{
	if (!m_surfaceManager)
	{
		throw std::runtime_error("Cannot update present mode: WebGPUSurfaceManager not initialized!");
	}

	// Get current config and update only the present mode
	auto currentConfig = m_surfaceManager->currentConfig();
	currentConfig.presentMode = enableVSync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate;

	// Reconfigure the surface with updated present mode
	m_surfaceManager->reconfigure(currentConfig);
}

void WebGPUContext::initSurface(void *windowHandle)
{
	if (m_surface)
		return;

	SDL_Window *sdlWindow = static_cast<SDL_Window *>(windowHandle);
	m_surface = wgpu::Surface(SDL_GetWGPUSurface(m_instance, sdlWindow));

	assert(m_surface);
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

void WebGPUContext::initDevice()
{
	// --------------- Request adapter ---------------
	wgpu::RequestAdapterOptions adapterOpts{};
	adapterOpts.compatibleSurface = m_surface;
	m_adapter = m_instance.requestAdapter(adapterOpts);
	assert(m_adapter);

	// --------------- Query supported limits ---------------
	wgpu::SupportedLimits supportedLimits;
#ifdef WEBGPU_BACKEND_WGPU
	m_adapter.getLimits(&supportedLimits);
#else
	// Fallback for non-WGPU backends
	supportedLimits.limits.minStorageBufferOffsetAlignment = 256;
	supportedLimits.limits.minUniformBufferOffsetAlignment = 256;
#endif

	// --------------- Set required limits ---------------
	wgpu::RequiredLimits requiredLimits = wgpu::Default;

	// Typical defaults: try to be practical but still safe
	requiredLimits.limits.maxVertexAttributes = std::min(16u, supportedLimits.limits.maxVertexAttributes);
	requiredLimits.limits.maxVertexBuffers = std::min(8u, supportedLimits.limits.maxVertexBuffers);
	requiredLimits.limits.maxBufferSize = std::min(67108864ull, supportedLimits.limits.maxBufferSize); // 64 MB
	requiredLimits.limits.maxVertexBufferArrayStride = 256;											   // safe stride
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
#ifdef WEBGPU_BACKEND_WGPU
	requiredLimits.limits.maxInterStageShaderComponents = std::min(60u, supportedLimits.limits.maxInterStageShaderComponents);
#else
	requiredLimits.limits.maxInterStageShaderComponents = 0xffffffffu;
#endif
	requiredLimits.limits.maxBindGroups = std::min(8u, supportedLimits.limits.maxBindGroups);
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 8;	   // more flexible than 2
	requiredLimits.limits.maxUniformBufferBindingSize = 64 * 1024; // 64 KB, safe default
	requiredLimits.limits.maxTextureDimension1D = 8192;
	requiredLimits.limits.maxTextureDimension2D = 8192;
	requiredLimits.limits.maxTextureArrayLayers = 256;
	requiredLimits.limits.maxSampledTexturesPerShaderStage = 16;
	requiredLimits.limits.maxSamplersPerShaderStage = 16;
	requiredLimits.limits.maxBindingsPerBindGroup = 16;
	// Storage buffers
	requiredLimits.limits.maxStorageBuffersPerShaderStage = 4;
	requiredLimits.limits.maxStorageBufferBindingSize = 16 * 1024 * 1024; // 16 MB (WebGPU spec minimum guaranteed)

	// --------------- Request device ---------------
	wgpu::DeviceDescriptor deviceDesc{};
	deviceDesc.label = "WebGPUContext Device";
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.defaultQueue.label = "Default Queue";
	m_device = m_adapter.requestDevice(deviceDesc);
	assert(m_device);

	// --------------- Add error callback for debug info ---------------
	static std::unique_ptr<wgpu::ErrorCallback> m_errorCallback;
	m_errorCallback = m_device.setUncapturedErrorCallback([](wgpu::ErrorType type, char const *message)
														  {
        std::cerr << "[WebGPU Error] Type " << static_cast<int>(type);
        if (message) std::cerr << ": " << message;
        std::cerr << std::endl; });

	// --------------- Get queue ---------------
	m_queue = m_device.getQueue();
	assert(m_queue);

	// --------------- Query preferred swapchain format ---------------
#ifdef WEBGPU_BACKEND_WGPU
	m_swapChainFormat = m_surface.getPreferredFormat(m_adapter);
#else
	m_swapChainFormat = TextureFormat::BGRA8Unorm;
#endif
	assert(m_swapChainFormat != wgpu::TextureFormat::Undefined);

	m_adapter.release(); // Release adapter after device creation

	// --------------- Create default sampler ---------------
	m_defaultSampler = samplerFactory().createDefaultSampler();
	assert(m_defaultSampler);
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

WebGPUPipelineFactory &WebGPUContext::pipelineFactory()
{
	if (!m_pipelineFactory)
	{
		throw std::runtime_error("WebGPUPipelineFactory not initialized!");
	}
	return *m_pipelineFactory;
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

} // namespace engine::rendering::webgpu
}
