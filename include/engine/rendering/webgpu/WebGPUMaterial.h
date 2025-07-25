#pragma once
#include <webgpu/webgpu.hpp>
#include <memory>

#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"
namespace engine::rendering::webgpu
{
	class WebGPUTexture;

	/**
	 * @brief Options for configuring a WebGPUMaterial.
	 */
	struct WebGPUMaterialOptions
	{
	};

	/**
	 * @class WebGPUMaterial
	 * @brief GPU-side material: wraps bind groups and layout setup, keeps a copy of the original Material.
	 */
	class WebGPUMaterial : public WebGPURenderObject
	{
	public:
		/**
		 * @brief Construct a WebGPUMaterial from a Material handle and bind group.
		 * @param context The WebGPU context.
		 * @param materialHandle Handle to the CPU-side Material.
		 * @param bindGroup The GPU-side bind group for this material.
		 * @param options Optional material options.
		 */
		WebGPUMaterial(WebGPUContext& context, const engine::rendering::Material::Handle& materialHandle, wgpu::BindGroup bindGroup, WebGPUMaterialOptions options = {});

		/**
		 * @brief Get the GPU-side bind group for this material.
		 * @return The WebGPU bind group.
		 */
		wgpu::BindGroup getBindGroup() const;

		/**
		 * @brief Get the original CPU-side Material.
		 * @return Reference to the Material.
		 */
		const engine::rendering::Material& getMaterial() const;

		/**
		 * @brief Get the material options used for this WebGPUMaterial.
		 * @return Reference to the options struct.
		 */
		const WebGPUMaterialOptions& getOptions() const;

		/**
		 * @brief Set the material options for this WebGPUMaterial.
		 * @param opts The options to set.
		 */
		void setOptions(const WebGPUMaterialOptions& opts);

	private:
		/**
		 * @brief Copy of the original Material used to create this WebGPUMaterial.
		 */
		engine::rendering::Material::Handle m_materialHandle;
		/**
		 * @brief The GPU-side bind group for this material.
		 */
		wgpu::BindGroup m_bindGroup;
		/**
		 * @brief Options used for configuring this WebGPUMaterial.
		 */
		WebGPUMaterialOptions m_options;
	};

} // namespace engine::rendering::webgpu
