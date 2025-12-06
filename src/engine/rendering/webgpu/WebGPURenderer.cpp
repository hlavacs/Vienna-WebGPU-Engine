#include "engine/rendering/webgpu/WebGPURenderer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPURenderer::WebGPURenderer(WebGPUContext *context) 
	: Renderer(std::shared_ptr<WebGPUContext>(context, [](WebGPUContext*){})), // Non-owning shared_ptr
	  m_context(context)
{
}

WebGPURenderer::~WebGPURenderer()
{
	shutdown();
}

void WebGPURenderer::initialize()
{
	// Allocate frame and lights buffers
}

void WebGPURenderer::beginFrame(const engine::scene::CameraNode &camera)
{
	updateFrameUniforms(camera);
}

void WebGPURenderer::renderScene(const engine::rendering::RenderCollector &collector)
{
	updateLights(collector.getLights());

	// Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Main Command Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// Create render pass descriptor (simplified, assumes one color/depth target)
	wgpu::RenderPassColorAttachment colorAttachment{};
	// colorAttachment.view = m_context->getCurrentTextureView();
	colorAttachment.loadOp = wgpu::LoadOp::Clear;
	colorAttachment.storeOp = wgpu::StoreOp::Store;
	colorAttachment.clearValue = {0.05f, 0.05f, 0.05f, 1.0f};

	wgpu::RenderPassDescriptor passDesc{};
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;
	passDesc.depthStencilAttachment = nullptr; // Add depth if needed

	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(passDesc);

	for (const auto &item : collector.getRenderItems())
	{
		auto modelHandle = item.model;
		std::shared_ptr<WebGPUModel> gpuModel;
		auto it = m_modelCache.find(modelHandle);
		if (it != m_modelCache.end()) {
			gpuModel = it->second;
		} else {
			gpuModel = m_context->modelFactory().createFromHandle(modelHandle);
			if (gpuModel) {
				m_modelCache[modelHandle] = gpuModel;
			}
		}
		if (gpuModel) {
			gpuModel->update();
			// gpuModel->render(encoder, renderPass, item.transform);
		}
	}

	renderPass.end();
	m_context->getQueue().submit(1, &encoder.finish());
}

void WebGPURenderer::submitFrame()
{
	// Submit commands and present
}

void WebGPURenderer::shutdown()
{
	// Release GPU resources
}

void WebGPURenderer::updateFrameUniforms(const engine::scene::CameraNode &camera)
{
	// Fill m_frameUniforms and write to m_frameUniformBuffer
}

void WebGPURenderer::updateLights(const std::vector<engine::rendering::LightStruct> &lights)
{
	// Write lights buffer to GPU
}



} // namespace engine::rendering::webgpu
