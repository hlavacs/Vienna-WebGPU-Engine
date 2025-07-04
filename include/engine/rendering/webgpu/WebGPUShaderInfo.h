#pragma once
#include <webgpu/webgpu.hpp>
#include <string>

namespace engine::rendering::webgpu
{

	struct WebGPUShaderInfo
	{
		wgpu::ShaderModule module;
		std::string entryPoint;

		WebGPUShaderInfo() = default;
		WebGPUShaderInfo(wgpu::ShaderModule mod, const std::string &entry)
			: module(mod), entryPoint(entry) {}
	};

} // namespace engine::rendering::webgpu
