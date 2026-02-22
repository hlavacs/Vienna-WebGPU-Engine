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

	auto pbrShader = createPBRShader();
	if (!pbrShader || !pbrShader->isValid())
	{
		spdlog::error("Failed to create PBR shader");
		return false;
	}
	registerShader(pbrShader);

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

	// Reload default shaders using the WebGPUShaderInfo overload (less reloading)#
	auto shaders = m_shaders; // Copy to avoid modification during iteration
	for (auto &[name, shaderInfo] : shaders)
	{
		if (shaderInfo)
		{
			spdlog::info("Reloading shader '{}'", name);
			m_context.shaderFactory().reloadShader(shaderInfo);
		}
	}

	spdlog::info("Shader reload complete");
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::getShader(const std::string &name) const
{
	auto it = m_shaders.find(name);
	if (it != m_shaders.end())
	{
		return it->second;
	}
	return nullptr;
}

bool ShaderRegistry::registerShader(std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo, bool replaceIfExists)
{
	if (!replaceIfExists && m_shaders.find(shaderInfo->getName()) != m_shaders.end())
	{
		spdlog::warn("Shader '{}' already registered", shaderInfo->getName());
		return false;
	}

	if (!shaderInfo || !shaderInfo->isValid())
	{
		spdlog::error("Cannot register invalid shader '{}'", shaderInfo->getName());
		return false;
	}

	m_shaders[shaderInfo->getName()] = shaderInfo;
	if (replaceIfExists)
	{
		spdlog::info("Replaced existing shader '{}'", shaderInfo->getName());
	}
	else
	{
		spdlog::info("Registered shader '{}'", shaderInfo->getName());
	}
	return true;
}

bool ShaderRegistry::unregisterShader(const std::string &name)
{
	auto it = m_shaders.find(name);
	if (it != m_shaders.end())
	{
		m_shaders.erase(it);
		spdlog::info("Unregistered shader '{}'", name);
		return true;
	}
	spdlog::warn("Shader '{}' not found for unregistration", name);
	return false;
}

void ShaderRegistry::unregisterAll()
{
	spdlog::info("Unregistering all {} shaders", m_shaders.size());
	m_shaders.clear();
}

bool ShaderRegistry::hasShader(const std::string &name) const
{
	return m_shaders.find(name) != m_shaders.end();
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
	auto shaderInfo =
		m_context.shaderFactory()
			.begin(
				shader::defaults::PBR,
				ShaderType::Lit,
				PathProvider::getResource("PBR_Lit_Shader.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::PositionNormalUVTangentColor,
				true,  // depthEnabled
				false, // blendEnabled
				true   // cullBackFaces
			)
			// Group 0: Frame uniforms (camera, time)
			.addFrameBindGroup()
			// Group 1: Lighting data (storage buffer with max 16 lights)
			.addLightBindGroup()
			// Group 2: Object uniforms (model matrix, normal matrix)
			.addObjectBindGroup()
			// Group 3: Material data (properties + textures)
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
				MaterialTextureSlots::DIFFUSE, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f) // default white for base color
			)
			.addMaterialTexture(
				"normalTexture",
				MaterialTextureSlots::NORMAL, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.5f, 0.5f, 1.0f) // default normal map color
			)
			.addMaterialTexture(
				"aoTexture",
				MaterialTextureSlots::AMBIENT, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f) // default white for AO
			)
			.addMaterialTexture(
				"roughnessTexture",
				MaterialTextureSlots::ROUGHNESS, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f) // default white for roughness
			)
			.addMaterialTexture(
				"metallicTexture",
				MaterialTextureSlots::METALLIC, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f) // default black for metallic
			)
			.addMaterialTexture(
				"emissionTexture",
				MaterialTextureSlots::EMISSIVE, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f) // default black for emission
			)
			// Group 4: Shadow mapping (sampler, 2D array, cube array, storage buffers)
			.addShadowBindGroup()
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
			.begin(shader::defaults::DEBUG, ShaderType::Debug, PathProvider::getResource("debug.wgsl"), "vs_main", "fs_main", VertexLayout::None, false, false, false)
			.addFrameBindGroup()
			.addBindGroup(
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
				PathProvider::getResource("fullscreen_quad.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false,
				false,
				false
			)
			.addBindGroup(
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
				PathProvider::getResource("mipmap_blit.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false
			)
			.addBindGroup(
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
				PathProvider::getResource("shadow2d.wgsl"),
				"vs_shadow",
				"fs_shadow",
				engine::rendering::VertexLayout::Position
			)
			.addBindGroup(
				bindgroup::defaults::SHADOW_PASS_2D,
				BindGroupReuse::PerFrame,
				BindGroupType::ShadowPass2D
			)
			// Group 0: Shadow uniforms (light view-projection matrix)
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
				PathProvider::getResource("shadow3d.wgsl"),
				"vs_shadow_cube",
				"fs_shadow_cube",
				engine::rendering::VertexLayout::Position
			)
			.addBindGroup(
				bindgroup::defaults::SHADOW_PASS_CUBE,
				BindGroupReuse::PerFrame,
				BindGroupType::ShadowPassCube
			)
			// Group 0: Shadow cube uniforms (light position and far plane)
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
				PathProvider::getResource("visualize_depth.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,
				false
			)
			.addBindGroup(
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
				PathProvider::getResource("postprocess_vignette.wgsl"),
				"vs_main",
				"fs_main",
				VertexLayout::None,  // No vertex buffers (fullscreen triangle)
				false,  // depthEnabled
				false,  // blendEnabled
				false   // cullBackFaces
			)
			// Group 0: Input texture from previous render pass
			.addBindGroup(
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
