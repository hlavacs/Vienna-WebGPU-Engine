#include "engine/rendering/ShaderRegistry.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShadowUniforms.h"
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
	// Create the standard PBR lit shader matching PBR_Lit_Shader.wgsl
	//
	// PBR_Lit_Shader.wgsl structure:
	// @group(0) @binding(0) var<uniform> uFrame: FrameUniforms;
	// @group(1) @binding(0) var<storage, read> uLights: LightsBuffer;
	// @group(2) @binding(0) var<uniform> uObject: ObjectUniforms;
	// @group(3) @binding(0) var<uniform> uMaterial: MaterialUniforms;
	// @group(3) @binding(1) var textureSampler: sampler;
	// @group(3) @binding(2) var baseColorTexture: texture_2d<f32>;
	// @group(3) @binding(3) var normalTexture: texture_2d<f32>;
	// @group(3) @binding(4) var aoTexture: texture_2d<f32>;
	// @group(3) @binding(5) var roughnessTexture: texture_2d<f32>;
	// @group(3) @binding(6) var metallicTexture: texture_2d<f32>;
	// @group(3) @binding(7) var emissionTexture: texture_2d<f32>;
	// @group(4) @binding(0) var shadowSampler: sampler;
	// @group(4) @binding(1) var shadowMap2DArray:
	// @group(4) @binding(2) var shadowMapCubeArray:
	// @group(4) @binding(3) var<storage, read> uShadowData2D: ShadowData2DBuffer;
	// @group(4) @binding(4) var<storage, read> uShadowDataCube: ShadowDataCubeBuffer;
	// @group(5) @binding(0) var<uniform> uEnvironment: EnvironmentUniforms;
	// @group(5) @binding(1) var environmentSampler: sampler;
	// @group(5) @binding(2) var environmentTexture: texture_2d<f32>;
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::PBR,
				ShaderType::Lit,
				PathProvider::getResource("shaders/PBR_Lit_Shader.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::PositionNormalUVTangentColor,
				true,  // depthEnabled
				true   // cullBackFaces
			)
			// Canonical layout per `doc/SpecShaderSystem.md` §4: Frame@0,
			// Scene@1 (lights + shadow + environment), Material@2, Object@3.
			.addFrameBindGroup()
			.addSceneBindGroup()
			.addBindGroup(bindgroup::defaults::MATERIAL, BindGroupReuse::PerObject, BindGroupType::Material)
			.addUniform(
				bindgroup::entry::defaults::MATERIAL_PROPERTIES,
				sizeof(PBRProperties),
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"textureSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addMaterialTexture(
				"baseColorTexture",
				MaterialTextureSlots::DIFFUSE,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f)
			)
			.addMaterialTexture(
				"normalTexture",
				MaterialTextureSlots::NORMAL,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.5f, 0.5f, 1.0f)
			)
			.addMaterialTexture(
				"aoTexture",
				MaterialTextureSlots::AMBIENT,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f)
			)
			.addMaterialTexture(
				"roughnessTexture",
				MaterialTextureSlots::ROUGHNESS,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f)
			)
			.addMaterialTexture(
				"metallicTexture",
				MaterialTextureSlots::METALLIC,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f)
			)
			.addMaterialTexture(
				"emissionTexture",
				MaterialTextureSlots::EMISSIVE,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f)
			)
			.addObjectBindGroup()
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createGBufferShader()
{
	// G-buffer geometry pass shader for deferred rendering.
	//
	// WGSL bind groups (see resources/g_buffer.wgsl):
	//   @group(0) Frame uniforms
	//   @group(1) Object uniforms
	//   @group(2) Material: properties (binding 0), sampler (1),
	//             baseColor (2), normal (3), roughness (4),
	//             metallic (5), ao (6)
	//
	// Fragment outputs (must stay in sync with webgpu::GBuffer):
	//   @location(0) world position + view-space depth   (RGBA16Float)
	//   @location(1) world normal   + view-space depth   (RGBA16Float)
	//   @location(2) albedo                              (RGBA8UnormSrgb)
	//   @location(3) (roughness, metallic, AO, unused)   (RGBA8Unorm)
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::GBUFFER,
				ShaderType::Lit,
				PathProvider::getResource("shaders/g_buffer.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::PositionNormalUVTangentColor,
				true,  // depthEnabled
				true   // cullBackFaces
			)
			.addFrameBindGroup()
			.addObjectBindGroup()
			// Material bind group must match PBR_Lit_Shader EXACTLY (binding order
			// and types) so the cached per-material bind group works for either
			// pass without rebuilding it.
			.addBindGroup(bindgroup::defaults::MATERIAL, BindGroupReuse::PerObject, BindGroupType::Material)
			.addUniform(
				bindgroup::entry::defaults::MATERIAL_PROPERTIES,
				sizeof(PBRProperties),
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"textureSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addMaterialTexture(
				"baseColorTexture",
				MaterialTextureSlots::DIFFUSE,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f)
			)
			.addMaterialTexture(
				"normalTexture",
				MaterialTextureSlots::NORMAL,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.5f, 0.5f, 1.0f)
			)
			.addMaterialTexture(
				"aoTexture",
				MaterialTextureSlots::AMBIENT,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f)
			)
			.addMaterialTexture(
				"roughnessTexture",
				MaterialTextureSlots::ROUGHNESS,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f)
			)
			.addMaterialTexture(
				"metallicTexture",
				MaterialTextureSlots::METALLIC,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f)
			)
			.addMaterialTexture(
				"emissionTexture",
				MaterialTextureSlots::EMISSIVE,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f)
			)
			// Declared format list - pipeline factory uses these to build the
			// fragment color targets instead of a single renderer-supplied format.
			.addColorTarget(engine::rendering::webgpu::GBuffer::FORMAT_POSITION)
			.addColorTarget(engine::rendering::webgpu::GBuffer::FORMAT_NORMAL)
			.addColorTarget(engine::rendering::webgpu::GBuffer::FORMAT_ALBEDO)
			.addColorTarget(engine::rendering::webgpu::GBuffer::FORMAT_MATERIAL)
			.addColorTarget(engine::rendering::webgpu::GBuffer::FORMAT_EMISSION)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createCompositionDeferredShader()
{
	// Canonical layout per `doc/SpecShaderSystem.md` §4: Frame@0, Scene@1
	// (lights + shadow + environment + cluster), GBuffer custom@4. Object@3
	// is reserved by convention (this pass is a fullscreen quad, no per-object
	// state) so the pipeline layout slot stays as an empty placeholder.
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::COMPOSITION_DEFERRED,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/deferred_composition.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false,
				false
			)
			.addFrameBindGroup()
			.addSceneBindGroup()
			.addBindGroupAt(4,
				"GBuffer_BindGroup",
				BindGroupReuse::Global,
				BindGroupType::Custom
			)
			.addTexture(
				"gBufferPositionTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"gBufferNormalTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"gBufferAlbedoTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"gBufferMaterialTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"gBufferEmissionTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createDebugShader()
{
	// Create debug visualization shader
	//
	// debug.wgsl structure:
	// @group(0) @binding(0) var<uniform> uFrameUniforms: FrameUniforms; (view-proj matrix)
	// @group(1) @binding(0) var<storage, read> uDebugPrimitives: array<DebugPrimitive>;

	auto shaderInfo =
		m_context.shaderFactory()
			.begin(shader::defaults::DEBUG, ShaderType::Debug, PathProvider::getResource("shaders/debug.wgsl"), "vs_main", "fs_main", VertexLayout::None, false, false)
			.addFrameBindGroup()
			.addBindGroupAt(4,
				bindgroup::defaults::DEBUG,
				BindGroupReuse::PerFrame,
				BindGroupType::Debug
			)
			.addStorageBuffer(
				"uDebugPrimitives",
				sizeof(DebugPrimitive) * 1024, // Max 1024 debug primitives (80 KB)
				true,						   // read-only
				WGPUShaderStage_Vertex | WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createFullscreenQuadShader()
{
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::FULLSCREEN_QUAD,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/fullscreen_quad.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false,
				false
			)
			.addBindGroupAt(4,
				bindgroup::defaults::FULLSCREEN_QUAD,
				BindGroupReuse::Global,
				BindGroupType::Custom
			)
			.addTexture(
				"cameraTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"cameraSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			// Group 5: post-process settings (HDR on/off + exposure).
			// Owned by CompositePass; one shared buffer for all per-camera draws
			// since the tonemap configuration is global.
			.addBindGroupAt(5,
				"PostProcess_BindGroup",
				BindGroupReuse::Global,
				BindGroupType::Custom
			)
			.addUniform(
				"postProcessUniforms",
				sizeof(glm::vec4),
				WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createSkyboxShader()
{
	// Skybox samples an equirectangular HDR map across a 36-vertex cube.
	// The WGSL writes clip.xyww so every fragment ends up at depth = 1.0 (far
	// plane). Depth test is LessEqual + read-only so the skybox only fills
	// pixels where the G-buffer left the cleared 1.0 depth value (background)
	// without stamping new depth itself.
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::SKYBOX,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/skybox.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				true,  // depthEnabled (LessEqual + read-only - see below)
				false  // cullBackFaces - inside-out cube, faces all point inward
			)
			.withDepthCompare(wgpu::CompareFunction::LessEqual)
			.withDepthWrite(false)
			.addFrameBindGroup()
			.addBindGroupAt(4,
				bindgroup::defaults::SKYBOX,
				BindGroupReuse::PerFrame,
				BindGroupType::Custom
			)
			.addUniform(
				"environmentUniforms",
				sizeof(glm::vec4),
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"environmentSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"environmentTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment
			)
			// HDR intermediate target uses RGBA16Float across the renderer.
			.addColorTarget(wgpu::TextureFormat::RGBA16Float)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createMipmapBlitShader()
{
	// Create mipmap generation shader - blits from source texture to render target
	//
	// mipmap_blit.wgsl structure:
	// @group(0) @binding(0) var srcTexture: texture_2d<f32>;
	// @group(0) @binding(1) var srcSampler: sampler;

	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::MIPMAP_BLIT,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/mipmap_blit.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false
			)
			.addBindGroupAt(4,
				bindgroup::defaults::MIPMAP_BLIT,
				BindGroupReuse::Global,
				BindGroupType::Custom
			)
			.addTexture(
				"srcTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false, // not multisampled
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"srcSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createShadowPass2DShader()
{
	// Create shadow mapping shader - renders depth from light's perspective
	//
	// shadow2d.wgsl structure:
	// @group(0) @binding(0) var<uniform> uShadow: ShadowUniforms;

	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::SHADOW_PASS_2D,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/shadow2d.wgsl"),
				"vs_shadow",
				"fs_shadow",
				engine::rendering::VertexLayout::Position
			)
			.addBindGroupAt(4,
				bindgroup::defaults::SHADOW_PASS_2D,
				BindGroupReuse::PerFrame,
				BindGroupType::ShadowPass2D
			)
			// Group 4: Shadow uniforms (light view-projection matrix)
			.addCustomUniform(
				"uShadow",
				sizeof(ShadowPass2DUniforms),
				WGPUShaderStage_Vertex
			)
			.addObjectBindGroup()
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createShadowPassCubeShader()
{
	// Create shadow mapping shader - renders depth from point light's perspective
	//
	// shadow3d.wgsl structure:
	// @group(0) @binding(0) var<uniform> uShadowCube: ShadowCubeUniforms;

	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::SHADOW_PASS_CUBE,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/shadow3d.wgsl"),
				"vs_shadow_cube",
				"fs_shadow_cube",
				engine::rendering::VertexLayout::Position
			)
			.addBindGroupAt(4,
				bindgroup::defaults::SHADOW_PASS_CUBE,
				BindGroupReuse::PerFrame,
				BindGroupType::ShadowPassCube
			)
			// Group 4: Shadow cube uniforms (light position and far plane)
			.addCustomUniform(
				"uShadowCube",
				sizeof(ShadowPassCubeUniforms),
				WGPUShaderStage_Vertex | WGPUShaderStage_Fragment
			)
			.addObjectBindGroup()
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createVisualizeDepthShader()
{
	// Create depth visualization shader for shadow map debugging
	// Converts depth texture to grayscale color texture
	//
	// visualize_depth.wgsl structure:
	// @group(0) @binding(0) var depthTexture: texture_depth_2d_array;
	// @group(0) @binding(1) var depthSampler: sampler_comparison;
	// @group(0) @binding(2) var<uniform> layer: u32;

	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::VISUALIZE_DEPTH,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/visualize_depth.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false
			)
			.addBindGroupAt(4,
				bindgroup::defaults::VISUALIZE_DEPTH,
				BindGroupReuse::Global,
				BindGroupType::Custom
			)
			.addTexture(
				"depthTexture",
				wgpu::TextureSampleType::Depth,
				wgpu::TextureViewDimension::_2DArray,
				false, // not multisampled
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"depthSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addCustomUniform(
				"layer",
				sizeof(uint32_t),
				WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createVignetteShader()
{
	// Vignette post-processing shader - darkens screen edges for cinematic effect
	// postprocess.wgsl structure:
	// @group(0) @binding(0) var inputSampler: sampler;
	// @group(0) @binding(1) var inputTexture: texture_2d<f32>;
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::VIGNETTE,
				ShaderType::Unlit,
				PathProvider::getResource("shaders/postprocess_vignette.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,  // No vertex buffers (fullscreen triangle)
				false,  // depthEnabled
				false   // cullBackFaces
			)
			// Group 4: Input texture from previous render pass
			.addBindGroupAt(4,
				bindgroup::defaults::VIGNETTE,
				BindGroupReuse::PerFrame,
				BindGroupType::Custom
			)
			.addSampler(
				"inputSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"inputTexture",
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,  // not multisampled
				WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

} // namespace engine::rendering
