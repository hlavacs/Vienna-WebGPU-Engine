#pragma once

#include <string>
#include <vector>

#include "engine/rendering/shaders/StructDescriptor.h"

namespace engine::rendering::shaders
{

/**
 * @brief Canonical descriptors for the engine's GPU-side structs.
 *
 * Each function returns a static singleton whose layout has been verified
 * against the matching C++ struct in `engine/rendering/*.h`. Code that wants
 * to emit WGSL or build a bind-group layout reads from these.
 */

const StructDescriptor &describeFrameUniforms();

/**
 * @brief Codegen's authoritative record of one emitted struct + binding.
 *
 * Populated by `regenerateEngineGeneratedFiles()` so the post-codegen
 * validator can ask "is FrameUniforms supposed to be at @group(0)?" and
 * fail loud when a shader places it elsewhere or redeclares it differently.
 */
struct GeneratedBindingRecord
{
	const StructDescriptor *descriptor;   ///< Non-owning. Static lifetime.
	EngineBindGroup         group;
	uint32_t                bindingIndex;
	std::string             wgslVarName;  ///< e.g. "u_frame".
	/// Buffer-wrapper structs trail a runtime-sized `array<Element>` after the
	/// header fields. The reflector sees N+1 fields where the descriptor only
	/// knows N — set this so the validator skips the count mismatch and the
	/// trailing-field comparison.
	bool                    hasTrailingRuntimeArray = false;
};

/// All bindings the codegen has emitted into `resources/generated/`.
/// Filled by `regenerateEngineGeneratedFiles()`. Returns an empty list if
/// regeneration hasn't run yet.
const std::vector<GeneratedBindingRecord> &registeredGeneratedBindings();

/**
 * @brief (Re)write every engine-generated WGSL header under `resources/generated/`.
 *
 * Idempotent — uses `ShaderCodegen::writeIfChanged`, so files whose bytes are
 * already current are not touched (and don't fire the file watcher). Safe to
 * call once at engine init before any shader load. Returns the number of files
 * that actually changed. Also (re)populates the generated-binding registry
 * consumed by the post-codegen validator.
 */
uint32_t regenerateEngineGeneratedFiles();

} // namespace engine::rendering::shaders
