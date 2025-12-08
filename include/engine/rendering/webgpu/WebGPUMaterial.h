#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/core/Handle.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"

namespace engine::rendering::webgpu
{
class WebGPUTexture;

/**
 * @brief Options for a WebGPUMaterial.
 */
struct WebGPUMaterialOptions
{
};

/**
 * @class WebGPUMaterial
 * @brief GPU-side material: wraps bind groups and layout setup, maintains a handle to the CPU-side Material.
 * 
 * Uses a dictionary-based texture system that matches texture slot names from the CPU Material
 * to GPU WebGPUTexture instances. This allows flexible, modular material definitions.
 */
class WebGPUMaterial : public WebGPURenderObject<engine::rendering::Material>
{
  public:
	/**
	 * @brief Construct a WebGPUMaterial from a Material handle and texture dictionary.
	 * @param context The WebGPU context.
	 * @param materialHandle Handle to the CPU-side Material.
	 * @param textures Dictionary mapping texture slot names to GPU textures.
	 * @param options Optional material options.
	 */
	WebGPUMaterial(
		WebGPUContext &context, 
		const engine::rendering::Material::Handle &materialHandle, 
		std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> textures,
		WebGPUMaterialOptions options = {}
	);

	~WebGPUMaterial() override = default;

	/**
	 * @brief Set up material state for rendering.
	 * @param encoder The command encoder for recording commands.
	 * @param renderPass The render pass for drawing.
	 */
	void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass) override;

	/**
	 * @brief Get the material textures dictionary.
	 * @return Map of texture slot names to GPU textures.
	 */
	const std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>>& getTextures() const { return m_textures; }
	
	/**
	 * @brief Get a specific texture by slot name.
	 * @param slotName Name of the texture slot.
	 * @return Shared pointer to the texture, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUTexture> getTexture(const std::string& slotName) const
	{
		auto it = m_textures.find(slotName);
		return it != m_textures.end() ? it->second : nullptr;
	}
	
	/**
	 * @brief Set a texture for a specific slot.
	 * @param slotName Name of the texture slot.
	 * @param texture Shared pointer to the GPU texture.
	 */
	void setTexture(const std::string& slotName, std::shared_ptr<WebGPUTexture> texture)
	{
		m_textures[slotName] = texture;
	}

	/**
	 * @brief Get the material options used for this WebGPUMaterial.
	 * @return Reference to the options struct.
	 */
	const WebGPUMaterialOptions &getOptions() const { return m_options; }
	
	/**
	 * @brief Get the pipeline handle for this material.
	 * @return Handle to the WebGPUPipeline, or invalid handle if not set.
	 */
	engine::core::Handle<WebGPUPipeline> getPipelineHandle() const { return m_pipelineHandle; }
	
	/**
	 * @brief Set the pipeline handle for this material.
	 * @param handle Handle to the WebGPUPipeline to use for rendering.
	 */
	void setPipelineHandle(engine::core::Handle<WebGPUPipeline> handle) { m_pipelineHandle = handle; }

	/**
	 * @brief Get the pipeline name for this material (based on shader type).
	 * @return Name of the pipeline to use for rendering.
	 */
	const std::string& getPipelineName() const { return m_pipelineName; }
	
	/**
	 * @brief Set the pipeline name for this material.
	 * @param name Name of the pipeline to use.
	 */
	void setPipelineName(const std::string& name) { m_pipelineName = name; }

	/**
	 * @brief Update GPU resources from CPU data.
	 * Public so that materials can be updated from external code (e.g., factories, renderers).
	 */
	void updateGPUResources();

  private:
	/**
	 * @brief Texture dictionary mapping slot names to GPU textures.
	 */
	std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> m_textures;

	/**
	 * @brief Options used for this WebGPUMaterial.
	 */
	WebGPUMaterialOptions m_options;
	
	/**
	 * @brief Handle to the pipeline used for rendering this material.
	 */
	engine::core::Handle<WebGPUPipeline> m_pipelineHandle;
	
	/**
	 * @brief Name of the pipeline to use for rendering (e.g., "main", "debug").
	 */
	std::string m_pipelineName = "main"; // Default to main pipeline
	
	// Note: Material properties buffer is managed by the shader's bind group (bind group 3, binding 0)
	// It's retrieved via materialBindGroup->findBufferByBinding(0) in updateGPUResources()
};

} // namespace engine::rendering::webgpu
