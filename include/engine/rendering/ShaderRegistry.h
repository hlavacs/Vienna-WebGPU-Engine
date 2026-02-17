#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;
}

namespace engine::rendering
{

namespace shader::defaults
{
constexpr const char *PBR = "PBR_Lit_Shader";
constexpr const char *DEBUG = "Debug_Shader";
constexpr const char *FULLSCREEN_QUAD = "Fullscreen_Quad_Shader";
constexpr const char *MIPMAP_BLIT = "Mipmap_Blit_Shader";
constexpr const char *SHADOW_PASS_2D = "ShadowPass2D_Shader";
constexpr const char *SHADOW_PASS_CUBE = "ShadowPassCube_Shader";
constexpr const char *VISUALIZE_DEPTH = "Visualize_Depth_Shader";
constexpr const char *VIGNETTE = "Vignette_Shader";
} // namespace shader::defaults

namespace bindgroup::defaults
{
constexpr const char *FRAME = "Frame_BindGroup";
constexpr const char *LIGHT = "Light_BindGroup";
constexpr const char *OBJECT = "Object_BindGroup";
constexpr const char *MATERIAL = "Material_BindGroup";
constexpr const char *SHADOW = "Shadow_BindGroup";
constexpr const char *SHADOW_PASS_2D = "ShadowPass2D_BindGroup";
constexpr const char *SHADOW_PASS_CUBE = "ShadowPassCube_BindGroup";
constexpr const char *MIPMAP_BLIT = "MipmapBlit_BindGroup";
constexpr const char *FULLSCREEN_QUAD = "Fullscreen_Quad_BindGroup";
constexpr const char *VISUALIZE_DEPTH = "Visualize_Depth_BindGroup";
constexpr const char *DEBUG = "Debug_BindGroup";
constexpr const char *VIGNETTE = "Vignette_BindGroup";
} // namespace bindgroup::defaults
namespace bindgroup::entry::defaults
{
constexpr const char *MATERIAL_PROPERTIES = "materialProperties";
} // namespace bindgroup::entry::defaults

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

	// Delete copy constructor and copy assignment operator
	ShaderRegistry(const ShaderRegistry &) = delete;
	ShaderRegistry &operator=(const ShaderRegistry &) = delete;

	// Allow move semantics
	ShaderRegistry(ShaderRegistry &&) = default;
	ShaderRegistry &operator=(ShaderRegistry &&) = delete;

	/**
	 * @brief Initialize default engine shaders.
	 * @return True if all default shaders were created successfully.
	 */
	bool initializeDefaultShaders();

	/**
	 * @brief Reload all shaders in the registry.
	 * This is useful for hot-reloading during development.
	 */
	void reloadAllShaders();

	/**
	 * @brief Get a shader by name.
	 * @param name The name of the shader.
	 * @return Shared pointer to shader info, or nullptr if not found.
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUShaderInfo> getShader(const std::string &name) const;

	/**
	 * @brief Register a shader with its name. Names must be unique.
	 * @param shaderInfo The shader to register.
	 * @param replaceIfExists If true, replaces existing shader with the same name.
	 * @return True if registered successfully, false if name already exists.
	 */
	bool registerShader(std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo, bool replaceIfExists = false);

	/**
	 * @brief Unregister and remove a shader by name.
	 * @param name The shader name to remove.
	 * @return True if shader was unregistered, false if not found.
	 */
	bool unregisterShader(const std::string &name);

	/**
	 * @brief Unregister all shaders from the registry.
	 */
	void unregisterAll();

	/**
	 * @brief Check if a shader is registered.
	 * @param name The shader name.
	 * @return True if shader exists.
	 */
	[[nodiscard]] bool hasShader(const std::string &name) const;

  private:
	webgpu::WebGPUContext &m_context;

	// Custom shaders indexed by name
	std::unordered_map<std::string, std::shared_ptr<webgpu::WebGPUShaderInfo>> m_shaders;

	// Helper methods to create specific default shaders
	std::shared_ptr<webgpu::WebGPUShaderInfo> createPBRShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createDebugShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createFullscreenQuadShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createMipmapBlitShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createShadowPass2DShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createShadowPassCubeShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createVisualizeDepthShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createVignetteShader();
};

} // namespace engine::rendering
