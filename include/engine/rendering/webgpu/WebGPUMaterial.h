#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/core/Handle.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUSyncObject.h"

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
class WebGPUMaterial : public WebGPUSyncObject<engine::rendering::Material>, public std::enable_shared_from_this<WebGPUMaterial>
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
	 * @brief Bind the material for rendering.
	 * @param renderPass The render pass encoder.
	 */
    void bind(wgpu::RenderPassEncoder &renderPass) const;

	/**
	 * @brief Get the material textures dictionary.
	 * @return Map of texture slot names to GPU textures.
	 */
	const std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> &getTextures() const { return m_textures; }

	/**
	 * @brief Get a specific texture by slot name.
	 * @param slotName Name of the texture slot.
	 * @return Shared pointer to the texture, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUTexture> getTexture(const std::string &slotName) const
	{
		auto it = m_textures.find(slotName);
		return it != m_textures.end() ? it->second : nullptr;
	}

	/**
	 * @brief Set a texture for a specific slot.
	 * @param slotName Name of the texture slot.
	 * @param texture Shared pointer to the GPU texture.
	 */
	void setTexture(const std::string &slotName, std::shared_ptr<WebGPUTexture> texture)
	{
		m_textures[slotName] = texture;
	}

	/**
	 * @brief Get the material options used for this WebGPUMaterial.
	 * @return Reference to the options struct.
	 */
	const WebGPUMaterialOptions &getOptions() const { return m_options; }

	/**
	 * @brief Get the shader name used by this material.
	 * @return The shader name string.
	 */
	const std::string &getShaderName() const { return m_shaderName; }

  protected:
	/**
	 * @brief Check if synchronization is needed.
	 * Checks material version and all texture versions.
	 */
	bool needsSync(const Material &cpuMaterial) const override;

	/**
	 * @brief Sync GPU resources from CPU material.
	 * Updates material properties and recreates bind groups if textures changed.
	 */
	void syncFromCPU(const Material &cpuMaterial) override;

  private:
	/**
	 * @brief Cache texture versions for dependency tracking.
	 */
	void cacheTextureVersions(const Material &cpuMaterial);

	/**
	 * @brief Texture dictionary mapping slot names to GPU textures.
	 */
	std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> m_textures;

	/**
	 * @brief Cached texture versions for dependency tracking.
	 */
	std::unordered_map<std::string, uint64_t> m_textureVersions;

	/**
	 * @brief Options used for this WebGPUMaterial.
	 */
	WebGPUMaterialOptions m_options;

	/**
	 * @brief The name of the shader used by this material.
	 */
	std::string m_shaderName;

	/**
	 * @brief The material bind group.
	 */
    std::shared_ptr<WebGPUBindGroup> m_materialBindGroup;
};

} // namespace engine::rendering::webgpu
