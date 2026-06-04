#include "engine/rendering/shaders/ShaderValidator.h"

#include <sstream>
#include <unordered_map>

#include "engine/rendering/reflection/ShaderReflection.h"
#include "engine/rendering/reflection/WgslReflector.h"
#include "engine/rendering/shaders/EngineStructDescriptors.h"
#include "engine/rendering/shaders/WgslTypes.h"

namespace engine::rendering::shaders
{

namespace
{

/// Map the codegen's compact `WgslType` enum to the reflector's `WgslPrimitive`
/// enum. Only the primitive subset the codegen emits is covered; anything else
/// is "not a match" and produces a diagnostic.
bool primitiveMatches(reflection::WgslPrimitive p, WgslType s)
{
	switch (s)
	{
		case WgslType::F32:  return p == reflection::WgslPrimitive::F32;
		case WgslType::I32:  return p == reflection::WgslPrimitive::I32;
		case WgslType::U32:  return p == reflection::WgslPrimitive::U32;
		case WgslType::Vec2: return p == reflection::WgslPrimitive::Vec2F;
		case WgslType::Vec3: return p == reflection::WgslPrimitive::Vec3F;
		case WgslType::Vec4: return p == reflection::WgslPrimitive::Vec4F;
		case WgslType::Mat4: return p == reflection::WgslPrimitive::Mat4x4F;
	}
	return false;
}

const char *primitiveName(reflection::WgslPrimitive p)
{
	switch (p)
	{
		case reflection::WgslPrimitive::F32:     return "f32";
		case reflection::WgslPrimitive::I32:     return "i32";
		case reflection::WgslPrimitive::U32:     return "u32";
		case reflection::WgslPrimitive::Bool:    return "bool";
		case reflection::WgslPrimitive::Vec2F:   return "vec2<f32>";
		case reflection::WgslPrimitive::Vec3F:   return "vec3<f32>";
		case reflection::WgslPrimitive::Vec4F:   return "vec4<f32>";
		case reflection::WgslPrimitive::Mat4x4F: return "mat4x4<f32>";
		default:                                 return "(other)";
	}
}

void compareStruct(const reflection::StructLayout &found,
                   const GeneratedBindingRecord &record,
                   const std::filesystem::path &shaderPath,
                   std::vector<ValidationDiagnostic> &diags)
{
	const StructDescriptor &expected = *record.descriptor;

	// Buffer wrappers carry a trailing `array<Element>` after the header fields
	// that the C++ descriptor doesn't model (it's runtime-sized). The reflector
	// reports the header sizeBytes as the fixed portion before the array, so
	// the size compare still works; only the field-count compare needs to give
	// the trailing array a free pass.
	const size_t expectedFieldCount =
		record.hasTrailingRuntimeArray ? expected.fields.size() + 1 : expected.fields.size();

	if (found.sizeBytes != expected.sizeBytes)
	{
		std::ostringstream os;
		os << "struct " << found.name << ": WGSL size " << found.sizeBytes
		   << " != C++ descriptor size " << expected.sizeBytes;
		diags.push_back({os.str(), shaderPath, 0});
	}

	if (found.fields.size() != expectedFieldCount)
	{
		std::ostringstream os;
		os << "struct " << found.name << ": WGSL has " << found.fields.size()
		   << " fields, C++ descriptor has " << expectedFieldCount;
		diags.push_back({os.str(), shaderPath, 0});
		return; // Field-by-field comparison only makes sense at matching counts.
	}

	// Compare only the header fields the descriptor knows about; the trailing
	// runtime array (if any) intentionally has no descriptor counterpart.
	for (size_t i = 0; i < expected.fields.size(); ++i)
	{
		const auto &ff = found.fields[i];
		const auto &ef = expected.fields[i];

		if (ff.name != ef.name)
		{
			std::ostringstream os;
			os << "struct " << found.name << " field[" << i << "]: WGSL name '"
			   << ff.name << "' != C++ name '" << ef.name << "'";
			diags.push_back({os.str(), shaderPath, 0});
		}

		if (!primitiveMatches(ff.type.primitive, ef.type))
		{
			std::ostringstream os;
			os << "struct " << found.name << " field '" << ff.name
			   << "': WGSL type " << primitiveName(ff.type.primitive)
			   << " != C++ type " << wgslTypeName(ef.type);
			diags.push_back({os.str(), shaderPath, 0});
		}

		if (ff.offsetBytes != ef.offsetBytes)
		{
			std::ostringstream os;
			os << "struct " << found.name << " field '" << ff.name
			   << "': WGSL offset " << ff.offsetBytes
			   << " != C++ offset " << ef.offsetBytes;
			diags.push_back({os.str(), shaderPath, 0});
		}
	}
}

void compareBindingSite(const reflection::Binding &found,
                        uint32_t actualGroupIndex,
                        const GeneratedBindingRecord &expected,
                        const std::filesystem::path &shaderPath,
                        std::vector<ValidationDiagnostic> &diags)
{
	const uint32_t expectedGroupIndex = canonicalGroupIndex(expected.group);

	if (actualGroupIndex != expectedGroupIndex)
	{
		std::ostringstream os;
		os << "binding for " << expected.descriptor->name << " is at @group("
		   << actualGroupIndex << "), codegen owns @group(" << expectedGroupIndex << ")";
		diags.push_back({os.str(), shaderPath, 0});
	}

	if (found.bindingIndex != expected.bindingIndex)
	{
		std::ostringstream os;
		os << "binding for " << expected.descriptor->name << " is at @binding("
		   << found.bindingIndex << "), codegen owns @binding(" << expected.bindingIndex << ")";
		diags.push_back({os.str(), shaderPath, 0});
	}

	if (!expected.wgslVarName.empty() && found.wgslName != expected.wgslVarName)
	{
		std::ostringstream os;
		os << "binding for " << expected.descriptor->name << ": WGSL var name '"
		   << found.wgslName << "' != codegen's '" << expected.wgslVarName << "'";
		diags.push_back({os.str(), shaderPath, 0});
	}
}

} // namespace

std::vector<ValidationDiagnostic>
validateExpandedWgsl(const std::string &finalWgsl, const std::filesystem::path &shaderPath)
{
	std::vector<ValidationDiagnostic> diags;
	const auto &registrations = registeredGeneratedBindings();
	if (registrations.empty())
	{
		return diags; // No codegen has run; nothing to validate against.
	}

	auto result = reflection::reflectWgsl(finalWgsl, shaderPath.string());

	// Reflector's own parse diagnostics indicate that the post-include WGSL is
	// malformed enough to confuse the parser; surface them via the validator so
	// the caller sees a single stream.
	for (const auto &d : result.diagnostics)
	{
		std::ostringstream os;
		os << "reflector: " << d.message;
		diags.push_back({os.str(), shaderPath, d.line});
	}

	std::unordered_map<std::string, const GeneratedBindingRecord *> byName;
	byName.reserve(registrations.size());
	for (const auto &reg : registrations)
	{
		byName.emplace(reg.descriptor->name, &reg);
	}

	for (const auto &found : result.reflection.structs)
	{
		auto it = byName.find(found.name);
		if (it == byName.end()) continue; // Not engine-generated.
		compareStruct(found, *it->second, shaderPath, diags);
	}

	for (const auto &group : result.reflection.bindGroups)
	{
		for (const auto &binding : group.bindings)
		{
			auto it = byName.find(binding.structLayout.name);
			if (it == byName.end()) continue;
			compareBindingSite(binding, group.groupIndex, *it->second, shaderPath, diags);
		}
	}

	return diags;
}

} // namespace engine::rendering::shaders
