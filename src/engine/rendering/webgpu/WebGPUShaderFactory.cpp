#include "engine/rendering/webgpu/WebGPUShaderFactory.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/reflection/WgslReflector.h"
#include "engine/rendering/shaders/ShaderValidator.h"
#include "engine/rendering/shaders/StructDescriptor.h"
#include "engine/rendering/shaders/WgslIncludeResolver.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

// Post-codegen validation runs in Debug/CI and is compiled out of Release (it
// is a pure cross-check; the runtime never reads its result). CI can force it
// on in a release config by defining ENGINE_SHADER_VALIDATION.
#if !defined(NDEBUG) && !defined(ENGINE_SHADER_VALIDATION)
#define ENGINE_SHADER_VALIDATION 1
#endif

namespace engine::rendering::webgpu
{

WebGPUShaderFactory::WebGPUShaderFactory(WebGPUContext &context) : m_context(context)
{
}

namespace
{
namespace refl = engine::rendering::reflection;

/// Validate the expanded WGSL against the codegen descriptors. Returns true to
/// proceed. In validating builds a mismatch is fatal (returns false) so a
/// drifted shader fails to load loudly instead of rendering wrong; in Release
/// the check is compiled out and this always returns true.
bool runShaderValidation(const std::string &expanded, const std::filesystem::path &path)
{
#ifdef ENGINE_SHADER_VALIDATION
	auto diags = engine::rendering::shaders::validateExpandedWgsl(expanded, path);
	for (const auto &d : diags)
		spdlog::error("Shader validator [{}]: {}", d.file.filename().string(), d.message);
	return diags.empty();
#else
	(void)expanded;
	(void)path;
	return true;
#endif
}

wgpu::TextureViewDimension toViewDim(refl::TextureViewDim d)
{
	switch (d)
	{
	case refl::TextureViewDim::D1:        return wgpu::TextureViewDimension::_1D;
	case refl::TextureViewDim::D2:        return wgpu::TextureViewDimension::_2D;
	case refl::TextureViewDim::D2Array:   return wgpu::TextureViewDimension::_2DArray;
	case refl::TextureViewDim::D3:        return wgpu::TextureViewDimension::_3D;
	case refl::TextureViewDim::Cube:      return wgpu::TextureViewDimension::Cube;
	case refl::TextureViewDim::CubeArray: return wgpu::TextureViewDimension::CubeArray;
	case refl::TextureViewDim::Unknown:   return wgpu::TextureViewDimension::_2D;
	}
	return wgpu::TextureViewDimension::_2D;
}

wgpu::TextureSampleType toSampleType(const std::string &s)
{
	if (s == "depth") return wgpu::TextureSampleType::Depth;
	if (s == "i32")   return wgpu::TextureSampleType::Sint;
	if (s == "u32")   return wgpu::TextureSampleType::Uint;
	return wgpu::TextureSampleType::Float;
}

/// Canonical name/type/reuse for an engine bind group, used when the descriptor
/// does not override them. The structure of the group still comes from
/// reflecting the included engine WGSL.
BindGroupMeta defaultEngineMeta(uint32_t index)
{
	using namespace engine::rendering;
	BindGroupMeta m;
	switch (index)
	{
	case 0: m.name = bindgroup::defaults::FRAME;    m.type = BindGroupType::Frame;    m.reuse = BindGroupReuse::PerFrame;  break;
	case 1: m.name = bindgroup::defaults::SCENE;    m.type = BindGroupType::Scene;    m.reuse = BindGroupReuse::PerFrame;  break;
	case 2: m.name = bindgroup::defaults::MATERIAL; m.type = BindGroupType::Material; m.reuse = BindGroupReuse::PerObject; break;
	case 3: m.name = bindgroup::defaults::OBJECT;   m.type = BindGroupType::Object;   m.reuse = BindGroupReuse::PerObject; break;
	default: break;
	}
	return m;
}

struct BuiltBinding
{
	wgpu::BindGroupLayoutEntry entry{};
	BindGroupBinding           typed{0, ""};
};

/// Translate one reflected binding into a GPU layout entry plus the engine's
/// typed metadata, applying any per-binding override. Engine groups force a
/// stable Vertex|Fragment visibility so the shared layout never varies by
/// per-shader usage; custom groups take the reflected visibility.
BuiltBinding translateBinding(const refl::Binding &rb, const BindingMeta *meta, bool engineGroup)
{
	BuiltBinding out;
	out.entry.binding    = rb.bindingIndex;
	out.entry.visibility = engineGroup
		? (WGPUShaderStage_Vertex | WGPUShaderStage_Fragment)
		: (rb.visibility != 0 ? rb.visibility : static_cast<uint32_t>(WGPUShaderStage_Fragment));

	out.typed.bindingIndex = rb.bindingIndex;
	out.typed.name         = rb.wgslName;
	out.typed.visibility   = static_cast<WGPUShaderStage>(out.entry.visibility);

	switch (rb.kind)
	{
	case refl::BindingKind::UniformBuffer:
		out.entry.buffer.type = wgpu::BufferBindingType::Uniform;
		out.entry.buffer.minBindingSize = rb.minBindingSize;
		out.typed.type = BindingType::UniformBuffer;
		out.typed.size = rb.minBindingSize;
		break;
	case refl::BindingKind::StorageBufferRO:
		out.entry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
		out.entry.buffer.minBindingSize = rb.minBindingSize;
		out.typed.type = BindingType::StorageBuffer;
		out.typed.size = rb.minBindingSize;
		break;
	case refl::BindingKind::StorageBufferRW:
		out.entry.buffer.type = wgpu::BufferBindingType::Storage;
		out.entry.buffer.minBindingSize = rb.minBindingSize;
		out.typed.type = BindingType::StorageBuffer;
		out.typed.size = rb.minBindingSize;
		break;
	case refl::BindingKind::Sampler:
		out.entry.sampler.type = (meta && meta->samplerType) ? *meta->samplerType : wgpu::SamplerBindingType::Filtering;
		out.typed.type = BindingType::Sampler;
		break;
	case refl::BindingKind::SamplerComparison:
		out.entry.sampler.type = wgpu::SamplerBindingType::Comparison;
		out.typed.type = BindingType::Sampler;
		break;
	case refl::BindingKind::Texture:
		out.entry.texture.sampleType    = (meta && meta->textureSampleType) ? *meta->textureSampleType : toSampleType(rb.texture.sampleType);
		out.entry.texture.viewDimension = toViewDim(rb.texture.viewDim);
		out.entry.texture.multisampled  = rb.texture.multisampled;
		if (meta && meta->materialSlot)
		{
			out.typed.type             = BindingType::MaterialTexture;
			out.typed.materialSlotName = *meta->materialSlot;
			out.typed.fallbackColor    = meta->fallbackColor;
		}
		else
		{
			out.typed.type = BindingType::Texture;
		}
		break;
	case refl::BindingKind::StorageTexture:
	case refl::BindingKind::Unknown:
		out.entry.buffer.type = wgpu::BufferBindingType::Uniform;
		out.typed.type = BindingType::UniformBuffer;
		break;
	}
	return out;
}

} // namespace

std::shared_ptr<WebGPUShaderInfo> WebGPUShaderFactory::buildFromDescriptor(const ShaderDescriptor &desc)
{
	std::string expanded = expandShaderSource(desc.path);
	if (expanded.empty())
	{
		spdlog::error("WebGPUShaderFactory: could not read shader '{}' ({})", desc.name, desc.path.string());
		return nullptr;
	}

	if (!runShaderValidation(expanded, desc.path))
		return nullptr;

	auto module = createShaderModuleFromWgsl(expanded, desc.path);
	if (!module)
		return nullptr;

	auto reflected = refl::reflectWgsl(expanded, desc.path.string());
	for (const auto &diag : reflected.diagnostics)
		spdlog::warn("Shader reflect [{}] line {}: {}", desc.name, diag.line, diag.message);

	auto info = std::make_shared<WebGPUShaderInfo>(
		desc.name, desc.path, desc.type, module, desc.vertexEntry, desc.fragmentEntry,
		desc.vertexLayout, engine::rendering::ShaderFeature::Flag::None, desc.enableDepth, desc.cullBackFaces
	);
	info->setDepthCompare(desc.depthCompare);
	info->setDepthWriteEnabled(desc.depthWrite);
	if (!desc.colorTargetFormats.empty())
		info->setColorTargetFormats(desc.colorTargetFormats);

	for (const auto &bg : reflected.reflection.bindGroups)
	{
		const uint32_t idx        = bg.groupIndex;
		const bool     engineRole = idx < engine::rendering::shaders::kFirstCustomBindGroupIndex;

		BindGroupMeta meta = engineRole ? defaultEngineMeta(idx) : BindGroupMeta{};
		if (auto it = desc.groups.find(idx); it != desc.groups.end())
		{
			const BindGroupMeta &o = it->second;
			if (!o.name.empty()) meta.name = o.name;
			meta.bindings = o.bindings;
			if (!engineRole) { meta.type = o.type; meta.reuse = o.reuse; }
		}
		if (meta.name.empty())
		{
			spdlog::error("Shader '{}' custom @group({}) has no metadata in its descriptor", desc.name, idx);
			continue;
		}

		const bool shared = (idx == 0 || idx == 1 || idx == 3) || meta.reuse == BindGroupReuse::Global;

		std::shared_ptr<WebGPUBindGroupLayoutInfo> layoutInfo;
		if (shared)
			layoutInfo = m_context.bindGroupFactory().getGlobalBindGroupLayout(meta.name);

		if (!layoutInfo)
		{
			std::vector<wgpu::BindGroupLayoutEntry> entries;
			std::vector<BindGroupBinding>           typed;
			entries.reserve(bg.bindings.size());
			typed.reserve(bg.bindings.size());
			for (const auto &rb : bg.bindings)
			{
				const BindingMeta *bmeta = nullptr;
				if (auto bit = meta.bindings.find(rb.bindingIndex); bit != meta.bindings.end())
					bmeta = &bit->second;
				auto built = translateBinding(rb, bmeta, engineRole);
				entries.push_back(built.entry);
				typed.push_back(built.typed);
			}

			layoutInfo = m_context.bindGroupFactory().createBindGroupLayoutInfo(
				meta.name, meta.type, meta.reuse, std::move(entries), std::move(typed)
			);
			if (shared)
				m_context.bindGroupFactory().storeGlobalBindGroupLayout(meta.name, layoutInfo);
		}

		info->addBindGroupLayout(idx, layoutInfo);
	}

	spdlog::info("WebGPUShaderFactory: built '{}' from descriptor with {} bind groups", desc.name, info->getBindGroupLayouts().size());
	return info;
}

std::string WebGPUShaderFactory::expandShaderSource(const std::filesystem::path &shaderPath)
{
	if (shaderPath.empty())
	{
		spdlog::error("WebGPUShaderFactory: no shader path specified");
		return {};
	}

	std::ifstream file(shaderPath);
	if (!file.is_open())
	{
		spdlog::error("WebGPUShaderFactory: failed to open shader file '{}'", shaderPath.string());
		return {};
	}
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	std::string shaderSource(size, ' ');
	file.seekg(0);
	file.read(shaderSource.data(), size);

	// Expand `#include "..."` before the WGSL reaches wgpu or the reflector.
	engine::rendering::shaders::WgslIncludeResolver resolver;
	auto resolved = resolver.expand(shaderSource, shaderPath);
	for (const auto &diag : resolved.errors)
		spdlog::error("Shader include error in {} (line {}): {}", diag.file.string(), diag.line, diag.message);

	return resolved.finalSource;
}

wgpu::ShaderModule WebGPUShaderFactory::createShaderModuleFromWgsl(const std::string &wgsl, const std::filesystem::path &path)
{
	wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
	shaderCodeDesc.code = wgsl.c_str();
	wgpu::ShaderModuleDescriptor shaderDesc;
	shaderDesc.nextInChain = &shaderCodeDesc.chain;
#ifdef WEBGPU_BACKEND_WGPU
	shaderDesc.hintCount = 0;
	shaderDesc.hints = nullptr;
#endif

	auto shaderModule = m_context.getDevice().createShaderModule(shaderDesc);
	if (!shaderModule)
		spdlog::error("WebGPUShaderFactory: failed to compile shader '{}'", path.string());
	return shaderModule;
}

wgpu::ShaderModule WebGPUShaderFactory::loadShaderModule(const std::filesystem::path &shaderPath)
{
	std::string expanded = expandShaderSource(shaderPath);
	if (expanded.empty())
		return nullptr;

	if (!runShaderValidation(expanded, shaderPath))
		return nullptr;

	return createShaderModuleFromWgsl(expanded, shaderPath);
}

bool WebGPUShaderFactory::reloadShader(std::shared_ptr<WebGPUShaderInfo> shaderInfo)
{
	if (!shaderInfo || shaderInfo->getPath().empty())
	{
		spdlog::error("WebGPUShaderFactory::reloadShader() - Invalid shader info or has no path");
		return false;
	}

	std::filesystem::path shaderPath = shaderInfo->getPath();
	auto shaderModule = loadShaderModule(shaderPath);

	if (!shaderModule)
	{
		spdlog::error("WebGPUShaderFactory::reloadShader() - Failed to reload shader module from '{}'", shaderPath.string());
		return false;
	}

	// Create a new immutable WebGPUShaderInfo with the reloaded module
	auto newShaderInfo = std::make_shared<WebGPUShaderInfo>(
		shaderInfo->getName(),
		shaderInfo->getPath(),
		shaderInfo->getShaderType(),
		shaderModule,
		shaderInfo->getVertexEntryPoint(),
		shaderInfo->getFragmentEntryPoint(),
		shaderInfo->getVertexLayout(),
		shaderInfo->getShaderFeatures(),
		shaderInfo->isDepthEnabled(),
		shaderInfo->isBackFaceCullingEnabled()
	);

	// Preserve post-construction state the builder populates separately - the
	// constructor only takes the basics. Dropping any of these silently breaks
	// pipelines after hot reload; e.g. losing colorTargetFormats made GBufferPass
	// try to bind a 1-attachment pipeline against the 5-attachment G-buffer pass.
	for (const auto &[groupIndex, layoutInfo] : shaderInfo->getBindGroupLayouts())
	{
		newShaderInfo->addBindGroupLayout(groupIndex, layoutInfo);
	}
	newShaderInfo->setColorTargetFormats(shaderInfo->getColorTargetFormats());
	newShaderInfo->setDepthCompare(shaderInfo->getDepthCompare());
	newShaderInfo->setDepthWriteEnabled(shaderInfo->isDepthWriteEnabled());

	return m_context.shaderRegistry().registerShader(newShaderInfo, true);
}

} // namespace engine::rendering::webgpu
