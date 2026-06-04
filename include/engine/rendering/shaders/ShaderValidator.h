#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace engine::rendering::shaders
{

struct ValidationDiagnostic
{
	std::string           message;
	std::filesystem::path file;
	uint32_t              line = 0;
};

/**
 * @brief Post-codegen sanity check: parse the include-expanded WGSL and
 *        confirm every engine-generated struct + binding it contains matches
 *        what the C++ descriptors said the codegen emitted.
 *
 * Catches three classes of bug:
 *  - **Codegen drift** — the emitter produced WGSL that doesn't match the
 *    `StructDescriptor` it was supposed to encode (size/offset/type/name).
 *  - **Hand-written redeclaration** — a shader manually wrote a struct or
 *    binding with the same name as a generated one but a different layout,
 *    silently overriding the codegen.
 *  - **Misplaced engine bindings** — a shader put `FrameUniforms` at the
 *    wrong `@group(N)` / `@binding(M)` / var name.
 *
 * Bindings/structs whose name doesn't appear in
 * `registeredGeneratedBindings()` are ignored — they're the shader author's
 * own types, not under codegen control.
 *
 * Returns the union of diagnostics; empty = OK. Callers decide whether to
 * fail the shader load loud (recommended for Debug/CI) or just log
 * (acceptable for prototype work, since wgpu's own validation will catch the
 * fatal cases anyway).
 */
[[nodiscard]] std::vector<ValidationDiagnostic>
validateExpandedWgsl(const std::string &finalWgsl, const std::filesystem::path &shaderPath);

} // namespace engine::rendering::shaders
