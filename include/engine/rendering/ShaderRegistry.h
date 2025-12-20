#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;
}

namespace engine::rendering
{

/**
 * @enum ShaderType
 * @brief Predefined shader types for the engine.
 */
enum class ShaderType
{
	// Standard shaders
	Lit,   // Full PBR lighting shader (shader.wgsl)
	Unlit, // Simple unlit shader (future)
	Debug, // Debug visualization shader (debug.wgsl)

	// Custom shader slot
	Custom
};

/**
 * @class ShaderRegistry
 * @brief Central registry for managing shaders used throughout the engine.
 *
 * Shaders are created once during initialization and can be retrieved by type.
 * Custom shaders can be registered dynamically at runtime.
 */
class ShaderRegistry
{
  public:
	explicit ShaderRegistry(webgpu::WebGPUContext &context);
	~ShaderRegistry() = default;

	/**
	 * @brief Initialize default engine shaders.
	 * @return True if all default shaders were created successfully.
	 */
	bool initializeDefaultShaders();

	/**
	 * @brief Get a shader by its type.
	 * @param type The shader type to retrieve.
	 * @return Shared pointer to shader info, or nullptr if not found.
	 */
	std::shared_ptr<webgpu::WebGPUShaderInfo> getShader(ShaderType type) const;

	/**
	 * @brief Get a shader by its type.
	 * @param type The shader type to retrieve.
	 * @param name The custom shader name (if type is Custom).
	 * @return Shared pointer to shader info, or nullptr if not found.
	 */
	std::shared_ptr<webgpu::WebGPUShaderInfo> getShader(ShaderType type, const std::string &customName) const;

	/**
	 * @brief Get a custom shader by name.
	 * @param name The name of the custom shader.
	 * @return Shared pointer to shader info, or nullptr if not found.
	 */
	std::shared_ptr<webgpu::WebGPUShaderInfo> getCustomShader(const std::string &name) const;

	/**
	 * @brief Register a custom shader.
	 * @param name Unique name for the shader.
	 * @param shaderInfo The shader to register.
	 * @return True if registered successfully, false if name already exists.
	 */
	bool registerCustomShader(const std::string &name, std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo);

	/**
	 * @brief Check if a shader type is registered.
	 * @param type The shader type to check.
	 * @return True if shader exists.
	 */
	bool hasShader(ShaderType type) const;

	/**
	 * @brief Check if a custom shader is registered.
	 * @param name The custom shader name.
	 * @return True if shader exists.
	 */
	bool hasCustomShader(const std::string &name) const;

  private:
	webgpu::WebGPUContext &m_context;

	// Default shaders indexed by type
	std::unordered_map<ShaderType, std::shared_ptr<webgpu::WebGPUShaderInfo>> m_defaultShaders;

	// Custom shaders indexed by name
	std::unordered_map<std::string, std::shared_ptr<webgpu::WebGPUShaderInfo>> m_customShaders;

	// Helper methods to create specific default shaders
	std::shared_ptr<webgpu::WebGPUShaderInfo> createLitShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createDebugShader();
};

} // namespace engine::rendering
