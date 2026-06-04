#include "engine/rendering/shaders/EngineCoreRegistry.h"

#include <cctype>
#include <map>
#include <sstream>
#include <string_view>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/ClusterManager.h"
#include "engine/rendering/shaders/ShaderCodegen.h"

namespace engine::rendering::shaders::core
{

namespace
{

std::vector<EngineBinding> &mutableEntries()
{
	static std::vector<EngineBinding> v;
	return v;
}

std::vector<EngineStructOnly> &mutableStructOnlyEntries()
{
	static std::vector<EngineStructOnly> v;
	return v;
}

std::vector<EngineBufferWrapper> &mutableBufferWrapperEntries()
{
	static std::vector<EngineBufferWrapper> v;
	return v;
}

std::vector<GeneratedBindingRecord> &mutableValidatorView()
{
	static std::vector<GeneratedBindingRecord> v;
	return v;
}

/// Insert `_` before each uppercase letter that follows a lowercase letter,
/// or before each uppercase letter that follows another uppercase letter that
/// is itself followed by a lowercase letter. Then lowercase everything.
///
/// `FrameUniforms` → `frame_uniforms`
/// `LightsBuffer`  → `lights_buffer`
/// `PBRProperties` → `pbr_properties`  (PBR run held together by the second rule)
/// `IORValue`      → `ior_value`
std::string snakeCase(std::string_view input)
{
	std::string out;
	out.reserve(input.size() + 4);
	for (size_t i = 0; i < input.size(); ++i)
	{
		const char c = input[i];
		if (std::isupper(static_cast<unsigned char>(c)) && i > 0)
		{
			const char prev = input[i - 1];
			const char next = (i + 1 < input.size()) ? input[i + 1] : '\0';
			const bool prevIsLower = std::islower(static_cast<unsigned char>(prev));
			const bool acronymEdge = std::isupper(static_cast<unsigned char>(prev)) &&
			                         std::islower(static_cast<unsigned char>(next));
			if (prevIsLower || acronymEdge)
			{
				out.push_back('_');
			}
		}
		out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	}
	return out;
}

/// Strip a trailing identifier suffix if present (case-sensitive). Returns
/// the head of @p s up to but not including @p suffix; returns the full
/// string when there's no match.
std::string_view stripSuffix(std::string_view s, std::string_view suffix)
{
	if (s.size() >= suffix.size() &&
	    s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
	{
		return s.substr(0, s.size() - suffix.size());
	}
	return s;
}

/// Drop any `ns::` qualification from @p s. Engine struct names occasionally
/// arrive fully qualified through the macros (e.g.
/// `engine::rendering::FrameUniforms`); the convention helpers only care
/// about the unqualified tail.
std::string_view unqualified(std::string_view s)
{
	const auto pos = s.rfind("::");
	if (pos != std::string_view::npos) return s.substr(pos + 2);
	return s;
}

} // namespace

std::string deriveVarName(const char *structName)
{
	std::string_view name = unqualified(structName);
	name                  = stripSuffix(name, "Uniforms");
	name                  = stripSuffix(name, "Buffer");
	std::string snake     = snakeCase(name);
	return "u_" + snake;
}

std::string deriveFileName(const char *structName)
{
	return snakeCase(unqualified(structName)) + ".wgsl";
}

void EngineCoreRegistry::registerBinding(EngineBinding spec)
{
	mutableEntries().push_back(std::move(spec));
}

const std::vector<EngineBinding> &EngineCoreRegistry::entries()
{
	return mutableEntries();
}

void EngineCoreRegistry::registerStructOnly(EngineStructOnly spec)
{
	mutableStructOnlyEntries().push_back(std::move(spec));
}

const std::vector<EngineStructOnly> &EngineCoreRegistry::structOnlyEntries()
{
	return mutableStructOnlyEntries();
}

void EngineCoreRegistry::registerBufferWrapper(EngineBufferWrapper spec)
{
	mutableBufferWrapperEntries().push_back(std::move(spec));
}

const std::vector<EngineBufferWrapper> &EngineCoreRegistry::bufferWrapperEntries()
{
	return mutableBufferWrapperEntries();
}

const std::vector<GeneratedBindingRecord> &EngineCoreRegistry::validatorView()
{
	return mutableValidatorView();
}

uint32_t EngineCoreRegistry::regenerateAll()
{
	uint32_t changed = 0;

	// Group entries by emitted file. One file may carry multiple bindings as
	// long as they share an engine bind group (e.g. several Scene-group
	// uniforms can sit in one scene.wgsl). Today each binding writes its own
	// file; the grouping is forward-compatible with that consolidation.
	std::map<std::string, GenEngineGroupSpec> byFile;
	for (const auto &e : entries())
	{
		auto &g    = byFile[e.generatedFile];
		g.group    = e.group;
		GenBindingSpec b{};
		b.bindingIndex = e.bindingIndex;
		b.kind         = e.kind;
		b.wgslVarName  = e.wgslVarName;
		b.structRef    = e.descriptor;
		g.bindings.push_back(std::move(b));
	}

	for (const auto &[file, group] : byFile)
	{
		// Source label: the struct(s) anchored by this file. Joins names with
		// "+" so diagnostics keep their meaning when several structs share a
		// file ("LightsBuffer+ShadowUniform").
		std::string sourceLabel;
		for (const auto &b : group.bindings)
		{
			if (!sourceLabel.empty()) sourceLabel.push_back('+');
			if (b.structRef) sourceLabel.append(b.structRef->name);
		}

		const std::string body = ShaderCodegen::emitGeneratedEngineGroupFile(sourceLabel, group);
		const auto        path = engine::core::PathProvider::getResource("shaders/core/" + file);
		if (ShaderCodegen::writeIfChanged(path, body))
		{
			spdlog::info("Codegen wrote {}", path.string());
			++changed;
		}
	}

	// Rebuild the validator-facing view from the same entries. Stable order
	// (entry registration order) so diagnostics are deterministic.
	auto &view = mutableValidatorView();
	view.clear();
	view.reserve(entries().size());
	for (const auto &e : entries())
	{
		view.push_back({e.descriptor, e.group, e.bindingIndex, e.wgslVarName});
	}

	// Buffer-wrapper entries: header struct + runtime array of an element
	// struct. Emits the include for the element + the header with the
	// trailing `<field>: array<Element>` appended. Each wrapper also lands in
	// the validator view so any shader that binds the wrapper at a non-
	// canonical slot fails loud; the `hasTrailingRuntimeArray` flag tells
	// compareStruct to ignore the runtime array the C++ descriptor can't
	// model.
	for (const auto &w : bufferWrapperEntries())
	{
		if (!w.header || !w.element) continue;
		std::ostringstream body;
		// Include the element struct's generated file. We don't track the
		// exact filename for the element here; the consuming shader is
		// expected to pull its own #includes. Instead derive it from the
		// element's WGSL name + the snake_case convention.
		body << "#include \"engine://core/"
		     << deriveFileName(w.element->name.c_str())
		     << "\"\n\n";
		std::string structBody;
		ShaderCodegen::emitStructWithRuntimeArray(*w.header, w.arrayFieldName, w.element->name, structBody);
		body << structBody;
		const std::string contents = ShaderCodegen::emitGeneratedFile(
			w.header->name + " (header + runtime array<" + w.element->name + ">)",
			body.str());
		const auto path = engine::core::PathProvider::getResource("shaders/core/" + w.generatedFile);
		if (ShaderCodegen::writeIfChanged(path, contents))
		{
			spdlog::info("Codegen wrote {}", path.string());
			++changed;
		}

		GeneratedBindingRecord rec{};
		rec.descriptor              = w.header;
		rec.group                   = w.group;
		rec.bindingIndex            = w.bindingIndex;
		rec.wgslVarName             = w.wgslVarName;
		rec.hasTrailingRuntimeArray = true;
		view.push_back(std::move(rec));
	}

	{
		std::ostringstream body;
		body << "// {offset, count} per cluster. Pairs with `array<u32>` light index pool.\n"
		     << "struct ClusterLightList {\n"
		     << "\toffset: u32,\n"
		     << "\tcount: u32,\n"
		     << "}\n";
		const std::string contents = ShaderCodegen::emitGeneratedFile(
			"ClusterLightList (composition + cluster compute share)",
			body.str());
		const auto path = engine::core::PathProvider::getResource("shaders/core/cluster_light_list.wgsl");
		if (ShaderCodegen::writeIfChanged(path, contents))
		{
			spdlog::info("Codegen wrote {}", path.string());
			++changed;
		}
	}

	// Scene bindings: emit a single core/scene_bindings.wgsl with the struct
	// includes + the 10 @group(1) @binding(N) declarations the consolidated
	// Scene layout owns. Consumers include this one file instead of
	// re-typing the same 10 declarations every time.
	{
		std::ostringstream body;
		body << "#include \"engine://core/lights_buffer.wgsl\"\n"
		     << "#include \"engine://core/shadow_uniform.wgsl\"\n"
		     << "#include \"engine://core/environment_uniforms.wgsl\"\n"
		     << "#include \"engine://core/cluster_light_list.wgsl\"\n"
		     << "\n"
		     << "@group(1) @binding(0) var<storage, read> u_lights:                 LightsBuffer;\n"
		     << "@group(1) @binding(1) var                shadow_sampler:           sampler_comparison;\n"
		     << "@group(1) @binding(2) var                shadow_maps_2d:           texture_depth_2d_array;\n"
		     << "@group(1) @binding(3) var                shadow_maps_cube:         texture_depth_cube_array;\n"
		     << "@group(1) @binding(4) var<storage, read> u_shadows:                array<ShadowUniform>;\n"
		     << "@group(1) @binding(5) var<uniform>       u_environment:            EnvironmentUniforms;\n"
		     << "@group(1) @binding(6) var                environment_sampler:      sampler;\n"
		     << "@group(1) @binding(7) var                environment_texture:      texture_2d<f32>;\n"
		     << "@group(1) @binding(8) var<storage, read> u_cluster_grid:           array<ClusterLightList>;\n"
		     << "@group(1) @binding(9) var<storage, read> u_cluster_light_indices:  array<u32>;\n";
		const std::string contents = ShaderCodegen::emitGeneratedFile(
			"Scene_BindGroup (struct includes + binding declarations)",
			body.str());
		const auto path = engine::core::PathProvider::getResource("shaders/core/scene_bindings.wgsl");
		if (ShaderCodegen::writeIfChanged(path, contents))
		{
			spdlog::info("Codegen wrote {}", path.string());
			++changed;
		}
	}

	// Struct-only entries: emit just the WGSL struct (no @group/@binding line)
	// for structs that anchor a bind group at a non-canonical @binding(N) slot.
	// Consuming shaders include this and hand-declare the actual binding.
	for (const auto &s : structOnlyEntries())
	{
		if (!s.descriptor) continue;
		std::string body;
		ShaderCodegen::emitStruct(*s.descriptor, body);
		const std::string contents = ShaderCodegen::emitGeneratedFile(s.descriptor->name, std::move(body));
		const auto        path     = engine::core::PathProvider::getResource("shaders/core/" + s.generatedFile);
		if (ShaderCodegen::writeIfChanged(path, contents))
		{
			spdlog::info("Codegen wrote {}", path.string());
			++changed;
		}
	}

	// Engine-side constants: emit cluster-related dimensions + light counts
	// into core/constants_cluster.wgsl. C++ is the single source of truth
	// (ClusterManager) — both light_clustering.wgsl and deferred_composition.wgsl
	// pick the values up via #include.
	{
		using engine::rendering::ClusterManager;
		std::ostringstream body;
		body << "const CLUSTER_GRID_DIM_X: u32 = " << ClusterManager::CLUSTER_GRID_DIM_X << "u;\n"
		     << "const CLUSTER_GRID_DIM_Y: u32 = " << ClusterManager::CLUSTER_GRID_DIM_Y << "u;\n"
		     << "const CLUSTER_GRID_DIM_Z: u32 = " << ClusterManager::CLUSTER_GRID_DIM_Z << "u;\n"
		     << "const CLUSTER_GRID_TOTAL: u32 = CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y * CLUSTER_GRID_DIM_Z;\n"
		     << "const MAX_LIGHTS_PER_CLUSTER: u32 = " << ClusterManager::MAX_LIGHTS_PER_CLUSTER << "u;\n"
		     << "\n"
		     << "// View-space z extents for the log-Z cluster mapping. Kept here so\n"
		     << "// compute + composition stay in lockstep; if a CameraNode ever needs\n"
		     << "// per-camera extents this becomes a per-pipeline override.\n"
		     << "const CLUSTER_Z_NEAR: f32 = 0.1;\n"
		     << "const CLUSTER_Z_FAR:  f32 = 1000.0;\n";
		const std::string file = ShaderCodegen::emitGeneratedFile("ClusterManager (C++ constants)", body.str());
		const auto        path = engine::core::PathProvider::getResource("shaders/core/constants_cluster.wgsl");
		if (ShaderCodegen::writeIfChanged(path, file))
		{
			spdlog::info("Codegen wrote {}", path.string());
			++changed;
		}
	}

	return changed;
}

} // namespace engine::rendering::shaders::core
