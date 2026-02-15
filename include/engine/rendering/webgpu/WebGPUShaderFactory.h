
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/ShaderType.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;
class WebGPUShaderInfo;
class WebGPUBindGroup;

/**
 * @brief Describes a single binding inside a bind group.
 */
struct ShaderBinding
{
	std::string name;
	std::optional<std::string> materialSlotName;
	BindingType type = BindingType::UniformBuffer;
	uint32_t binding = 0;

	size_t size = 0; ///< For buffers
	WGPUBufferUsageFlags usage = 0;
	uint32_t visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	bool readOnly = false;

	// Texture
	wgpu::TextureSampleType textureSampleType = wgpu::TextureSampleType::Float;
	wgpu::TextureViewDimension textureViewDimension = wgpu::TextureViewDimension::_2D;
	bool textureMultisampled = false;
	std::optional<glm::vec3> fallbackColor = std::nullopt;

	// Sampler
	wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering;
};

/**
 * @brief Factory/builder for creating WebGPUShaderInfo with typed bind groups.
 */
class WebGPUShaderFactory
{
  public:
	/**
	 * @brief Constructs a WebGPUShaderFactory bound to a WebGPU context.
	 * @param context Reference to the WebGPU context for device access.
	 */
	explicit WebGPUShaderFactory(WebGPUContext &context);

	struct BindGroupBuilder
	{
		std::string name;								  ///< User-provided name for the bind group (also used as cache key)
		BindGroupType type = BindGroupType::Custom;		  ///< Semantic type of bind group
		BindGroupReuse reuse = BindGroupReuse::PerObject; ///< Reuse policy
		std::vector<ShaderBinding> bindings;			  ///< Typed bindings in this bind group
		bool isEngineDefault = false;					  ///< Whether this is a global/shared bind group
														  // ToDo: isEngineDefault is used for bindgroup layouts. It is bad design and we should have a WebGPUBindGroupRegistry where we can register default bindgroups on initialization.
	};

	class WebGPUShaderBuilder
	{
		friend class WebGPUShaderFactory;

	  public:
		/**
		 * @brief Adds a new bind group. All subsequent bindings attach here.
		 * @param name The name/key for this bind group (e.g., "shadow", "frame", "material").
		 * @param reuse Reuse policy: Shared for global bindgroups, PerObject for per-material.
		 * @param type Optional semantic type for bind group caching and lookup.
		 * @return Reference to this builder for chaining.
		 *
		 * After calling this, you can retrieve the bind group layout from the shader via:
		 * ```
		 * auto shader = factory.begin(...)
		 *     .addBindGroup("shadow", BindGroupReuse::Shared, BindGroupType::Shadow)
		 *     .build();
		 * auto shadowLayout = shader->getBindGroupLayout("shadow");
		 * auto shadowLayoutByType = shader->getBindGroupLayout(BindGroupType::Shadow);
		 * ```
		 */
		WebGPUShaderBuilder &addBindGroup(
			const std::string &name,
			BindGroupReuse reuse = BindGroupReuse::PerObject,
			BindGroupType type = BindGroupType::Custom
		);

