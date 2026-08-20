#pragma once

#include <string>

#include "engine/shadergraph/Graph.h"

namespace engine::shadergraph
{

/**
 * @brief Wrap a graph compilation into a complete WGSL fragment shader that
 *        the engine's ShaderRegistry can register and bind.
 *
 * The template mirrors the engine's canonical convention:
 *   - Frame uniforms at @group(0) (via engine://core/frame_uniforms.wgsl)
 *   - Material struct + bindings at @group(2) (via engine://core/material.wgsl)
 *   - Object uniforms at @group(3) (via engine://core/object_uniforms.wgsl)
 *   - Custom texture/sampler bindings injected from compileResult.declarations
 *
 * The vertex shader passes through standard PBR interpolants (position,
 * normal, uv, world_position) so the graph's FragmentUV / FragmentNormal /
 * etc. nodes can reference `in.uv` / `in.normal` directly.
 *
 * Final output is `vec4f(result, 1.0)` where `result` is the graph's
 * designated output expression — for a MaterialOutput-terminated graph
 * that's the albedo channel. MaterialOutput's other named bindings
 * (`<out>_metallic` etc.) are present in the emitted body but not yet
 * routed to G-buffer color attachments; a future PBR-MRT template would
 * need to do that.
 */
[[nodiscard]] std::string wrapAsPBRFragmentShader(const Graph::CompileResult &compileResult);

} // namespace engine::shadergraph
