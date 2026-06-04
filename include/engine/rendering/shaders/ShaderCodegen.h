#pragma once

#include <filesystem>
#include <string>

#include "engine/rendering/shaders/StructDescriptor.h"

namespace engine::rendering::shaders
{

/**
 * @brief Resource binding kind for generated bind-group declarations.
 *
 * Mirrors the subset of WebGPU binding kinds the codegen currently emits.
 * Buffer bindings carry a `StructDescriptor`; the other kinds get added when a
 * descriptor needs them (samplers, textures, storage textures).
 */
enum class GenBindingKind : uint8_t
{
	UniformBuffer,
	StorageBufferRO,
	StorageBufferRW,
};

/**
 * @brief Spec for a single `@group(N) @binding(M) var<...> name: Type;` line.
 */
struct GenBindingSpec
{
	uint32_t                bindingIndex;
	GenBindingKind          kind;
	std::string             wgslVarName; ///< e.g. "u_frame".
	const StructDescriptor *structRef;   ///< Non-owning. Outlives this spec.
};

/// One canonical engine bind group's full declaration.
struct GenEngineGroupSpec
{
	EngineBindGroup             group;
	std::vector<GenBindingSpec> bindings;
};

/**
 * @brief Pure-text emitters. No engine deps; safe to call from anywhere.
 *
 * Each emit* function appends to `out`. Callers control file wrapping (header
 * comment, trailing newline) via emitGeneratedFile or by composing directly.
 */
namespace ShaderCodegen
{
/// Emit one WGSL struct definition. Inserts explicit `_pad_*` fields where
/// std140 layout demands padding between consecutive descriptor fields, so
/// the generated WGSL byte layout is independent of compiler choices.
void emitStruct(const StructDescriptor &d, std::string &out);

/// Like `emitStruct` but appends one extra field at the end:
///     `<arrayFieldName>: array<ElementName>`
///
/// Used for storage-buffer wrappers (e.g. `LightsBuffer { count, _pad..., lights: array<LightStruct> }`)
/// where the runtime-sized array can't be expressed as a regular descriptor
/// field. The header struct's size + alignment still come from the
/// descriptor; the trailing array adds zero compile-time size.
void emitStructWithRuntimeArray(const StructDescriptor &header,
                                const std::string      &arrayFieldName,
                                const std::string      &elementWgslName,
                                std::string            &out);

/// Emit one `@group(N) @binding(M) var<...> name: Type;` line.
void emitBindingDecl(uint32_t groupIndex, const GenBindingSpec &b, std::string &out);

/// Emit an entire engine bind-group block: the struct(s) referenced by its
/// bindings (if not already emitted by the caller) followed by every
/// `@group/@binding` line. Does NOT add an outer file header — use
/// emitGeneratedFile for that.
void emitEngineGroup(const GenEngineGroupSpec &g, std::string &out);

/// Wrap an emit body with a "AUTO-GENERATED ..." header comment + trailing
/// newline, suitable for writing to disk under `resources/generated/`.
[[nodiscard]] std::string emitGeneratedFile(std::string_view sourceLabel, std::string body);

/// Convenience: produce the full file body for one engine bind group plus
/// its referenced struct(s). Drop into `resources/generated/<name>.wgsl`.
[[nodiscard]] std::string emitGeneratedEngineGroupFile(std::string_view sourceLabel, const GenEngineGroupSpec &g);

/// Write `contents` to `path` only if the file does not exist or its bytes
/// differ. Returns true when a write actually happened. No-op writes do not
/// touch mtime, so file watchers don't fire on idempotent regeneration.
bool writeIfChanged(const std::filesystem::path &path, const std::string &contents);
} // namespace ShaderCodegen

} // namespace engine::rendering::shaders
