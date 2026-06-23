
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/ShaderType.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;
class WebGPUShaderInfo;

/**
 * @brief Per-binding facts WGSL cannot express, applied by binding index.
 *
 * Reflection recovers a binding's index, kind, type and size from the shader;
 * this supplies the rest: a texture's material slot + fallback colour (which
 * make it a per-material texture), or a sampler/texture detail override for
 * the cases WGSL leaves ambiguous (filtering vs non-filtering, filterable vs
 * unfilterable float).
 */
struct BindingMeta
{
	std::optional<std::string>              materialSlot;
	std::optional<glm::vec3>                fallbackColor;
	std::optional<wgpu::SamplerBindingType> samplerType;
	std::optional<wgpu::TextureSampleType>  textureSampleType;
};

/**
 * @brief Non-WGSL metadata for one bind group: engine-facing name, semantic
 * type, reuse policy, and per-binding overrides.
 *
 * Engine groups (`@group` 0..3) get canonical name/type/reuse when omitted;
 * custom groups (>= 4) must provide at least a name.
 */
struct BindGroupMeta
{
	std::string                     name;
	BindGroupType                   type  = BindGroupType::Custom;
	BindGroupReuse                  reuse = BindGroupReuse::PerObject;
	std::map<uint32_t, BindingMeta> bindings;
};

/**
 * @brief Declarative shader registration: a WGSL file plus the facts the WGSL
 * cannot carry.
 *
 * The bind-group structure is read from the include-expanded WGSL by
 * reflection. This descriptor adds only pipeline state and the per-group /
 * per-binding metadata overlay. It is the sole way to register a shader.
 */
struct ShaderDescriptor
{
	std::string                     name;
	std::filesystem::path           path;
	ShaderType                      type          = ShaderType::Lit;
	std::string                     vertexEntry   = "vs_main";
	std::string                     fragmentEntry = "fs_main";
	engine::rendering::VertexLayout vertexLayout  = engine::rendering::VertexLayout::PositionNormalUVTangentColor;

	bool                             enableDepth   = true;
	bool                             cullBackFaces = true;
	bool                             depthWrite    = true;
	wgpu::CompareFunction            depthCompare  = wgpu::CompareFunction::Less;
	std::vector<wgpu::TextureFormat> colorTargetFormats;

	/// Metadata overlay keyed by `@group` index. Engine groups fall back to
	/// canonical defaults when absent.
	std::map<uint32_t, BindGroupMeta> groups;
};

/**
 * @brief Builds WebGPUShaderInfo from a WGSL file by reflecting its bind groups.
 *
 * The shader author declares every resource in the WGSL; the factory reflects
 * the include-expanded source to discover the bind groups and realises their
 * GPU layouts through @ref WebGPUBindGroupFactory. C++ supplies only the facts
 * WGSL cannot carry, via @ref ShaderDescriptor.
 */
class WebGPUShaderFactory
{
  public:
	/**
	 * @brief Constructs a WebGPUShaderFactory bound to a WebGPU context.
	 * @param context Reference to the WebGPU context for device access.
	 */
	explicit WebGPUShaderFactory(WebGPUContext &context);

	/**
	 * @brief Build a shader from a declarative @ref ShaderDescriptor.
	 *
	 * Reads and include-expands the WGSL, reflects it to discover every bind
	 * group, then realises each layout through @ref WebGPUBindGroupFactory,
	 * overlaying the descriptor's name/type/reuse and per-binding metadata.
	 * Engine groups (@group 0..3) reuse the shared global layouts; custom
	 * groups are created per shader.
	 */
	std::shared_ptr<WebGPUShaderInfo> buildFromDescriptor(const ShaderDescriptor &desc);

	/**
	 * @brief Loads and include-expands a WGSL file into a shader module.
	 * @param shaderPath Path to the WGSL shader file.
	 * @return Loaded shader module, or null on failure.
	 */
	wgpu::ShaderModule loadShaderModule(const std::filesystem::path &shaderPath);

	/**
	 * @brief Reloads a shader by recompiling its module and preserving the
	 * existing bind-group layouts and pipeline state.
	 * @param shaderInfo The shader info to reload (source of path + metadata).
	 * @return True if the shader was successfully reloaded.
	 */
	bool reloadShader(std::shared_ptr<WebGPUShaderInfo> shaderInfo);

  private:
	/// Read a WGSL file and run the `#include` resolver, returning the final
	/// expanded source. Empty on read failure. Include errors are logged.
	std::string expandShaderSource(const std::filesystem::path &shaderPath);

	/// Compile expanded WGSL into a shader module. Returns null on failure.
	wgpu::ShaderModule createShaderModuleFromWgsl(const std::string &wgsl, const std::filesystem::path &path);

	WebGPUContext &m_context;
};

} // namespace engine::rendering::webgpu
