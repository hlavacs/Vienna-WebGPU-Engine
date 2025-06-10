#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include <iostream>
#include <cassert>

namespace engine::rendering::webgpu
{

	WebGPUContext::WebGPUContext() : m_surface(nullptr), m_lastWindowHandle(nullptr) {}

	void WebGPUContext::initialize(void *windowHandle)
	{
		m_lastWindowHandle = windowHandle;
#ifdef __EMSCRIPTEN__
		m_instance = wgpu::wgpuCreateInstance(nullptr);
#else
		m_instance = wgpu::createInstance(wgpu::InstanceDescriptor{});
#endif
		assert(m_instance);
		initSurface(windowHandle);
		initDevice();
		m_meshFactory = std::make_unique<WebGPUMeshFactory>(*this);
		m_textureFactory = std::make_unique<WebGPUTextureFactory>(*this);
		m_materialFactory = std::make_unique<WebGPUMaterialFactory>(*this);
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
		requiredLimits.limits.maxBindGroups = 2;
		requiredLimits.limits.maxUniformBuffersPerShaderStage = 2;
		requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
		requiredLimits.limits.maxTextureDimension1D = 2048;
		requiredLimits.limits.maxTextureDimension2D = 2048;
		requiredLimits.limits.maxTextureArrayLayers = 1;
		requiredLimits.limits.maxSampledTexturesPerShaderStage = 2;
		requiredLimits.limits.maxSamplersPerShaderStage = 1;
		requiredLimits.limits.maxBindGroupsPlusVertexBuffers = 2;
		requiredLimits.limits.maxBindingsPerBindGroup = 5;

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
		WebGPUSamplerFactory samplerFactory(*this);
		wgpu::SamplerDescriptor samplerDesc{};
		samplerDesc.label = "Default Sampler";
		m_defaultSampler = samplerFactory.createDefaultSampler();
		assert(m_defaultSampler);
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

	wgpu::Buffer WebGPUContext::createBuffer(const wgpu::BufferDescriptor &desc)
	{
		return m_device.createBuffer(desc);
	}

	wgpu::Buffer WebGPUContext::createBufferWithData(const void *data, size_t size, wgpu::BufferUsage usage)
	{
		// --------------- Create buffer descriptor ---------------
		wgpu::BufferDescriptor desc;
		desc.size = size;
		desc.usage = usage | wgpu::BufferUsage::CopyDst;
		desc.mappedAtCreation = true;

		// --------------- Create buffer ---------------
		wgpu::Buffer buffer = m_device.createBuffer(desc);

		// --------------- Copy data if available ---------------
		if (size > 0 && data)
		{
			void *mapped = buffer.getMappedRange(0, size);
			memcpy(mapped, data, size);
			buffer.unmap();
		}
		return buffer;
	}

	wgpu::Texture WebGPUContext::createTexture(const wgpu::TextureDescriptor &desc)
	{
		// --------------- Create texture ---------------
		return m_device.createTexture(desc);
	}

} // namespace engine::rendering::webgpu
