#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;
class WebGPUBindGroupLayoutInfo;

/**
 * @brief Type of binding in a bind group.
 */
enum class BindingType
{
	UniformBuffer,
	StorageBuffer,
	Texture,
	Sampler
};

/**
 * @brief Helper struct to describe a binding during shader creation.
 */
struct ShaderBinding
{
	std::string name;
	std::string materialSlotName;

	BindingType type = BindingType::UniformBuffer;
	uint32_t binding = 0;

	size_t size = 0;
	WGPUBufferUsageFlags usage = 0;
	uint32_t visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	bool readOnly = false;

	// Texture
	wgpu::TextureSampleType textureSampleType = wgpu::TextureSampleType::Float;
	wgpu::TextureViewDimension textureViewDimension = wgpu::TextureViewDimension::_2D;
	bool textureMultisampled = false;

	// Sampler
	wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering;
};

/**
 * @brief Builder pattern factory for creating shader metadata with manual reflection.
 *
 * Since WebGPU provides no shader reflection API, WebGPUShaderFactory uses a builder
 * pattern to manually describe shader structure:
 * - Bind group organization
 * - Buffer bindings (global vs per-material)
 * - Texture/sampler bindings
 *
 * Usage:
 * ```cpp
 * auto shaderInfo = WebGPUShaderFactory(context)
 *     .begin("myShader", "vs_main", "fs_main", shaderPath)
 *     .addFrameUniforms(0)      // group 0
 *     .addLightUniforms(1)      // group 1
 *     .addCustomUniform("MyUniforms", sizeof(MyUniforms), 2, 0) // group 2
 *     .addTexture("albedoTex", "albedo", 3, 0) // group 3
 *     .build();
 * ```
 */
class WebGPUShaderFactory
{
  public:
	struct BindGroupBuilder
	{
		std::optional<std::string> key; // identity of the whole bind group
		std::vector<ShaderBinding> bindings;
	};

	/**
	 * @brief Constructs a WebGPUShaderFactory bound to a WebGPU context.
	 * @param context Reference to the WebGPU context for device access.
	 */
	explicit WebGPUShaderFactory(WebGPUContext &context);

	/**
	 * @brief Begins building a new shader.
	 * @param name Shader name for debugging and lookup.
	 * @param vertexEntry Vertex shader entry point name.
	 * @param fragmentEntry Fragment shader entry point name.
	 * @param shaderPath Optional path to WGSL file (empty to skip loading).
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &begin(
		const std::string &name,
		const std::string &vertexEntry,
		const std::string &fragmentEntry,
		const std::filesystem::path &shaderPath = ""
	);

	/**
	 * @brief Sets the shader module directly (alternative to loading from file).
	 * @param module Pre-compiled shader module.
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &setShaderModule(wgpu::ShaderModule module);

	/**
	 * @brief Sets the key for a specific bind group.
	 * @param groupIndex Bind group index.
	 * @param key Unique key for the bind group.
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &asGlobalBindGroup(uint32_t groupIndex, std::string key);

	/**
	 * @brief Adds frame uniforms (view/projection matrix, camera position, time).
	 * @param groupIndex Bind group index (typically 0).
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &addFrameUniforms(uint32_t groupIndex);

	/**
	 * @brief Adds light data uniforms (light count + array of lights).
	 * @param groupIndex Bind group index (typically 1).
	 * @param maxLights Maximum number of lights to support (default 16).
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &addLightUniforms(uint32_t groupIndex, size_t maxLights = 16);

	// === Custom Uniforms ===

	/**
	 * @brief Adds a custom uniform buffer (per-material or global).
	 * @param name Buffer name for debugging.
	 * @param size Buffer size in bytes.
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @param visibility Shader stage visibility flags.
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &addCustomUniform(
		const std::string &name,
		size_t size,
		uint32_t groupIndex,
		uint32_t binding,
		uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
	);

	/**
	 * @brief Adds a storage buffer.
	 * @param name Buffer name for debugging.
	 * @param size Buffer size in bytes.
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @param readOnly Whether the buffer is read-only.
	 * @param visibility Shader stage visibility flags.
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &addStorageBuffer(
		const std::string &name,
		size_t size,
		uint32_t groupIndex,
		uint32_t binding,
		bool readOnly = true,
		uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
	);

	/**
	 * @brief Adds a texture binding.
	 * @param name Texture variable name in shader for debugging.
	 * @param materialSlotName Material texture slot name (e.g., MaterialTextureSlots::ALBEDO).
	 *                         This is used to match the texture from Material::getTexture(slotName).
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @param sampleType Texture sample type (default: Float).
	 * @param viewDimension Texture view dimension (default: 2D).
	 * @param multisampled Whether the texture is multisampled (default: false).
	 * @param visibility Shader stage visibility flags.
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &addTexture(
		const std::string &name,
		const std::string &materialSlotName,
		uint32_t groupIndex,
		uint32_t binding,
		wgpu::TextureSampleType sampleType = wgpu::TextureSampleType::Float,
		wgpu::TextureViewDimension viewDimension = wgpu::TextureViewDimension::_2D,
		bool multisampled = false,
		uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Fragment)
	);

	/**
	 * @brief Adds a sampler binding.
	 * @param name Sampler name for debugging.
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @param samplerType Sampler binding type (default: Filtering).
	 * @param visibility Shader stage visibility flags.
	 * @return Reference to this factory for chaining.
	 */
	WebGPUShaderFactory &addSampler(
		const std::string &name,
		uint32_t groupIndex,
		uint32_t binding,
		wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering,
		uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Fragment)
	);

	/**
	 * @brief Finalizes the shader and creates GPU resources.
	 *
	 * This method:
	 * - Loads/validates the shader module (if not already set)
	 * - Creates bind group layouts from the metadata
	 * - Does NOT create per-material buffers (that's the material system's job)
	 *
	 * @return Complete WebGPUShaderInfo ready for pipeline creation.
	 */
	std::shared_ptr<WebGPUShaderInfo> build();

  private:
	/**
	 * @brief Ensures a bind group builder exists at the given index.
	 * @param groupIndex The bind group index.
	 * @return Reference to the BindGroupBuilder for the specified index.
	 */
	BindGroupBuilder &getOrCreateBindGroup(uint32_t groupIndex);

	/**
	 * @brief Creates bind group layouts from the buffer bindings.
	 */
	void createBindGroupLayouts();

	/**
	 * @brief Loads shader module from file path.
	 */
	void loadShaderModule();

	WebGPUContext &m_context;
	std::shared_ptr<WebGPUShaderInfo> m_shaderInfo;
	std::filesystem::path m_shaderPath;
	std::map<uint32_t, BindGroupBuilder> m_bindGroupsBuilder;
};

} // namespace engine::rendering::webgpu
