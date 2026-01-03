#include "engine/rendering/ShaderRegistry.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering
{

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

	spdlog::info("Default shaders initialized successfully");
	return true;
}

void ShaderRegistry::reloadAllShaders()
{
	spdlog::info("Reloading all shaders in ShaderRegistry...");

	// Reload default shaders
	for (auto &[name, shaderInfo] : m_shaders)
	{
		if (shaderInfo)
		{
			auto name = shaderInfo->getName();
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

bool ShaderRegistry::registerShader(std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo)
{
	if (m_shaders.find(shaderInfo->getName()) != m_shaders.end())
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
	spdlog::info("Registered custom shader '{}'", shaderInfo->getName());
	return true;
}

bool ShaderRegistry::hasShader(const std::string &name) const
{
	return m_shaders.find(name) != m_shaders.end();
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createPBRShader()
{
	// Create the standard PBR lit shader matching shader.wgsl
	//
	// shader.wgsl structure:
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

	auto shaderInfo =
		m_context.shaderFactory()
			.begin(shader::default ::PBR, ShaderType::Lit, "vs_main", "fs_main", engine::core::PathProvider::getResource("shader.wgsl"))
			// Group 0: Frame uniforms (camera, time)
			.addFrameUniforms(0)
			// Group 1: Lighting data (storage buffer with max 16 lights)
			.addLightUniforms(1, 16)
			// Group 2: Object uniforms (model matrix, normal matrix)
			.addObjectUniforms(2)
			// Group 3: Material data (properties + textures)
			.addCustomUniform(
				"materialUniforms",
				sizeof(PBRProperties),
				3, // group
				0, // binding
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"textureSampler",
				3, // group
				1, // binding
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addTexture(
				"baseColorTexture",
				MaterialTextureSlots::DIFFUSE, // material slot name
				3,							   // group
				2,							   // binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false, // not multisampled
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f) // default white for base color
			)
			.addTexture(
				"normalTexture",
				MaterialTextureSlots::NORMAL, // material slot name
				3,							  // group
				3,							  // binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment,
				glm::vec3(0.5f, 0.5f, 1.0f) // default normal map color
			)
			.addTexture(
				"aoTexture",
				MaterialTextureSlots::AMBIENT, // material slot name
				3,							   // group
				4,							   // binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f) // default white for AO
			)
			.addTexture(
				"roughnessTexture",
				MaterialTextureSlots::ROUGHNESS, // material slot name
				3,								 // group
				5,								 // binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment,
				glm::vec3(1.0f, 1.0f, 1.0f) // default white for roughness
			)
			.addTexture(
				"metallicTexture",
				MaterialTextureSlots::METALLIC, // material slot name
				3,								// group
				6,								// binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f) // default black for metallic
			)
			.addTexture(
				"emissionTexture",
				MaterialTextureSlots::EMISSIVE, // material slot name
				3,								// group
				7,								// binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment,
				glm::vec3(0.0f, 0.0f, 0.0f) // default black for emission
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
			.begin(shader::default::DEBUG, ShaderType::Debug, "vs_main", "fs_main", engine::core::PathProvider::getResource("debug.wgsl"))
			.addFrameUniforms(0) // View-projection matrix from frame uniforms
			.addStorageBuffer(
				"uDebugPrimitives",
				sizeof(DebugPrimitive) * 1024, // Max 1024 debug primitives (80 KB)
				1,							   // group 1 (separate from frame uniforms)
				0,							   // binding 0
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
				shader::default ::FULLSCREEN_QUAD,
				ShaderType::Unlit,
				"vs_main",
				"fs_main",
				engine::core::PathProvider::getResource("fullscreen_quad.wgsl")
			)
			.setVertexLayout(engine::rendering::VertexLayout::None)
			.disableDepth()
			.addTexture(
				"cameraTexture",
				"",
				0,
				0,
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false,
				WGPUShaderStage_Fragment,
				std::nullopt
			)
			.addSampler(
				"cameraSampler",
				0,
				1,
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
				shader::default::MIPMAP_BLIT,
				ShaderType::Unlit,
				"vs_main",
				"fs_main",
				engine::core::PathProvider::getResource("mipmap_blit.wgsl")
			)
			.setVertexLayout(engine::rendering::VertexLayout::None)
			.addTexture(
				"srcTexture",
				"", // No material slot
				0,	// group
				0,	// binding
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				false, // not multisampled
				WGPUShaderStage_Fragment,
				std::nullopt // No default color
			)
			.addSampler(
				"srcSampler",
				0, // group
				1, // binding
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.build();

	return shaderInfo;
}

} // namespace engine::rendering
