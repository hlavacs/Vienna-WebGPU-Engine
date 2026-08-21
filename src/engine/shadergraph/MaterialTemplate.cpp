#include "engine/shadergraph/MaterialTemplate.h"

#include <sstream>

namespace engine::shadergraph
{

std::string wrapAsPBRFragmentShader(const Graph::CompileResult &compileResult)
{
	if (!compileResult.success) return {};

	std::ostringstream out;

	// Engine bindings via the codegen includes — exactly what the hand-
	// written PBR_Lit_Shader uses. Keeps graph-driven materials byte-
	// compatible with the engine's bind-group layouts.
	out << "// Auto-generated from shader graph compilation. Do not edit.\n"
	    << "#include \"engine://core/frame_uniforms.wgsl\"\n"
	    << "#include \"engine://core/object_uniforms.wgsl\"\n"
	    << "#include \"engine://core/material.wgsl\"\n"
	    << "\n";

	// Graph-specific module-level decls (texture bindings, helper fns).
	if (!compileResult.declarations.empty())
	{
		out << "// Graph-declared bindings\n"
		    << compileResult.declarations
		    << "\n";
	}

	// Standard PBR interpolant struct. Matches PBR_Lit_Shader.wgsl so
	// graph-driven materials can share vertex shaders with hand-written
	// ones.
	out <<
		"struct VertexInput {\n"
		"    @location(0) position:  vec3<f32>,\n"
		"    @location(1) normal:    vec3<f32>,\n"
		"    @location(2) uv:        vec2<f32>,\n"
		"    @location(3) tangent:   vec3<f32>,\n"
		"}\n\n"
		"struct VertexOutput {\n"
		"    @builtin(position) position:        vec4<f32>,\n"
		"    @location(0)       world_position:  vec4<f32>,\n"
		"    @location(1)       normal:          vec3<f32>,\n"
		"    @location(2)       uv:              vec2<f32>,\n"
		"    @location(3)       tangent:         vec3<f32>,\n"
		"}\n\n"
		"@vertex\n"
		"fn vs_main(input: VertexInput) -> VertexOutput {\n"
		"    var output: VertexOutput;\n"
		"    let world_pos = u_object.modelMatrix * vec4<f32>(input.position, 1.0);\n"
		"    output.world_position = world_pos;\n"
		"    output.position = u_frame.viewProjectionMatrix * world_pos;\n"
		"    output.normal = normalize((u_object.normalMatrix * vec4<f32>(input.normal, 0.0)).xyz);\n"
		"    output.tangent = normalize((u_object.modelMatrix * vec4<f32>(input.tangent, 0.0)).xyz);\n"
		"    output.uv = input.uv;\n"
		"    return output;\n"
		"}\n\n";

	// Fragment body wraps the graph's emitted `let` bindings and returns the
	// designated result expression. resultExpr is the variable carrying the
	// MaterialOutput.material (= albedo) value.
	out << "@fragment\n"
	    << "fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {\n"
	    << compileResult.wgsl
	    << "    return vec4<f32>(" << compileResult.resultExpr << ", 1.0);\n"
	    << "}\n";

	return out.str();
}

} // namespace engine::shadergraph
