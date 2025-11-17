#include "engine/rendering/webgpu/WebGPURenderPassFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPURenderPassFactory::WebGPURenderPassFactory(WebGPUContext &context) : m_context(context) {}

std::shared_ptr<WebGPURenderPassContext> WebGPURenderPassFactory::createDefault(
	const std::shared_ptr<WebGPUTexture> &colorTexture,
	const std::shared_ptr<WebGPUDepthTexture> &depthTexture
)
{
	wgpu::RenderPassColorAttachment renderPassColorAttachment{};
	renderPassColorAttachment.view = colorTexture ? colorTexture->getTextureView() : nullptr;
	renderPassColorAttachment.resolveTarget = nullptr;
#ifdef __EMSCRIPTEN__
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
	renderPassColorAttachment.loadOp = wgpu::LoadOp::Clear;
	renderPassColorAttachment.storeOp = wgpu::StoreOp::Store;
	renderPassColorAttachment.clearValue = wgpu::Color{0.05, 0.05, 0.05, 1.0};

	wgpu::RenderPassDepthStencilAttachment depthStencilAttachment;
	depthStencilAttachment.view = depthTexture ? depthTexture->getTextureView() : nullptr;
	depthStencilAttachment.depthClearValue = 1.0f;
	depthStencilAttachment.depthLoadOp = wgpu::LoadOp::Clear;
	depthStencilAttachment.depthStoreOp = wgpu::StoreOp::Store;
	depthStencilAttachment.depthReadOnly = false;
	depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
	depthStencilAttachment.stencilLoadOp = wgpu::LoadOp::Clear;
	depthStencilAttachment.stencilStoreOp = wgpu::StoreOp::Store;
#else
	depthStencilAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
	depthStencilAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
#endif
	depthStencilAttachment.stencilReadOnly = true;

	wgpu::RenderPassDescriptor renderPassDesc{};
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;
	renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

	return std::make_shared<WebGPURenderPassContext>(
		std::vector<std::shared_ptr<WebGPUTexture>>{colorTexture},
		depthTexture,
		renderPassDesc
	);
}

std::shared_ptr<WebGPURenderPassContext> WebGPURenderPassFactory::createCustom(
	const std::vector<std::shared_ptr<WebGPUTexture>> &colorTextures,
	const std::shared_ptr<WebGPUDepthTexture> &depthTexture,
	const wgpu::RenderPassDescriptor &descriptor
)
{
	// Assert that colorTextures size matches descriptor.colorAttachmentCount
	assert(colorTextures.size() == descriptor.colorAttachmentCount && "Color texture count must match descriptor's colorAttachmentCount");

	// Copy and update color attachments
	std::vector<wgpu::RenderPassColorAttachment> colorAttachmentCopies(descriptor.colorAttachmentCount);
	for (size_t i = 0; i < descriptor.colorAttachmentCount; ++i)
	{
		colorAttachmentCopies[i] = descriptor.colorAttachments[i];
		if (i < colorTextures.size() && colorTextures[i])
			colorAttachmentCopies[i].view = colorTextures[i]->getTextureView();
	}

	wgpu::RenderPassDescriptor descCopy = descriptor;
	descCopy.colorAttachments = colorAttachmentCopies.data();

	// Copy and update depth attachment if present
	std::optional<wgpu::RenderPassDepthStencilAttachment> depthAttachmentCopy;
	if (descriptor.depthStencilAttachment)
	{
		depthAttachmentCopy = *descriptor.depthStencilAttachment;
		if (depthTexture)
			depthAttachmentCopy->view = depthTexture->getTextureView();
		descCopy.depthStencilAttachment = &(*depthAttachmentCopy);
	}

	return std::make_shared<WebGPURenderPassContext>(
		colorTextures,
		depthTexture,
		descCopy
	);
}

} // namespace engine::rendering::webgpu
