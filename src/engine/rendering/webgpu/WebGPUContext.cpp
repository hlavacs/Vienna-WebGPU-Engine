#include "engine/rendering/webgpu/WebGPUContext.h"

#include <cassert>
#include <iostream>

#include "engine/rendering/Vertex.h"

namespace engine::rendering::webgpu
{

WebGPUContext::WebGPUContext() :
	m_surface(nullptr), m_lastWindowHandle(nullptr) {}

void WebGPUContext::initialize(void *windowHandle)
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
	m_lastWindowHandle = windowHandle;
#ifdef __EMSCRIPTEN__
	m_instance = wgpu::wgpuCreateInstance(nullptr);
#else
	m_instance = wgpu::createInstance(wgpu::InstanceDescriptor{});
#endif
	assert(m_instance);
	initSurface(windowHandle);
	initDevice();

	// ToDo: Move this to the surface manager
	SDL_Window *sdlWindow = static_cast<SDL_Window *>(windowHandle);
	int width, height;
	SDL_GL_GetDrawableSize(sdlWindow, &width, &height);

	WebGPUSurfaceManager::Config config;
	config.format = getSwapChainFormat();
	config.width = width;
	config.height = height;
	config.presentMode = wgpu::PresentMode::Fifo;
	m_surfaceManager->reconfigure(config);

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
	supportedLimits.limits.minStorageBufferOffsetAlignment = 256;
	supportedLimits.limits.minUniformBufferOffsetAlignment = 256;
#endif

	// --------------- Set required limits ---------------
	wgpu::RequiredLimits requiredLimits = wgpu::Default;

	// Example overrides (adjust as needed for your engine):
	requiredLimits.limits.maxVertexAttributes = 6;
	requiredLimits.limits.maxVertexBuffers = 1;
	requiredLimits.limits.maxBufferSize = 150000 * sizeof(engine::rendering::Vertex);
	requiredLimits.limits.maxVertexBufferArrayStride = sizeof(engine::rendering::Vertex);
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
#ifdef WEBGPU_BACKEND_WGPU
	requiredLimits.limits.maxInterStageShaderComponents = 17;
	//                                                    ^^ This was a 11
#else
	requiredLimits.limits.maxInterStageShaderComponents = 0xffffffffu; // undefined
#endif
	requiredLimits.limits.maxBindGroups = 4;
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 2;
	requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
	requiredLimits.limits.maxTextureDimension1D = 2048;
	requiredLimits.limits.maxTextureDimension2D = 2048;
	requiredLimits.limits.maxTextureArrayLayers = 1;
	requiredLimits.limits.maxSampledTexturesPerShaderStage = 2;
	requiredLimits.limits.maxSamplersPerShaderStage = 1;
	requiredLimits.limits.maxBindGroupsPlusVertexBuffers = 2;
	requiredLimits.limits.maxBindingsPerBindGroup = 5;
	// Add required limits for storage buffers
	requiredLimits.limits.maxStorageBuffersPerShaderStage = 1;
	requiredLimits.limits.maxStorageBufferBindingSize = 4096; // Set to accommodate our lights buffer (at least 16 + num_lights*76 bytes)

	// --------------- Request device ---------------
	wgpu::DeviceDescriptor deviceDesc{};
	deviceDesc.label = "WebGPUContext Device";
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.defaultQueue.label = "Default Queue";
	m_device = m_adapter.requestDevice(deviceDesc);
	assert(m_device);

	// --------------- Add error callback for debug info ---------------
	// In your Application or wherever you own m_device:
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

wgpu::Texture WebGPUContext::createTexture(const wgpu::TextureDescriptor &desc)
{
	// --------------- Create texture ---------------
	return m_device.createTexture(desc);
}

} // namespace engine::rendering::webgpu
