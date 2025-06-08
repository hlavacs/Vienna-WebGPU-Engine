#include "engine/rendering/webgpu/WebGPUContext.h"
#include <iostream>
#include <cassert>

namespace engine::rendering::webgpu
{

    WebGPUContext::WebGPUContext() = default;

    void WebGPUContext::initialize(void *windowHandle)
    {
        // --------------- Create instance ---------------
        wgpu::InstanceDescriptor instanceDesc{};
        m_instance = wgpu::CreateInstance(instanceDesc);
        assert(m_instance);

        // TODO: Support other Windows backends in the future
        SDL_Window *sdlWindow = static_cast<SDL_Window *>(windowHandle);

        // --------------- Create surface ---------------
        m_surface = wgpu::Surface(SDL_GetWGPUSurface(m_instance.Get(), sdlWindow));
        assert(m_surface);

        // --------------- Initialize device ---------------
        initDevice();

        // --------------- Initialize factories ---------------
        m_meshFactory = std::make_unique<WebGPUMeshFactory>(*this);
        m_textureFactory = std::make_unique<WebGPUTextureFactory>(*this);
        m_materialFactory = std::make_unique<WebGPUMaterialFactory>(*this);
    }

    void WebGPUContext::initDevice()
    {
        // --------------- Request adapter ---------------
        wgpu::RequestAdapterOptions adapterOpts{};
        adapterOpts.compatibleSurface = m_surface;
        m_adapter = m_instance.RequestAdapter(adapterOpts);
        assert(m_adapter);

        // --------------- Request device ---------------
        wgpu::DeviceDescriptor deviceDesc{};
        deviceDesc.label = "WebGPUContext Device";
        m_device = m_adapter.RequestDevice(deviceDesc);
        assert(m_device);

        // --------------- Get queue ---------------
        m_queue = m_device.GetQueue();
        assert(m_queue);

        // --------------- Query preferred swapchain format ---------------
        m_swapChainFormat = m_surface.GetPreferredFormat(m_adapter);
        assert(m_swapChainFormat != wgpu::TextureFormat::Undefined);

        // --------------- Create default sampler ---------------
        wgpu::SamplerDescriptor samplerDesc{};
        samplerDesc.label = "Default Sampler";
        m_defaultSampler = m_device.CreateSampler(&samplerDesc);
        assert(m_defaultSampler);
    }

    WebGPUMeshFactory &WebGPUContext::meshFactory()
    {
        return *m_meshFactory;
    }
    WebGPUTextureFactory &WebGPUContext::textureFactory()
    {
        return *m_textureFactory;
    }
    WebGPUMaterialFactory &WebGPUContext::materialFactory()
    {
        return *m_materialFactory;
    }

    wgpu::Device WebGPUContext::getDevice() const { return m_device; }
    wgpu::Queue WebGPUContext::getQueue() const { return m_queue; }
    wgpu::TextureFormat WebGPUContext::getSwapChainFormat() const { return m_swapChainFormat; }
    wgpu::Sampler WebGPUContext::getDefaultSampler() const { return m_defaultSampler; }

    wgpu::Buffer WebGPUContext::createBuffer(const wgpu::BufferDescriptor &desc) const
    {
        return m_device.CreateBuffer(&desc);
    }

    wgpu::Buffer WebGPUContext::createBufferWithData(const void *data, size_t size, wgpu::BufferUsage usage) const
    {
        // --------------- Create buffer descriptor ---------------
        wgpu::BufferDescriptor desc{};
        desc.size = size;
        desc.usage = usage | wgpu::BufferUsage::CopyDst;
        desc.mappedAtCreation = true;

        // --------------- Create buffer ---------------
        wgpu::Buffer buffer = m_device.CreateBuffer(&desc);

        // --------------- Copy data if available ---------------
        if (size > 0 && data)
        {
            void *mapped = buffer.GetMappedRange();
            memcpy(mapped, data, size);
            buffer.Unmap();
        }
        return buffer;
    }

    wgpu::Texture WebGPUContext::createTexture(const wgpu::TextureDescriptor &desc) const
    {
        // --------------- Create texture ---------------
        return m_device.CreateTexture(&desc);
    }

} // namespace engine::rendering::webgpu
