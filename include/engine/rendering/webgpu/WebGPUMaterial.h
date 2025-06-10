#pragma once
/**
 * @file WebGPUMaterial.h
 * @brief GPU-side material: wraps bind groups and layout setup.
 */
#include <webgpu/webgpu.hpp>
#include <memory>

namespace engine::rendering::webgpu
{

	class WebGPUTexture;

	struct WebGPUMaterialOptions
	{
		wgpu::BindGroupLayout bindGroupLayout = nullptr;
		wgpu::RenderPipeline pipeline = nullptr;
		wgpu::ShaderModule vertexShader = nullptr;
		wgpu::ShaderModule fragmentShader = nullptr;
	};

	/**
	 * @class WebGPUMaterial
	 * @brief Creates bind groups for a Material, referencing WebGPUTexture.
	 */
	class WebGPUMaterial
	{
	public:
		WebGPUMaterial(wgpu::BindGroup bindGroup, WebGPUMaterialOptions options = {});
		/** @brief Get the bind group. */
		wgpu::BindGroup getBindGroup() const;
		const WebGPUMaterialOptions &getOptions() const;
		void setOptions(const WebGPUMaterialOptions &opts);

	private:
		wgpu::BindGroup m_bindGroup;
		WebGPUMaterialOptions m_options;
	};

} // namespace engine::rendering::webgpu
