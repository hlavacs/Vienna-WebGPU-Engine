#include "engine/shadergraph/Nodes.h"

#include <cstdio>
#include <string>

namespace engine::shadergraph::nodes
{

namespace
{

/// WGSL-safe f32 literal formatter. WGSL parses `"1"` as i32, so we always
/// emit a decimal point (or exponent) for f32 literals. `%g` keeps the
/// short form for common values like `0.5` and `2.0`.
std::string formatFloatLiteral(float v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.7g", static_cast<double>(v));
	std::string s(buf);
	if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
	    && s.find('E') == std::string::npos)
	{
		s += ".0";
	}
	return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Free helpers: zero / one literals per SlotType
// ---------------------------------------------------------------------------

std::string zeroLiteral(SlotType t)
{
	switch (t)
	{
		case SlotType::F32:   return "0.0";
		case SlotType::Vec2:  return "vec2<f32>(0.0)";
		case SlotType::Vec3:  return "vec3<f32>(0.0)";
		case SlotType::Vec4:  return "vec4<f32>(0.0)";
		default:              return "0.0";  // Opaque / matrix handled by validator.
	}
}

std::string oneLiteral(SlotType t)
{
	switch (t)
	{
		case SlotType::F32:   return "1.0";
		case SlotType::Vec2:  return "vec2<f32>(1.0)";
		case SlotType::Vec3:  return "vec3<f32>(1.0)";
		case SlotType::Vec4:  return "vec4<f32>(1.0)";
		default:              return "1.0";
	}
}

// ---------------------------------------------------------------------------
// ConstantF32
// ---------------------------------------------------------------------------

std::string ConstantF32::formatFloat(float v)
{
	return formatFloatLiteral(v);
}

// ---------------------------------------------------------------------------
// ConstantVec3
// ---------------------------------------------------------------------------

std::string ConstantVec3::emit(
	const std::vector<std::string> & /*ins*/,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": vec3<f32> = vec3<f32>("
		+ formatFloatLiteral(m_rgb[0]) + ", "
		+ formatFloatLiteral(m_rgb[1]) + ", "
		+ formatFloatLiteral(m_rgb[2]) + ");\n";
}

// ---------------------------------------------------------------------------
// Multiply
// ---------------------------------------------------------------------------

std::string Multiply::defaultExpr(SlotType t)
{
	switch (t)
	{
		case SlotType::F32:   return "1.0";
		case SlotType::Vec2:  return "vec2<f32>(1.0)";
		case SlotType::Vec3:  return "vec3<f32>(1.0)";
		case SlotType::Vec4:  return "vec4<f32>(1.0)";
		default:              return "1.0"; // Mat/opaque cases handled by graph validator.
	}
}

std::string Multiply::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": " + wgslTypeName(m_type)
		+ " = " + ins[0] + " * " + ins[1] + ";\n";
}

// ---------------------------------------------------------------------------
// Add
// ---------------------------------------------------------------------------

std::string Add::defaultExpr(SlotType t)
{
	switch (t)
	{
		case SlotType::F32:   return "0.0";
		case SlotType::Vec2:  return "vec2<f32>(0.0)";
		case SlotType::Vec3:  return "vec3<f32>(0.0)";
		case SlotType::Vec4:  return "vec4<f32>(0.0)";
		default:              return "0.0";
	}
}

std::string Add::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": " + wgslTypeName(m_type)
		+ " = " + ins[0] + " + " + ins[1] + ";\n";
}

// ---------------------------------------------------------------------------
// Mix / Saturate / Normalize / Dot / Pow — single-line math wrappers.
// ---------------------------------------------------------------------------

std::string Mix::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": " + wgslTypeName(m_type)
		+ " = mix(" + ins[0] + ", " + ins[1] + ", " + ins[2] + ");\n";
}

