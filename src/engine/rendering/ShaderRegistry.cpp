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

ShaderRegistry::ShaderRegistry(webgpu::WebGPUContext& context)
	: m_context(context)
{
}

bool ShaderRegistry::initializeDefaultShaders()
{
	spdlog::info("Initializing default shaders...");

	// Create standard lit shader
	auto litShader = createLitShader();
	if (!litShader || !litShader->isValid())
	{
		spdlog::error("Failed to create Lit shader");
		return false;
	}
	m_defaultShaders[ShaderType::Lit] = litShader;
	spdlog::info("Created Lit shader");

	// Create debug shader
	auto debugShader = createDebugShader();
	if (!debugShader || !debugShader->isValid())
	{
		spdlog::warn("Failed to create Debug shader - debug rendering will be unavailable");
		// Don't fail initialization if debug shader fails
	}
	else
	{
		m_defaultShaders[ShaderType::Debug] = debugShader;
		spdlog::info("Created Debug shader");
	}

	// TODO: Add more default shaders as needed (Unlit, etc.)

	spdlog::info("Default shaders initialized successfully");
	return true;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::getShader(ShaderType type) const
{
	auto it = m_defaultShaders.find(type);
	if (it != m_defaultShaders.end())
	{
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::getCustomShader(const std::string& name) const
{
	auto it = m_customShaders.find(name);
	if (it != m_customShaders.end())
	{
		return it->second;
	}
	return nullptr;
}

bool ShaderRegistry::registerCustomShader(const std::string& name, std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo)
{
	if (m_customShaders.find(name) != m_customShaders.end())
	{
		spdlog::warn("Custom shader '{}' already registered", name);
		return false;
	}

	if (!shaderInfo || !shaderInfo->isValid())
	{
		spdlog::error("Cannot register invalid shader '{}'", name);
		return false;
	}

	m_customShaders[name] = shaderInfo;
	spdlog::info("Registered custom shader '{}'", name);
	return true;
}

bool ShaderRegistry::hasShader(ShaderType type) const
{
	return m_defaultShaders.find(type) != m_defaultShaders.end();
}

bool ShaderRegistry::hasCustomShader(const std::string& name) const
{
	return m_customShaders.find(name) != m_customShaders.end();
}

std::shared_ptr<webgpu::WebGPUShaderInfo> ShaderRegistry::createLitShader()
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
	
	auto shaderInfo = m_context.shaderFactory()
		.begin("lit", "vs_main", "fs_main", engine::core::PathProvider::getResource("shader.wgsl"))
		// Group 0: Frame uniforms (camera, time)
		.addFrameUniforms(0, 0)
		// Group 1: Lighting data (storage buffer with max 16 lights)
		.addLightUniforms(1, 0, 16)
		// Group 2: Object uniforms (model matrix, normal matrix)
		.addCustomUniform(
			"objectUniforms",
			sizeof(ObjectUniforms),
			2,  // group
			0,  // binding
			false,  // global - reusable buffer with dynamic writes
			WGPUShaderStage_Vertex | WGPUShaderStage_Fragment
		)
		// Group 3: Material data (properties + textures)
		.addCustomUniform(
			"materialUniforms",
			sizeof(Material::MaterialProperties),
			3,  // group
			0,  // binding
			false,  // global - reusable buffer with dynamic writes (like object uniforms)
			WGPUShaderStage_Fragment
		)
		.addSampler(
			"textureSampler",
			3,  // group
			1,  // binding
			wgpu::SamplerBindingType::Filtering,
			WGPUShaderStage_Fragment
		)
		.addTexture(
			"baseColorTexture",
			MaterialTextureSlots::ALBEDO,  // material slot name
			3,  // group
			2,  // binding
			wgpu::TextureSampleType::Float,
			wgpu::TextureViewDimension::_2D,
			false,  // not multisampled
			WGPUShaderStage_Fragment
		)
		.addTexture(
			"normalTexture",
			MaterialTextureSlots::NORMAL,  // material slot name
			3,  // group
			3,  // binding
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
	
	auto shaderInfo = m_context.shaderFactory()
		.begin("debug", "vs_main", "fs_main", engine::core::PathProvider::getResource("debug.wgsl"))
		.addFrameUniforms(0, 0)  // View-projection matrix from frame uniforms
		.addStorageBuffer(
			"uDebugPrimitives",
			sizeof(DebugPrimitive) * 1024,  // Max 1024 debug primitives (80 KB)
			1,  // group 1 (separate from frame uniforms)
			0,  // binding 0
			true,  // read-only
			false,  // not global - per-frame primitives
			WGPUShaderStage_Vertex | WGPUShaderStage_Fragment
		)
		.build();

	return shaderInfo;
}

} // namespace engine::rendering
