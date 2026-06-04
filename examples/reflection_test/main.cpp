// Reflector smoke test. Loads every .wgsl in resources/ and prints what the
// reflector derived. No assertions yet - the goal of Phase 0 is to look at the
// output and confirm it matches what ShaderRegistry hand-builds.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/core/PathProvider.h"
#include "engine/rendering/reflection/ShaderReflection.h"
#include "engine/rendering/reflection/WgslReflector.h"

using namespace engine::rendering::reflection;

namespace
{
const char *stageName(ShaderStage s)
{
	switch (s)
	{
		case ShaderStage::Vertex:   return "Vertex";
		case ShaderStage::Fragment: return "Fragment";
		case ShaderStage::Compute:  return "Compute";
	}
	return "?";
}

const char *kindName(BindingKind k)
{
	switch (k)
	{
		case BindingKind::UniformBuffer:     return "UniformBuffer";
		case BindingKind::StorageBufferRO:   return "StorageBufferRO";
		case BindingKind::StorageBufferRW:   return "StorageBufferRW";
		case BindingKind::Sampler:           return "Sampler";
		case BindingKind::SamplerComparison: return "SamplerComparison";
		case BindingKind::Texture:           return "Texture";
		case BindingKind::StorageTexture:    return "StorageTexture";
		default:                             return "Unknown";
	}
}

const char *viewDimName(TextureViewDim d)
{
	switch (d)
	{
		case TextureViewDim::D1:        return "1D";
		case TextureViewDim::D2:        return "2D";
		case TextureViewDim::D2Array:   return "2DArray";
		case TextureViewDim::D3:        return "3D";
		case TextureViewDim::Cube:      return "Cube";
		case TextureViewDim::CubeArray: return "CubeArray";
		default:                        return "?";
	}
}

const char *reuseName(BindGroupReuse r)
{
	switch (r)
	{
		case BindGroupReuse::Global:      return "Global";
		case BindGroupReuse::PerFrame:    return "PerFrame";
		case BindGroupReuse::PerCamera:   return "PerCamera";
		case BindGroupReuse::PerObject:   return "PerObject";
		case BindGroupReuse::PerMaterial: return "PerMaterial";
	}
	return "?";
}

const char *roleName(BindGroupRole r)
{
	switch (r)
	{
		case BindGroupRole::Frame:    return "Frame";
		case BindGroupRole::Scene:    return "Scene";
		case BindGroupRole::Material: return "Material";
		case BindGroupRole::Object:   return "Object";
		case BindGroupRole::Custom:   return "Custom";
	}
	return "?";
}

std::string readFile(const std::filesystem::path &p)
{
	std::ifstream in(p, std::ios::binary);
	if (!in) return {};
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

void dump(const ShaderReflection &r)
{
	std::cout << "  Entry points: ";
	if (r.entryPoints.empty()) std::cout << "(none)";
	for (const auto &ep : r.entryPoints)
	{
		std::cout << stageName(ep.stage) << ":" << ep.name;
		if (ep.stage == ShaderStage::Compute)
			std::cout << "(" << ep.workgroupSize[0] << "," << ep.workgroupSize[1] << "," << ep.workgroupSize[2] << ")";
		std::cout << " ";
	}
	std::cout << "\n";

	for (const auto &bg : r.bindGroups)
	{
		std::cout << "  @group(" << bg.groupIndex << ") "
				  << bg.name << " [" << reuseName(bg.reuse) << ", " << roleName(bg.role) << "]\n";
		for (const auto &b : bg.bindings)
		{
			std::cout << "    @binding(" << b.bindingIndex << ") "
					  << b.wgslName << "  " << kindName(b.kind);
			if (b.kind == BindingKind::Texture || b.kind == BindingKind::StorageTexture)
				std::cout << " <" << viewDimName(b.texture.viewDim) << "," << b.texture.sampleType << ">";
			if (!b.structLayout.name.empty())
				std::cout << "  " << b.structLayout.name << " (" << b.structLayout.sizeBytes << " B)";
			std::cout << "\n";
		}
	}

	if (!r.fragmentOutputs.empty())
	{
		auto kindName = [](ComponentKind k) {
			switch (k) {
				case ComponentKind::Float: return "f";
				case ComponentKind::Sint:  return "i";
				case ComponentKind::Uint:  return "u";
			}
			return "?";
		};
		std::cout << "  Fragment outputs: ";
		for (const auto &f : r.fragmentOutputs)
			std::cout << "@" << f.location << "=" << kindName(f.componentKind) << f.componentCount << " ";
		std::cout << "\n";
	}
	if (r.hints.depthCompare)  std::cout << "  Depth compare: " << *r.hints.depthCompare << "\n";
	if (r.hints.depthWrite)    std::cout << "  Depth write:   " << (*r.hints.depthWrite ? "true" : "false") << "\n";
	if (r.hints.cullMode)      std::cout << "  Cull mode:     " << *r.hints.cullMode << "\n";

	if (!r.structs.empty())
	{
		std::cout << "  Structs:\n";
		for (const auto &s : r.structs)
		{
			std::cout << "    " << s.name << " size=" << s.sizeBytes << " align=" << s.alignBytes
					  << " fields=" << s.fields.size();
			if (s.hasRuntimeArray) std::cout << " (+runtime array)";
			std::cout << "\n";
		}
	}
}
} // namespace

int main()
{
#if defined(DEBUG_ROOT_DIR) && defined(ASSETS_ROOT_DIR)
	engine::core::PathProvider::initialize(ASSETS_ROOT_DIR, DEBUG_ROOT_DIR);
#elif defined(DEBUG_ROOT_DIR)
	engine::core::PathProvider::initialize("", DEBUG_ROOT_DIR);
#else
	engine::core::PathProvider::initialize();
#endif
	std::cout << "Resource root: " << engine::core::PathProvider::getResourceRoot().string() << "\n\n";

	const std::vector<std::string> files = {
		"light_clustering.wgsl",
		"g_buffer.wgsl",
		"deferred_composition.wgsl",
		"PBR_Lit_Shader.wgsl",
		"fullscreen_quad.wgsl",
		"skybox.wgsl",
	};

	int errors = 0;
	for (const auto &name : files)
	{
		std::cout << "==== " << name << " ====\n";
		auto path = engine::core::PathProvider::getResource(name);
		std::string src = readFile(path);
		if (src.empty())
		{
			std::cout << "  (file not found at " << path.string() << ")\n";
			++errors;
			continue;
		}
		auto result = reflectWgsl(src, path.string());
		dump(result.reflection);
		for (const auto &d : result.diagnostics)
		{
			std::cout << "  ! " << d.line << ":" << d.column << " " << d.message << "\n";
			++errors;
		}
		std::cout << "\n";
	}
	return errors == 0 ? 0 : 1;
}