std::string Saturate::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": " + wgslTypeName(m_type)
		+ " = clamp(" + ins[0] + ", " + zeroLiteral(m_type) + ", " + oneLiteral(m_type) + ");\n";
}

std::string Normalize::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": " + wgslTypeName(m_type)
		+ " = normalize(" + ins[0] + ");\n";
}

std::string Dot::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": f32 = dot(" + ins[0] + ", " + ins[1] + ");\n";
}

std::string Pow::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": " + wgslTypeName(m_type)
		+ " = pow(" + ins[0] + ", " + ins[1] + ");\n";
}

// ---------------------------------------------------------------------------
// SplitVec3 / CombineVec3 — channel ops.
// ---------------------------------------------------------------------------

std::string SplitVec3::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	std::string s;
	s += "    let " + outs[0] + ": f32 = " + ins[0] + ".x;\n";
	s += "    let " + outs[1] + ": f32 = " + ins[0] + ".y;\n";
	s += "    let " + outs[2] + ": f32 = " + ins[0] + ".z;\n";
	return s;
}

std::string CombineVec3::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": vec3<f32> = vec3<f32>("
		+ ins[0] + ", " + ins[1] + ", " + ins[2] + ");\n";
}

// ---------------------------------------------------------------------------
// Fragment-input passthroughs. These don't emit a value — they reference
// the fragment-shader struct's field directly. The template wrapping the
// graph must name its input `in`.
// ---------------------------------------------------------------------------

std::string FragmentUV::emit(
	const std::vector<std::string> & /*ins*/,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": vec2<f32> = in.uv;\n";
}

std::string FragmentNormal::emit(
	const std::vector<std::string> & /*ins*/,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": vec3<f32> = normalize(in.normal);\n";
}

// ---------------------------------------------------------------------------
// TextureSample
// ---------------------------------------------------------------------------

std::vector<BindingDecl> TextureSample::declarations() const
{
	std::vector<BindingDecl> out;
	// Material group = canonical @group(2). The compiler dedupes by exact
	// text, so two TextureSample nodes that share a name emit these lines
	// once. Names also collide with the engine's PBR conventions if reused
	// (`u_baseColor` already exists in PBR_Lit_Shader.wgsl) — that's
	// intentional, the graph is a drop-in replacement for hand-written
	// material shaders.
	out.push_back({
		"@group(2) @binding(" + std::to_string(m_textureBinding) + ") var "
			+ m_name + ": texture_2d<f32>;\n"
	});
	out.push_back({
		"@group(2) @binding(" + std::to_string(m_samplerBinding) + ") var "
			+ m_name + "_sampler: sampler;\n"
	});
	return out;
}

std::string TextureSample::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	return "    let " + outs[0] + ": vec4<f32> = textureSample("
		+ m_name + ", " + m_name + "_sampler, " + ins[0] + ");\n";
}

// ---------------------------------------------------------------------------
// MaterialOutput
// ---------------------------------------------------------------------------

std::string MaterialOutput::emit(
	const std::vector<std::string> &ins,
	const std::vector<std::string> &outs) const
{
	// First-cut PBR integration: route albedo through to the graph's result
	// slot. The compiler treats this as the final value; the full Material
	// struct hookup (writing all four channels into the engine's material
	// uniform) is a separate pass.
	//
	// ins[0]=albedo, ins[1]=metallic, ins[2]=roughness, ins[3]=emission —
	// metallic/roughness/emission are emitted as `let` bindings so a later
	// pass can pick them up by name without re-walking the graph.
	std::string out;
	out += "    let " + outs[0] + ": vec3<f32> = " + ins[0] + ";\n";
	out += "    let " + outs[0] + "_metallic:  f32      = " + ins[1] + ";\n";
	out += "    let " + outs[0] + "_roughness: f32      = " + ins[2] + ";\n";
	out += "    let " + outs[0] + "_emission:  vec3<f32> = " + ins[3] + ";\n";
	return out;
}

} // namespace engine::shadergraph::nodes
