#pragma once

#include <memory>
#include <string>

#include <webgpu/webgpu.hpp>

#include "engine/rendering/cache/SlotCache.h"
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
constexpr const char *GBUFFER = "GBuffer";
constexpr const char *COMPOSITION_DEFERRED = "Composition_Deferred";
constexpr const char *DEBUG = "Debug_Shader";
constexpr const char *FULLSCREEN_QUAD = "Fullscreen_Quad_Shader";
constexpr const char *SKYBOX = "Skybox_Shader";
constexpr const char *MIPMAP_BLIT = "Mipmap_Blit_Shader";
constexpr const char *SHADOW_PASS_2D = "ShadowPass2D_Shader";
constexpr const char *SHADOW_PASS_CUBE = "ShadowPassCube_Shader";
constexpr const char *VISUALIZE_DEPTH = "Visualize_Depth_Shader";
constexpr const char *VIGNETTE = "Vignette_Shader";
} // namespace shader::defaults

namespace shader::resource_paths
{
constexpr const char *CLUSTER_COMPUTE = "shaders/light_clustering.wgsl";
} // namespace shader::resource_paths

namespace bindgroup::defaults
{
constexpr const char *FRAME = "Frame_BindGroup";
constexpr const char *SCENE = "Scene_BindGroup";
constexpr const char *OBJECT = "Object_BindGroup";
constexpr const char *MATERIAL = "Material_BindGroup";
constexpr const char *SHADOW_PASS_2D = "ShadowPass2D_BindGroup";
constexpr const char *SHADOW_PASS_CUBE = "ShadowPassCube_BindGroup";
constexpr const char *MIPMAP_BLIT = "MipmapBlit_BindGroup";
constexpr const char *FULLSCREEN_QUAD = "Fullscreen_Quad_BindGroup";
constexpr const char *SKYBOX = "Skybox_BindGroup";
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
	 * @brief Get the slot Handle for @p name — versioned access.
	 *
	 * Consumers that need to invalidate downstream caches on shader hot
	 * reload (material bind groups, custom pipelines, etc.) should call
	 * this instead of `getShader`. The returned `Handle<WebGPUShaderInfo>`
	 * exposes:
	 *  - `lock()` for the current snapshot (same as `getShader` + version)
	 *  - `version()` — bumps each time `registerShader(..., true)` or
	 *    `WebGPUShaderFactory::reloadShader` swaps the slot's resource
	 *    in place. Wire this into `BindGroupSignature::addVersioned` so a
	 *    hot reload propagates even when the new shader's bind group
	 *    layout happens to reuse the same allocation.
	 *
	 * Returns an empty Handle if @p name is not registered.
	 */
	[[nodiscard]] engine::rendering::cache::Handle<webgpu::WebGPUShaderInfo>
	getShaderHandle(const std::string &name);

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

	/// Drop every cached shader's resource pointer but keep the slot + name
	/// alive. Future `getShader(name)` after a soft-clear returns nullptr
	/// until `registerShader` repopulates the slot — used by the "Clear All"
	/// UI flow that wants visible reload without breaking outstanding
	/// `shared_ptr<WebGPUShaderInfo>` consumers (each pass holds its own
	/// ref via shared_ptr, so the underlying shader stays valid until
	/// they drop it).
	void softClear() { m_shaders.clearResources(); }

	// --- CacheRegistry surface ----------------------------------------------
	void                       cleanup() { m_shaders.cleanup(); }
	[[nodiscard]] std::size_t  cacheSize() const { return m_shaders.cacheSize(); }
	[[nodiscard]] std::size_t  aliveCount() const { return m_shaders.aliveCount(); }
	void                       notifyFrame() { m_shaders.notifyFrame(); }
	std::size_t                evictStale() { return m_shaders.evictStale(); }
	void                       setMaxIdleFrames(uint32_t frames) { m_shaders.setMaxIdleFrames(frames); }
	[[nodiscard]] uint32_t     maxIdleFrames() const { return m_shaders.maxIdleFrames(); }

  private:
	webgpu::WebGPUContext &m_context;

	// Shader storage. SlotCache slots own one shared_ptr<WebGPUShaderInfo>
	// each; getShader() materialises a snapshot via Handle::lock().
	// registerShader uses getOrCreate (initial) or replace (hot-reload via
	// WebGPUShaderFactory::reloadShader). Slots have no auto-rebuild
	// build_fn — shaders are not built from a key, they're built by
	// hand-rolled createXxxShader() helpers during initialiseDefaultShaders
	// and re-registered after hot-reload. Eviction therefore *removes*
	// the entry rather than rebuilding it.
	using SlotCacheT = engine::rendering::cache::SlotCache<std::string, webgpu::WebGPUShaderInfo>;
	SlotCacheT m_shaders;

	// Helper methods to create specific default shaders
	std::shared_ptr<webgpu::WebGPUShaderInfo> createPBRShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createGBufferShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createCompositionDeferredShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createDebugShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createFullscreenQuadShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createSkyboxShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createMipmapBlitShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createShadowPass2DShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createShadowPassCubeShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createVisualizeDepthShader();
	std::shared_ptr<webgpu::WebGPUShaderInfo> createVignetteShader();
};

} // namespace engine::rendering
