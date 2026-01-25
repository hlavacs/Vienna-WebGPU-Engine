#include "engine/rendering/RenderPass.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{
bool RenderPass::bind(
	wgpu::RenderPassEncoder renderPass,
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &webgpuShaderInfo,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &bindgroup
)
{
	auto bindGroupIndexOpt = webgpuShaderInfo->getBindGroupIndex(bindgroup->getLayoutInfo()->getName());
	if (!bindGroupIndexOpt.has_value())
		return false;

	renderPass.setBindGroup(bindGroupIndexOpt.value(), bindgroup->getBindGroup(), 0, nullptr);
	return true;
}
} // namespace engine::rendering