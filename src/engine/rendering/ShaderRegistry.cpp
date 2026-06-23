#include "engine/rendering/ShaderRegistry.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/shaders/EngineStructDescriptors.h"
#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

#ifdef None
#undef None
#endif

namespace engine::rendering
{

using PathProvider = engine::core::PathProvider;

// ToDo: Add Texture vs Add Texture Binding so that we can have optional textures amd BindGroup layouts that match
ShaderRegistry::ShaderRegistry(webgpu::WebGPUContext &context) : m_context(context)
{
}

bool ShaderRegistry::initializeDefaultShaders()
{
	spdlog::info("Initializing default shaders...");

	// Regenerate every WGSL header under resources/generated/ before any
	// shader load. Idempotent (writeIfChanged); cheap when descriptors haven't
	// moved. Must run BEFORE loadShaderModule so the include resolver sees the
	// freshest text on first compile.
	shaders::regenerateEngineGeneratedFiles();

	auto pbrShader = createPBRShader();
	if (!pbrShader || !pbrShader->isValid())
	{
		spdlog::error("Failed to create PBR shader");
		return false;
	}
	registerShader(pbrShader);

	auto gbufferShader = createGBufferShader();
	if (!gbufferShader || !gbufferShader->isValid())
	{
		spdlog::error("Failed to create G-Buffer shader");
		return false;
	}
	registerShader(gbufferShader);

	auto compositionDeferredShader = createCompositionDeferredShader();
	if (!compositionDeferredShader || !compositionDeferredShader->isValid())
	{
		spdlog::warn("Failed to create Composition Deferred shader - deferred rendering will be incomplete");
	}
	else
	{
		registerShader(compositionDeferredShader);
	}

	auto debugShader = createDebugShader();
	if (!debugShader || !debugShader->isValid())
	{
		spdlog::warn("Failed to create Debug shader - debug rendering will be unavailable");
	}
	else
	{
		registerShader(debugShader);
	}

	auto fullscreenQuadShader = createFullscreenQuadShader();
	if (!fullscreenQuadShader || !fullscreenQuadShader->isValid())
	{
		spdlog::error("Failed to create Fullscreen Quad shader");
		return false;
	}
	registerShader(fullscreenQuadShader);

	auto skyboxShader = createSkyboxShader();
	if (!skyboxShader || !skyboxShader->isValid())
	{
		spdlog::error("Failed to create Skybox shader");
		return false;
	}
	registerShader(skyboxShader);

	auto mipmapBlitShader = createMipmapBlitShader();
	if (!mipmapBlitShader || !mipmapBlitShader->isValid())
	{
		spdlog::error("Failed to create Mipmap Blit shader");
		return false;
	}
	registerShader(mipmapBlitShader);

	auto shadowPass2DShader = createShadowPass2DShader();
	if (!shadowPass2DShader || !shadowPass2DShader->isValid())
	{
		spdlog::error("Failed to create Shadow Pass 2D shader");
		return false;
	}
	registerShader(shadowPass2DShader);

	auto shadowPassCubeShader = createShadowPassCubeShader();
	if (!shadowPassCubeShader || !shadowPassCubeShader->isValid())
	{
		spdlog::error("Failed to create Shadow Pass Cube shader");
		return false;
	}
	registerShader(shadowPassCubeShader);

	auto visualizeDepthShader = createVisualizeDepthShader();
	if (!visualizeDepthShader || !visualizeDepthShader->isValid())
	{
		spdlog::warn("Failed to create Visualize Depth shader - shadow visualization will be unavailable");
	}
	else
	{
		registerShader(visualizeDepthShader);
	}

	auto vignetteShader = createVignetteShader();
	if (!vignetteShader || !vignetteShader->isValid())
	{
		spdlog::warn("Failed to create Vignette shader - vignette post-processing will be unavailable");
	}
	else
	{
		registerShader(vignetteShader);
	}

	spdlog::info("Default shaders initialized successfully");
	return true;
}

void ShaderRegistry::reloadAllShaders()
{
	spdlog::info("Reloading all shaders in ShaderRegistry...");

	// Snapshot keys + materialised shader infos under the SlotCache mutex,
	// then iterate outside to avoid recursive lock if reloadShader → register
	// re-enters the cache.
	auto names = m_shaders.keys();
	for (const auto &name : names)
	{
		auto info = m_shaders.find(name).lock();
		if (!info) continue;
		spdlog::info("Reloading shader '{}'", name);
		m_context.shaderFactory().reloadShader(info);
	}

	spdlog::info("Shader reload complete");
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::getShader(const std::string &name) const
{
	// SlotCache::find is const-safe here — it takes the internal mutex.
	auto handle = const_cast<SlotCacheT &>(m_shaders).find(name);
	return handle ? handle.lock() : nullptr;
}

engine::rendering::cache::Handle<webgpu::WebGPUShaderInfo>
ShaderRegistry::getShaderHandle(const std::string &name)
{
	// Hand back the SlotCache Handle directly. Downstream caches use it for
	// version-tracked bind-group invalidation; idle-eviction won't drop a
	// shader slot because we set its maxIdleFrames to 0 in WebGPUContext.
	return m_shaders.find(name);
}

bool ShaderRegistry::registerShader(std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo, bool replaceIfExists)
{
	if (!shaderInfo || !shaderInfo->isValid())
	{
		spdlog::error("Cannot register invalid shader '{}'", shaderInfo ? shaderInfo->getName() : "<null>");
		return false;
	}

	const std::string &name = shaderInfo->getName();
	const bool         exists = m_shaders.find(name).valid();

	if (exists && !replaceIfExists)
	{
		spdlog::warn("Shader '{}' already registered", name);
		return false;
	}

	if (exists)
	{
		// Hot-swap inside the slot — every outstanding shared_ptr from a
		// previous getShader() call keeps the old shader alive until the
		// caller drops it, which is what makes mid-frame hot reload safe.
		m_shaders.replace(name, shaderInfo);
		spdlog::info("Replaced existing shader '{}'", name);
	}
	else
	{
		// First registration creates the slot with a trivial build_fn that
		// just returns the same info on demand — used by getOrCreate's
		// initial build path. Subsequent eviction (rare for shaders) would
		// hand out the captured pointer rather than rebuild from disk;
		// since shaders are immutable once registered this is safe.
		auto captured = shaderInfo;
		m_shaders.getOrCreate(name, [captured]() { return captured; });
		spdlog::info("Registered shader '{}'", name);
	}
	return true;
}

bool ShaderRegistry::unregisterShader(const std::string &name)
{
	if (m_shaders.erase(name))
	{
		spdlog::info("Unregistered shader '{}'", name);
		return true;
	}
	spdlog::warn("Shader '{}' not found for unregistration", name);
	return false;
}

void ShaderRegistry::unregisterAll()
{
	const auto count = m_shaders.cacheSize();
	spdlog::info("Unregistering all {} shaders", count);
	m_shaders.cleanup();
}

bool ShaderRegistry::hasShader(const std::string &name) const
{
	return const_cast<SlotCacheT &>(m_shaders).find(name).valid();
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createPBRShader()
{
	// Canonical layout (doc/SpecShaderSystem.md §4): Frame@0, Scene@1,
	// Material@2, Object@3. The structure is reflected from the WGSL; only the
	// material texture slot mapping + fallback colours are supplied here.
	webgpu::ShaderDescriptor desc;
	desc.name = shader::defaults::PBR;
	desc.type = ShaderType::Lit;
	desc.path = PathProvider::getResource("shaders/PBR_Lit_Shader.wgsl");

	webgpu::BindGroupMeta material;
	material.bindings[2] = {MaterialTextureSlots::DIFFUSE,   glm::vec3(1.0f, 1.0f, 1.0f)};
	material.bindings[3] = {MaterialTextureSlots::NORMAL,    glm::vec3(0.5f, 0.5f, 1.0f)};
	material.bindings[4] = {MaterialTextureSlots::AMBIENT,   glm::vec3(1.0f, 1.0f, 1.0f)};
	material.bindings[5] = {MaterialTextureSlots::ROUGHNESS, glm::vec3(1.0f, 1.0f, 1.0f)};
	material.bindings[6] = {MaterialTextureSlots::METALLIC,  glm::vec3(0.0f, 0.0f, 0.0f)};
	material.bindings[7] = {MaterialTextureSlots::EMISSIVE,  glm::vec3(0.0f, 0.0f, 0.0f)};
	desc.groups[2] = material;

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createGBufferShader()
{
	// The Material group must match PBR_Lit_Shader exactly (same binding order +
	// slot mapping) so the cached per-material bind group works for either pass.
	// Color target formats must stay in sync with webgpu::GBuffer.
	webgpu::ShaderDescriptor desc;
	desc.name = shader::defaults::GBUFFER;
	desc.type = ShaderType::Lit;
	desc.path = PathProvider::getResource("shaders/g_buffer.wgsl");
	desc.colorTargetFormats = {
		engine::rendering::webgpu::GBuffer::FORMAT_POSITION,
		engine::rendering::webgpu::GBuffer::FORMAT_NORMAL,
		engine::rendering::webgpu::GBuffer::FORMAT_ALBEDO,
		engine::rendering::webgpu::GBuffer::FORMAT_MATERIAL,
		engine::rendering::webgpu::GBuffer::FORMAT_EMISSION,
	};

	webgpu::BindGroupMeta material;
	material.bindings[2] = {MaterialTextureSlots::DIFFUSE,   glm::vec3(1.0f, 1.0f, 1.0f)};
	material.bindings[3] = {MaterialTextureSlots::NORMAL,    glm::vec3(0.5f, 0.5f, 1.0f)};
	material.bindings[4] = {MaterialTextureSlots::AMBIENT,   glm::vec3(1.0f, 1.0f, 1.0f)};
	material.bindings[5] = {MaterialTextureSlots::ROUGHNESS, glm::vec3(1.0f, 1.0f, 1.0f)};
	material.bindings[6] = {MaterialTextureSlots::METALLIC,  glm::vec3(0.0f, 0.0f, 0.0f)};
	material.bindings[7] = {MaterialTextureSlots::EMISSIVE,  glm::vec3(0.0f, 0.0f, 0.0f)};
	desc.groups[2] = material;

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createCompositionDeferredShader()
{
	// Canonical layout (doc/SpecShaderSystem.md §4): Frame@0, Scene@1, plus the
	// GBuffer textures at custom @group(4). Structure is reflected from the WGSL;
	// only the custom group's name/reuse policy is supplied here.
	webgpu::ShaderDescriptor desc;
	desc.name          = shader::defaults::COMPOSITION_DEFERRED;
	desc.type          = ShaderType::Unlit;
	desc.path          = PathProvider::getResource("shaders/deferred_composition.wgsl");
	desc.vertexLayout  = VertexLayout::None;
	desc.enableDepth   = false;
	desc.cullBackFaces = false;
	desc.groups[4]     = {"GBuffer_BindGroup", BindGroupType::Custom, BindGroupReuse::Global, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createDebugShader()
{
	// Frame@0 plus the debug primitive storage buffer at custom @group(4).
	webgpu::ShaderDescriptor desc;
	desc.name          = shader::defaults::DEBUG;
	desc.type          = ShaderType::Debug;
	desc.path          = PathProvider::getResource("shaders/debug.wgsl");
	desc.vertexLayout  = VertexLayout::None;
	desc.enableDepth   = false;
	desc.cullBackFaces = false;
	desc.groups[4]     = {bindgroup::defaults::DEBUG, BindGroupType::Debug, BindGroupReuse::PerFrame, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createFullscreenQuadShader()
{
	// Custom @group(4): camera texture + sampler. Custom @group(5): post-process
	// settings (HDR + exposure), one shared buffer owned by CompositePass.
	webgpu::ShaderDescriptor desc;
	desc.name          = shader::defaults::FULLSCREEN_QUAD;
	desc.type          = ShaderType::Unlit;
	desc.path          = PathProvider::getResource("shaders/fullscreen_quad.wgsl");
	desc.vertexLayout  = VertexLayout::None;
	desc.enableDepth   = false;
	desc.cullBackFaces = false;
	desc.groups[4]     = {bindgroup::defaults::FULLSCREEN_QUAD, BindGroupType::Custom, BindGroupReuse::Global, {}};
	desc.groups[5]     = {"PostProcess_BindGroup", BindGroupType::Custom, BindGroupReuse::Global, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createSkyboxShader()
{
	// Skybox samples an equirectangular HDR map across a 36-vertex cube.
	// The WGSL writes clip.xyww so every fragment ends up at depth = 1.0 (far
	// plane). Depth test is LessEqual + read-only so the skybox only fills
	// pixels where the G-buffer left the cleared 1.0 depth value (background)
	// without stamping new depth itself.
	// Frame@0 plus the environment uniform/sampler/texture at custom @group(4).
	// The WGSL writes clip.xyww so depth lands at the far plane: LessEqual test,
	// no depth write, so the skybox only fills uncovered background pixels.
	webgpu::ShaderDescriptor desc;
	desc.name               = shader::defaults::SKYBOX;
	desc.type               = ShaderType::Unlit;
	desc.path               = PathProvider::getResource("shaders/skybox.wgsl");
	desc.vertexLayout       = VertexLayout::None;
	desc.cullBackFaces      = false; // inside-out cube, faces point inward
	desc.depthCompare       = wgpu::CompareFunction::LessEqual;
	desc.depthWrite         = false;
	desc.colorTargetFormats = {wgpu::TextureFormat::RGBA16Float};
	desc.groups[4]          = {bindgroup::defaults::SKYBOX, BindGroupType::Custom, BindGroupReuse::PerFrame, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createMipmapBlitShader()
{
	// Source texture + sampler at custom @group(4); blits one mip to the next.
	webgpu::ShaderDescriptor desc;
	desc.name         = shader::defaults::MIPMAP_BLIT;
	desc.type         = ShaderType::Unlit;
	desc.path         = PathProvider::getResource("shaders/mipmap_blit.wgsl");
	desc.vertexLayout = VertexLayout::None;
	desc.enableDepth  = false;
	desc.groups[4]    = {bindgroup::defaults::MIPMAP_BLIT, BindGroupType::Mipmap, BindGroupReuse::Global, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createShadowPass2DShader()
{
	// Depth-only pass: light view-proj uniform at custom @group(4) + Object@3.
	webgpu::ShaderDescriptor desc;
	desc.name          = shader::defaults::SHADOW_PASS_2D;
	desc.type          = ShaderType::Unlit;
	desc.path          = PathProvider::getResource("shaders/shadow2d.wgsl");
	desc.vertexEntry   = "vs_shadow";
	desc.fragmentEntry = "fs_shadow";
	desc.vertexLayout  = VertexLayout::Position;
	desc.groups[4]     = {bindgroup::defaults::SHADOW_PASS_2D, BindGroupType::ShadowPass2D, BindGroupReuse::PerFrame, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createShadowPassCubeShader()
{
	// Depth-only point-light pass: light pos + far plane at custom @group(4) + Object@3.
	webgpu::ShaderDescriptor desc;
	desc.name          = shader::defaults::SHADOW_PASS_CUBE;
	desc.type          = ShaderType::Unlit;
	desc.path          = PathProvider::getResource("shaders/shadow3d.wgsl");
	desc.vertexEntry   = "vs_shadow_cube";
	desc.fragmentEntry = "fs_shadow_cube";
	desc.vertexLayout  = VertexLayout::Position;
	desc.groups[4]     = {bindgroup::defaults::SHADOW_PASS_CUBE, BindGroupType::ShadowPassCube, BindGroupReuse::PerFrame, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createVisualizeDepthShader()
{
	// Debug: depth-array texture + sampler + layer index at custom @group(4).
	webgpu::ShaderDescriptor desc;
	desc.name         = shader::defaults::VISUALIZE_DEPTH;
	desc.type         = ShaderType::Unlit;
	desc.path         = PathProvider::getResource("shaders/visualize_depth.wgsl");
	desc.vertexLayout = VertexLayout::None;
	desc.enableDepth  = false;
	desc.groups[4]    = {bindgroup::defaults::VISUALIZE_DEPTH, BindGroupType::Custom, BindGroupReuse::Global, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createVignetteShader()
{
	// Input texture + sampler from the previous pass at custom @group(4).
	webgpu::ShaderDescriptor desc;
	desc.name          = shader::defaults::VIGNETTE;
	desc.type          = ShaderType::Unlit;
	desc.path          = PathProvider::getResource("shaders/postprocess_vignette.wgsl");
	desc.vertexLayout  = VertexLayout::None;
	desc.enableDepth   = false;
	desc.cullBackFaces = false;
	desc.groups[4]     = {bindgroup::defaults::VIGNETTE, BindGroupType::Custom, BindGroupReuse::PerFrame, {}};

	return m_context.shaderFactory().buildFromDescriptor(desc);
}

} // namespace engine::rendering