		/**
		 * @brief Adds a uniform buffer binding to the current bind group.
		 * @param name Uniform buffer name for debugging.
		 * @param size Buffer size in bytes.
		 * @param binding Binding index within the group.
		 * @param visibility Shader stage visibility flags.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addUniform(
			const std::string &name,
			size_t size,
			uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
		);

		/**
		 * @brief Adds a storage buffer binding to the current bind group.
		 * @param name Storage buffer name for debugging.
		 * @param size Buffer size in bytes.
		 * @param binding Binding index within the group.
		 * @param readOnly Whether the buffer is read-only.
		 * @param visibility Shader stage visibility flags.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addStorageBuffer(
			const std::string &name,
			size_t size,
			bool readOnly = true,
			uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
		);

		/**
		 * @brief Adds a texture binding to the current bind group.
		 * @param name Texture variable name in shader for debugging.
		 * @param binding Binding index within the group.
		 * @param sampleType Texture sample type (default: Float).
		 * @param viewDimension Texture view dimension (default: 2D).
		 * @param multisampled Whether the texture is multisampled (default: false).
		 * @param fallbackColor Optional fallback color for missing textures.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addTexture(
			const std::string &name,
			wgpu::TextureSampleType sampleType = wgpu::TextureSampleType::Float,
			wgpu::TextureViewDimension viewDimension = wgpu::TextureViewDimension::_2D,
			bool multisampled = false,
			uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Fragment)
		);

		/**
		 * @brief Adds a material texture binding to the current bind group.
		 * @param name Texture variable name in shader for debugging.
		 * @param materialSlotName Material slot name (e.g., "albedo", "normal").
		 * @param sampleType Texture sample type (default: Float).
		 * @param viewDimension Texture view dimension (default: 2D).
		 * @param visibility Shader stage visibility flags.
		 * @param fallbackColor Optional fallback color for missing textures.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addMaterialTexture(
			const std::string &name,
			const std::string &materialSlotName,
			wgpu::TextureSampleType sampleType = wgpu::TextureSampleType::Float,
			wgpu::TextureViewDimension viewDimension = wgpu::TextureViewDimension::_2D,
			uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Fragment),
			std::optional<glm::vec3> fallbackColor = std::nullopt
		);

		/**
		 * @brief Adds a sampler binding to the current bind group.
		 * @param name Sampler name for debugging.
		 * @param binding Binding index within the group.
		 * @param samplerType Sampler binding type (default: Filtering).
		 * @param visibility Shader stage visibility flags.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addSampler(
			const std::string &name,
			wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering,
			uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Fragment)
		);

		/**
		 * @brief Adds frame uniforms bind group (view/projection matrix, camera position, time).
		 * Automatically creates a new "Frame" bind group with BindGroupType::Frame.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addFrameBindGroup();

		/**
		 * @brief Adds object uniforms bind group (model matrix, normal matrix).
		 * Automatically creates a new "Object" bind group with BindGroupType::Object.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addObjectBindGroup();

		/**
		 * @brief Adds light data bind group (light count + array of lights).
		 * Automatically creates a new "Light" bind group with BindGroupType::Light.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addLightBindGroup(); // ToDo: Move configurations out

		/**
		 * @brief Adds shadow mapping bind group (shadow textures, samplers, uniforms).
		 * Automatically creates a new "Shadow" bind group with BindGroupType::Shadow.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addShadowBindGroup();

		/**
		 * @brief Adds a custom uniform buffer (per-material or global).
		 * @param name Buffer name for debugging.
		 * @param size Buffer size in bytes.
		 * @param visibility Shader stage visibility flags.
		 * @return Reference to this builder for chaining.
		 */
		WebGPUShaderBuilder &addCustomUniform(
			const std::string &name,
			size_t size,
			uint32_t visibility = static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
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
		 * @brief Constructs a WebGPUShaderBuilder with the given parameters.
		 */
		WebGPUShaderBuilder(
			WebGPUShaderFactory &factory,
			std::string name,
			ShaderType type,
			std::string vertexEntry,
			std::string fragmentEntry,
			engine::rendering::VertexLayout vertexLayout,
			bool depthEnabled,
			bool blendEnabled,
			bool cullBackFaces,
			std::filesystem::path shaderPath
		);

		/**
		 * @brief Checks that a bind group has been added before adding bindings.
		 */
		void checkLastBindGroup();

		std::string m_name;
		ShaderType m_type;
		std::string m_vertexEntry;
		std::string m_fragmentEntry;
		engine::rendering::VertexLayout m_vertexLayout;
		wgpu::ShaderModule m_shaderModule;
		bool m_depthEnabled;
		bool m_blendEnabled;
		bool m_backFaceCullingEnabled;
		uint32_t m_shaderFeatures = 0;
		std::filesystem::path m_shaderPath;

		std::map<uint32_t, BindGroupBuilder> m_bindGroupsBuilder;
		int32_t m_lastBindGroupIndex;
		WebGPUShaderFactory &m_factory;
	};

	/**
	 * @brief Begins building a new shader.
	 * @param name Shader name for debugging and lookup.
	 * @param type Shader type enum.
	 * @param shaderPath Path to WGSL file.
	 * @param vertexEntry Vertex shader entry point name.
	 * @param fragmentEntry Fragment shader entry point name.
	 * @param vertexLayout Vertex layout enum for the shader (default PositionNormalUVTangentColor).
	 * @param depthEnabled Whether depth testing is enabled (default true).
	 * @param blendEnabled Whether blending is enabled (default false).
	 * @param cullBackFaces Whether back-face culling is enabled (default true).
	 * @return A WebGPUShaderBuilder for chaining method calls.
	 */
	WebGPUShaderBuilder begin(
		const std::string &name,
		ShaderType type,
		const std::filesystem::path &shaderPath,
		const std::string &vertexEntry,
		const std::string &fragmentEntry,
		engine::rendering::VertexLayout vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor,
		bool depthEnabled = true,
		bool blendEnabled = false,
		bool cullBackFaces = true
	);

	/**
	 * @brief Loads shader module from file path.
	 * @param shaderPath Path to the WGSL shader file.
	 * @return Loaded shader module.
	 */
	wgpu::ShaderModule loadShaderModule(const std::filesystem::path &shaderPath);

	/**
	 * @brief Reloads a specific shader info by reconstructing it with current data.
	 * This does not modify the existing shader info, but creates a new one.
	 * @note This will update the shader registry with the reloaded shader.
	 * @param shaderInfo The shader info to reload (used to get path and metadata).
	 * @return True if the shader was successfully reloaded, false otherwise.
	 */
	bool reloadShader(std::shared_ptr<WebGPUShaderInfo> shaderInfo);

  private:
	/**
	 * @brief Creates bind group layouts from the buffer bindings.
	 * @param shaderInfo Shared pointer to the shader info to populate.
	 * @param bindGroupsBuilder Map of bind group builders to create layouts from.
	 */
	void createBindGroupLayouts(std::shared_ptr<WebGPUShaderInfo> shaderInfo, std::map<uint32_t, BindGroupBuilder> &bindGroupsBuilder);

	WebGPUContext &m_context;
};

} // namespace engine::rendering::webgpu
