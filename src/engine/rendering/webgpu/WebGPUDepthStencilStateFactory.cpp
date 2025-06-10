#include "engine/rendering/webgpu/WebGPUDepthStencilStateFactory.h"
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
	using namespace wgpu;

	WebGPUDepthStencilStateFactory::WebGPUDepthStencilStateFactory() = default;

	wgpu::DepthStencilState WebGPUDepthStencilStateFactory::createDefault(wgpu::TextureFormat format, bool enableDepth)
	{
		DepthStencilState depthStencilState = Default;
		depthStencilState.format = format;
		depthStencilState.depthWriteEnabled = enableDepth;
		depthStencilState.depthCompare = CompareFunction::Less;
		depthStencilState.stencilFront.compare = CompareFunction::Always;
		depthStencilState.stencilFront.depthFailOp = StencilOperation::Keep;
		depthStencilState.stencilFront.failOp = StencilOperation::Keep;
		depthStencilState.stencilFront.passOp = StencilOperation::Keep;
		depthStencilState.stencilBack.compare = CompareFunction::Always;
		depthStencilState.stencilBack.depthFailOp = StencilOperation::Keep;
		depthStencilState.stencilBack.failOp = StencilOperation::Keep;
		depthStencilState.stencilBack.passOp = StencilOperation::Keep;
		depthStencilState.stencilReadMask = 0;
		depthStencilState.stencilWriteMask = 0;
		return depthStencilState;
	}

	wgpu::DepthStencilState WebGPUDepthStencilStateFactory::create(
		wgpu::TextureFormat format,
		bool depthWriteEnabled,
		wgpu::CompareFunction depthCompare,
		uint32_t stencilReadMask,
		uint32_t stencilWriteMask,
		wgpu::StencilFaceState stencilFront,
		wgpu::StencilFaceState stencilBack)
	{
		DepthStencilState depthStencilState = Default;
		depthStencilState.format = format;
		depthStencilState.depthWriteEnabled = depthWriteEnabled;
		depthStencilState.depthCompare = depthCompare;
		depthStencilState.stencilReadMask = stencilReadMask;
		depthStencilState.stencilWriteMask = stencilWriteMask;
		depthStencilState.stencilFront = stencilFront;
		depthStencilState.stencilBack = stencilBack;
		return depthStencilState;
	}

} // namespace engine::rendering::webgpu
