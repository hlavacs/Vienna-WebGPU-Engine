#include "engine/rendering/RenderPass.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

#include <spdlog/spdlog.h>

namespace engine::rendering
{

std::shared_ptr<webgpu::WebGPUShaderInfo> RenderPass::getValidatedShader(std::string_view shaderName) const
{
	std::string name(shaderName);
	auto shader = m_context->shaderRegistry().getShader(name);
	if (!shader || !shader->isValid())
	{
		spdlog::error("{}: shader '{}' is missing or invalid", this->name(), name);
		return nullptr;
	}
	return shader;
}

bool RenderPass::bind(
	wgpu::RenderPassEncoder renderPass,
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &webgpuShaderInfo,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &bindgroup
)
{
	auto bindGroupIndexOpt = webgpuShaderInfo->getBindGroupIndex(bindgroup->getLayoutInfo()->getName());
	if (!bindGroupIndexOpt.has_value())
		return false;

	renderPass.setBindGroup(static_cast<uint32_t>(bindGroupIndexOpt.value()), bindgroup->getBindGroup(), 0, nullptr);
	return true;
}
} // namespace engine::rendering