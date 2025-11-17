#include "engine/rendering/webgpu/WebGPUDepthTexture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu {

bool WebGPUDepthTexture::resize(WebGPUContext &context, uint32_t newWidth, uint32_t newHeight)
{
    // No resize needed
    if (matches(newWidth, newHeight, getFormat()))
        return false;

    // Prepare new descriptors
    wgpu::TextureDescriptor newTexDesc = m_textureDesc;
    newTexDesc.size.width = newWidth;
    newTexDesc.size.height = newHeight;
    if (newTexDesc.format == wgpu::TextureFormat::Undefined)
    {
        return false;
    }

    // Create new texture
    wgpu::Texture newTexture = context.createTexture(newTexDesc);
    if (!newTexture)
    {
        return false;
    }

    // Prepare view descriptor
    wgpu::TextureViewDescriptor newViewDesc = m_viewDesc;
    if (newViewDesc.format == wgpu::TextureFormat::Undefined)
    {
        newViewDesc.dimension = wgpu::TextureViewDimension::_2D;
        newViewDesc.aspect = wgpu::TextureAspect::DepthOnly;
        newViewDesc.baseMipLevel = 0;
        newViewDesc.mipLevelCount = 1;
        newViewDesc.baseArrayLayer = 0;
        newViewDesc.arrayLayerCount = 1;
    }
    newViewDesc.format = newTexDesc.format;

    // Create new view
    wgpu::TextureView newView = newTexture.createView(newViewDesc);
    if (!newView)
    {
        std::cerr << "[WebGPUDepthTexture] Failed to create texture view.\n";
        return false;
    }

    // Everything succeeded, release old resources and update members
    if (m_textureView)
        m_textureView.release();
    if (m_texture)
        m_texture.release();

    m_texture = newTexture;
    m_textureView = newView;
    m_textureDesc = newTexDesc;
    m_viewDesc = newViewDesc;

    // Assert to make sure everything is valid
    assert(m_texture);
    assert(m_textureView);

    return true;
}



} // namespace engine::rendering::webgpu
