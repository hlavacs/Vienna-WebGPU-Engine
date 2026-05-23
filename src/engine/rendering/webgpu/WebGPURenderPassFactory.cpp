#include <memory>

#include "engine/rendering/webgpu/WebGPURenderPassFactory.h"

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPURenderPassFactory::WebGPURenderPassFactory(WebGPUContext &context) : m_context(context) {}

std::shared_ptr<WebGPURenderPassContext> WebGPURenderPassFactory::create(
	const std::shared_ptr<WebGPUTexture> &colorTexture,
	const std::shared_ptr<WebGPUTexture> &depthTexture,
	engine::rendering::ClearFlags clearFlags,
	const glm::vec4 &backgroundColor,
	int colorTextureLayer,
	int depthTextureLayer
)
{

	wgpu::RenderPassDescriptor renderPassDesc{};
	renderPassDesc.colorAttachmentCount = 0;
	wgpu::RenderPassColorAttachment colorAttachment{};
	wgpu::RenderPassDepthStencilAttachment depthAttachment;
	if (colorTexture)
	{
		colorAttachment.view = colorTexture->getTextureView(colorTextureLayer);
		colorAttachment.resolveTarget = nullptr;
#ifdef __EMSCRIPTEN__
		colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
		colorAttachment.loadOp = hasFlag(clearFlags, ClearFlags::SolidColor) ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		colorAttachment.storeOp = wgpu::StoreOp::Store;
		colorAttachment.clearValue = wgpu::Color{backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a};
		renderPassDesc.colorAttachmentCount = 1;
		renderPassDesc.colorAttachments = &colorAttachment;
	}
	// Only create depth attachment if depth texture is provided
	if (depthTexture)
	{
		depthAttachment.view = depthTexture->getTextureView(depthTextureLayer);
		depthAttachment.depthClearValue = 1.0f;
		depthAttachment.depthLoadOp = hasFlag(clearFlags, ClearFlags::Depth) ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
		depthAttachment.depthReadOnly = false;
		depthAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
		depthAttachment.stencilLoadOp = wgpu::LoadOp::Clear;
		depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;
#else
		depthAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
		depthAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
#endif
		depthAttachment.stencilReadOnly = true;
		renderPassDesc.depthStencilAttachment = &depthAttachment;
	}

	return std::make_shared<WebGPURenderPassContext>(
		std::vector<std::shared_ptr<WebGPUTexture>>{colorTexture},
		depthTexture,
		renderPassDesc
	);
}

std::shared_ptr<WebGPURenderPassContext> WebGPURenderPassFactory::createDepthOnly(
	std::shared_ptr<WebGPUTexture> depthTexture,
	int arrayLayer,
	bool clearDepth,
	float clearValue
)
{
	wgpu::RenderPassDepthStencilAttachment depthAttachment{};
	depthAttachment.view = depthTexture->getTextureView(arrayLayer);
	depthAttachment.depthLoadOp = clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
	depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
	depthAttachment.depthClearValue = clearValue;
	depthAttachment.depthReadOnly = false;
	depthAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
	depthAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
	depthAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
#else
	depthAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
	depthAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
#endif
	depthAttachment.stencilReadOnly = true;

	wgpu::RenderPassDescriptor renderPassDesc{};
	renderPassDesc.colorAttachmentCount = 0;
	renderPassDesc.colorAttachments = nullptr;
	renderPassDesc.depthStencilAttachment = &depthAttachment;

	// No color textures for depth-only pass
	return std::make_shared<WebGPURenderPassContext>(
		std::vector<std::shared_ptr<WebGPUTexture>>{},
		nullptr,
		renderPassDesc
	);
}

std::shared_ptr<WebGPURenderPassContext> WebGPURenderPassFactory::createCustom(
	const std::vector<std::shared_ptr<WebGPUTexture>> &colorTextures,
	const std::shared_ptr<WebGPUTexture> &depthTexture,
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

std::shared_ptr<WebGPURenderPassContext> WebGPURenderPassFactory::createMultiTarget(
	const char *label,
	const std::vector<std::shared_ptr<WebGPUTexture>> &colorTextures,
	const std::shared_ptr<WebGPUTexture> &depthTexture,
	const glm::vec4 &clearColor,
	float depthClear
)
{
	assert(!colorTextures.empty() && "createMultiTarget needs at least one color attachment");

	std::vector<wgpu::RenderPassColorAttachment> colorAttachments(colorTextures.size());
	for (size_t i = 0; i < colorTextures.size(); ++i)
	{
		assert(colorTextures[i] && "Null color texture passed to createMultiTarget");
		auto &att = colorAttachments[i];
		att.view = colorTextures[i]->getTextureView();
		att.loadOp = wgpu::LoadOp::Clear;
		att.storeOp = wgpu::StoreOp::Store;
		att.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};
	}

	wgpu::RenderPassDepthStencilAttachment depthAttachment{};
	if (depthTexture)
	{
		depthAttachment.view = depthTexture->getTextureView();
		depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
		depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
		depthAttachment.depthClearValue = depthClear;
		depthAttachment.depthReadOnly = false;
		// Stencil aspect is unused for the depth-only formats we expect here.
		depthAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
		depthAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
		depthAttachment.stencilReadOnly = true;
	}

	wgpu::RenderPassDescriptor desc{};
	desc.label = label;
	desc.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
	desc.colorAttachments = colorAttachments.data();
	desc.depthStencilAttachment = depthTexture ? &depthAttachment : nullptr;

	// createCustom() copies both the descriptor and the attachment array
	// into the context's internal storage, so it is safe to let the locals
	// here go out of scope on return.
	return createCustom(colorTextures, depthTexture, desc);
}

} // namespace engine::rendering::webgpu
