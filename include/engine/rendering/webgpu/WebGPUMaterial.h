#pragma once
#include <webgpu/webgpu.hpp>
#include <memory>
#include <vector>

#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"

namespace engine::rendering::webgpu
{
	class WebGPUTexture;

	/**
	 * @brief Options for  a WebGPUMaterial.
	 */
	struct WebGPUMaterialOptions
	{
	};

	/**
	 * @brief Container for material textures with named properties
	 */
	struct WebGPUMaterialTextures
	{
		std::shared_ptr<WebGPUTexture> albedo;
		std::shared_ptr<WebGPUTexture> normal;
		std::shared_ptr<WebGPUTexture> metallic;
		std::shared_ptr<WebGPUTexture> roughness;
		std::shared_ptr<WebGPUTexture> ao;
		std::shared_ptr<WebGPUTexture> emissive;
	};

	/**
	 * @class WebGPUMaterial
	 * @brief GPU-side material: wraps bind groups and layout setup, maintains a handle to the CPU-side Material.
	 */
	class WebGPUMaterial : public WebGPURenderObject<engine::rendering::Material>
	{
	public:
		/**
		 * @brief Construct a WebGPUMaterial from a Material handle and resources.
		 * @param context The WebGPU context.
		 * @param materialHandle Handle to the CPU-side Material.
		 * @param bindGroup The GPU-side bind group for this material.
		 * @param propertiesBuffer Buffer containing material properties.
		 * @param textures Textures used by the material.
		 * @param options Optional material options.
		 */
		WebGPUMaterial(
			WebGPUContext &context,
			const engine::rendering::Material::Handle &materialHandle,
			wgpu::BindGroup bindGroup,
			wgpu::Buffer propertiesBuffer,
			WebGPUMaterialTextures textures,
			WebGPUMaterialOptions options = {});

		~WebGPUMaterial() override
		{
			if (m_bindGroup)
			{
				m_bindGroup.release();
			}
			if (m_propertiesBuffer)
			{
				m_propertiesBuffer.release();
			}
		}

		/**
		 * @brief Set up material state for rendering.
		 * @param encoder The command encoder for recording commands.
		 * @param renderPass The render pass for drawing.
		 */
		void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass) override;

		/**
		 * @brief Get the GPU-side bind group for this material.
		 * @return The WebGPU bind group.
		 */
		wgpu::BindGroup getBindGroup() const { return m_bindGroup; }

		/**
		 * @brief Get the material properties buffer.
		 * @return Buffer containing material properties.
		 */
		wgpu::Buffer getPropertiesBuffer() const { return m_propertiesBuffer; }

		/**
		 * @brief Get the material textures.
		 * @return The material's textures.
		 */
		const WebGPUMaterialTextures &getTextures() const { return m_textures; }

		/**
		 * @brief Get the material options used for this WebGPUMaterial.
		 * @return Reference to the options struct.
		 */
		const WebGPUMaterialOptions &getOptions() const { return m_options; }

	protected:
		/**
		 * @brief Update GPU resources from CPU data.
		 * Implementation of WebGPURenderObject::updateGPUResources().
		 */
		void updateGPUResources() override;

	private:
		/**
		 * @brief The GPU-side bind group for this material.
		 */
		wgpu::BindGroup m_bindGroup;

		/**
		 * @brief Buffer containing material properties.
		 */
		wgpu::Buffer m_propertiesBuffer;

		/**
		 * @brief Textures used by this material.
		 */
		WebGPUMaterialTextures m_textures;

		/**
		 * @brief Options used for 	 this WebGPUMaterial.
		 */
		WebGPUMaterialOptions m_options;
	};

} // namespace engine::rendering::webgpu
