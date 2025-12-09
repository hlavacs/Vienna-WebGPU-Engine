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
	std::string name;			  // Debug/shader variable name
	std::string materialSlotName; // Material texture slot name (for textures only, e.g., MaterialTextureSlots::ALBEDO)
	BindingType type = BindingType::UniformBuffer;
	uint32_t binding = 0;
	size_t size = 0;				// For buffers
	WGPUBufferUsageFlags usage = 0; // For buffers
	bool isGlobal = false;
	uint32_t visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	bool readOnly = false; // For storage buffers

	// For textures
	wgpu::TextureSampleType textureSampleType = wgpu::TextureSampleType::Float;
	wgpu::TextureViewDimension textureViewDimension = wgpu::TextureViewDimension::_2D;
	bool textureMultisampled = false;

	// For samplers
	wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering;
};

/**
 * @brief Builder pattern factory for creating shader metadata with manual reflection.
 *
 * Since WebGPU provides no shader reflection API, ShaderFactory uses a builder
 * pattern to manually describe shader structure:
 * - Bind group organization
 * - Buffer bindings (global vs per-material)
 * - Texture/sampler bindings
 *
 * Usage:
 * ```cpp
 * auto shaderInfo = ShaderFactory(context)
 *     .begin("myShader", "vs_main", "fs_main", shaderPath)
 *     .addFrameUniforms(0, 0)      // group 0, binding 0
 *     .addLightUniforms(0, 1)      // group 0, binding 1
 *     .build();
 * ```
 */
class ShaderFactory
{
  public:
	/**
	 * @brief Constructs a ShaderFactory bound to a WebGPU context.
	 * @param context Reference to the WebGPU context for device access.
	 */
	explicit ShaderFactory(WebGPUContext &context);

	/**
	 * @brief Begins building a new shader.
	 * @param name Shader name for debugging and lookup.
	 * @param vertexEntry Vertex shader entry point name.
	 * @param fragmentEntry Fragment shader entry point name.
	 * @param shaderPath Optional path to WGSL file (empty to skip loading).
	 * @return Reference to this factory for chaining.
	 */
	ShaderFactory &begin(
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
	ShaderFactory &setShaderModule(wgpu::ShaderModule module);

	// === Predefined Global Uniforms ===

	/**
	 * @brief Adds frame uniforms (view/projection matrix, camera position, time).
	 * @param groupIndex Bind group index (typically 0).
	 * @param binding Binding index within the group (typically 0).
	 * @return Reference to this factory for chaining.
	 */
	ShaderFactory &addFrameUniforms(uint32_t groupIndex, uint32_t binding);

	/**
	 * @brief Adds light data uniforms (light count + array of lights).
	 * @param groupIndex Bind group index (typically 0).
	 * @param binding Binding index within the group (typically 1).
	 * @param maxLights Maximum number of lights to support (default 16).
	 * @return Reference to this factory for chaining.
	 */
	ShaderFactory &addLightUniforms(uint32_t groupIndex, uint32_t binding, size_t maxLights = 16);

	/**
	 * @brief Adds camera uniforms (if different from frame uniforms).
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @return Reference to this factory for chaining.
	 */
	ShaderFactory &addCameraUniforms(uint32_t groupIndex, uint32_t binding);

	// === Custom Uniforms ===

	/**
	 * @brief Adds a custom uniform buffer (per-material or global).
	 * @param name Buffer name for debugging.
	 * @param size Buffer size in bytes.
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @param isGlobal Whether this is engine-managed (true) or per-material (false).
	 * @param visibility Shader stage visibility flags.
	 * @return Reference to this factory for chaining.
	 */
	ShaderFactory &addCustomUniform(
		const std::string &name,
		size_t size,
		uint32_t groupIndex,
		uint32_t binding,
		bool isGlobal = false,
		uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
	);

	/**
	 * @brief Adds a storage buffer.
	 * @param name Buffer name for debugging.
	 * @param size Buffer size in bytes.
	 * @param groupIndex Bind group index.
	 * @param binding Binding index within the group.
	 * @param readOnly Whether the buffer is read-only.
	 * @param isGlobal Whether this is engine-managed.
	 * @param visibility Shader stage visibility flags.
	 * @return Reference to this factory for chaining.
	 */
	ShaderFactory &addStorageBuffer(
		const std::string &name,
		size_t size,
		uint32_t groupIndex,
		uint32_t binding,
		bool readOnly = true,
		bool isGlobal = false,
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
	ShaderFactory &addTexture(
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
	ShaderFactory &addSampler(
		const std::string &name,
		uint32_t groupIndex,
		uint32_t binding,
		wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering,
		uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Fragment)
	);

	// Future: Add texture and sampler methods
	// ShaderFactory& addTexture(...);
	// ShaderFactory& addSampler(...);

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

	/**
	 * @brief Gets a global buffer from the cache by name.
	 * @param bufferName The name of the global buffer to retrieve.
	 * @return Shared pointer to the buffer, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUBuffer> getGlobalBuffer(const std::string &bufferName) const;

  private:
	/**
	 * @brief Ensures a bind group builder exists at the given index.
	 * @param groupIndex The bind group index.
	 * @return Reference to the bindings vector for that group.
	 */
	std::vector<ShaderBinding> &getOrCreateBindGroup(uint32_t groupIndex);

	/**
	 * @brief Creates bind group layouts from the buffer bindings.
	 */
	void createBindGroupLayouts();

	/**
	 * @brief Creates buffers (global and non-global) and bind groups.
	 */
	void createBuffersAndBindGroups();

	/**
	 * @brief Creates a buffer from a binding specification.
	 * @param binding The shader binding specification.
	 * @return Shared pointer to the created buffer, or nullptr on failure.
	 */
	std::shared_ptr<WebGPUBuffer> createBuffer(const ShaderBinding &binding);

	/**
	 * @brief Loads shader module from file path.
	 */
	void loadShaderModule();

	WebGPUContext &m_context;
	std::shared_ptr<WebGPUShaderInfo> m_shaderInfo;
	std::filesystem::path m_shaderPath;
	std::map<uint32_t, std::vector<ShaderBinding>> m_bindGroupsBuilder;

	// Temporary storage during build
	std::map<uint32_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> m_tempLayouts;

	// Cache for global buffers (shared across shader instances)
	std::map<std::string, std::shared_ptr<WebGPUBuffer>> m_globalBufferCache;
};

} // namespace engine::rendering::webgpu
