#pragma once

#include <array>
#include <string>
#include <vector>

#include "engine/shadergraph/Graph.h"

namespace engine::shadergraph::nodes
{

/// WGSL literal for "the additive identity at this slot type" — `"0.0"`
/// for F32, `"vec3<f32>(0.0)"` for Vec3, etc. Used as the default value
/// for unconnected math-node inputs. Free helper so every node class can
/// share it without a friend declaration.
std::string zeroLiteral(SlotType t);

/// WGSL literal for the multiplicative identity at this slot type.
std::string oneLiteral(SlotType t);


/**
 * @brief Emit a float literal as the node's single output.
 *
 *   `let n<id>_0: f32 = <value>;`
 */
class ConstantF32 : public Node
{
  public:
	explicit ConstantF32(float v) : m_value(v) {}

	const char *typeName() const override { return "ConstantF32"; }
	std::vector<SlotSpec> inputs()  const override { return {}; }
	std::vector<SlotSpec> outputs() const override { return {{"value", SlotType::F32, ""}}; }

	std::string emit(
		const std::vector<std::string> & /*inputs*/,
		const std::vector<std::string> &outs) const override
	{
		return "    let " + outs[0] + ": f32 = " + formatFloat(m_value) + ";\n";
	}

  private:
	float m_value;
	static std::string formatFloat(float v);
};

/**
 * @brief Emit a vec3<f32> literal as the node's single output.
 *
 *   `let n<id>_0: vec3<f32> = vec3<f32>(<r>, <g>, <b>);`
 */
class ConstantVec3 : public Node
{
  public:
	explicit ConstantVec3(float r, float g, float b) : m_rgb{r, g, b} {}

	const char *typeName() const override { return "ConstantVec3"; }
	std::vector<SlotSpec> inputs()  const override { return {}; }
	std::vector<SlotSpec> outputs() const override { return {{"value", SlotType::Vec3, ""}}; }

	std::string emit(
		const std::vector<std::string> & /*inputs*/,
		const std::vector<std::string> &outs) const override;

  private:
	std::array<float, 3> m_rgb;
};

/**
 * @brief Component-wise multiply `a * b`. Operates on whichever slot type
 *        the node was constructed with — both inputs and the output share it.
 */
class Multiply : public Node
{
  public:
	explicit Multiply(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Multiply"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"a", m_type, defaultExpr(m_type)},
			{"b", m_type, defaultExpr(m_type)},
		};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", m_type, ""}};
	}

	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
	static std::string defaultExpr(SlotType t);
};

/**
 * @brief Component-wise add `a + b`. Same shape as Multiply.
 */
class Add : public Node
{
  public:
	explicit Add(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Add"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"a", m_type, defaultExpr(m_type)},
			{"b", m_type, defaultExpr(m_type)},
		};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", m_type, ""}};
	}

	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
	static std::string defaultExpr(SlotType t);
};

/**
 * @brief Component-wise `mix(a, b, t)` — linear interpolation. `a` and `b`
 *        share the slot type; `t` is always f32.
 */
class Mix : public Node
{
  public:
	explicit Mix(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Mix"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"a", m_type, zeroLiteral(m_type)},
			{"b", m_type, zeroLiteral(m_type)},
			{"t", SlotType::F32, "0.5"},
		};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", m_type, ""}};
	}

	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
};

/// `saturate(x)` — clamp to [0, 1]. Slot type-agnostic; operates on whatever
/// the input is.
class Saturate : public Node
{
  public:
	explicit Saturate(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Saturate"; }
	std::vector<SlotSpec> inputs() const override
	{
		return {{"value", m_type, zeroLiteral(m_type)}};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", m_type, ""}};
	}
	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
};

/// `normalize(v)` — unit-length vector. Vec2/Vec3/Vec4 only.
class Normalize : public Node
{
  public:
	explicit Normalize(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Normalize"; }
	std::vector<SlotSpec> inputs() const override
	{
		return {{"v", m_type, zeroLiteral(m_type)}};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", m_type, ""}};
	}
	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
};

/// `dot(a, b)` — scalar dot product. Inputs share the slot type; output
/// is always f32.
class Dot : public Node
{
  public:
	explicit Dot(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Dot"; }
	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"a", m_type, zeroLiteral(m_type)},
			{"b", m_type, zeroLiteral(m_type)},
		};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", SlotType::F32, ""}};
	}
	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
};

/// `pow(base, exponent)` — power. Slot-type agnostic.
class Pow : public Node
{
  public:
	explicit Pow(SlotType t) : m_type(t) {}

	const char *typeName() const override { return "Pow"; }
	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"base",     m_type, zeroLiteral(m_type)},
			{"exponent", m_type, m_type == SlotType::F32 ? "2.0" : oneLiteral(m_type)},
		};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", m_type, ""}};
	}
	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	SlotType m_type;
};

/// Split a vec3 into three f32 channels (r/g/b).
class SplitVec3 : public Node
{
  public:
	const char *typeName() const override { return "SplitVec3"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {{"v", SlotType::Vec3, "vec3<f32>(0.0)"}};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {
			{"r", SlotType::F32, ""},
			{"g", SlotType::F32, ""},
			{"b", SlotType::F32, ""},
		};
	}
	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;
};

/// Combine three f32 channels into a vec3.
class CombineVec3 : public Node
{
  public:
	const char *typeName() const override { return "CombineVec3"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"r", SlotType::F32, "0.0"},
			{"g", SlotType::F32, "0.0"},
			{"b", SlotType::F32, "0.0"},
		};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"result", SlotType::Vec3, ""}};
	}
	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;
};

/**
 * @brief Pull the per-fragment UV from the vertex-shader interpolant.
 *
 * Assumes the fragment-shader signature names the input struct `in` with
 * a `uv: vec2<f32>` field — matches what the engine's PBR template
 * vertex shader produces. The node has no inputs, one Vec2 output.
 */
class FragmentUV : public Node
{
  public:
	const char *typeName() const override { return "FragmentUV"; }
	std::vector<SlotSpec> inputs() const override { return {}; }
	std::vector<SlotSpec> outputs() const override
	{
		return {{"uv", SlotType::Vec2, ""}};
	}
	std::string emit(
		const std::vector<std::string> & /*ins*/,
		const std::vector<std::string> &outs) const override;
};

/**
 * @brief Pull the per-fragment world-space normal from the interpolant.
 *
 * Output is a Vec3; the engine's PBR template populates this from the
 * vertex shader after TBN math.
 */
class FragmentNormal : public Node
{
  public:
	const char *typeName() const override { return "FragmentNormal"; }
	std::vector<SlotSpec> inputs() const override { return {}; }
	std::vector<SlotSpec> outputs() const override
	{
		return {{"normal", SlotType::Vec3, ""}};
	}
	std::string emit(
		const std::vector<std::string> & /*ins*/,
		const std::vector<std::string> &outs) const override;
};

/**
 * @brief Sample a 2D texture at the supplied UV. Declares the texture and
 *        a sampler at the engine's canonical Material bind group (@group(2)).
 *
 * @param wgslName  WGSL identifier for the texture (e.g. `"u_baseColor"`).
 *                  The sampler shares the same name with `_sampler` appended.
 * @param textureBinding  Material-group binding index for the texture.
 * @param samplerBinding  Material-group binding index for the sampler.
 *
 * Single input UV (Vec2); single output rgba (Vec4). The compiler picks up
 * this node's `declarations()` and emits the binding lines at module scope
 * exactly once even if multiple TextureSample nodes share the same name.
 */
class TextureSample : public Node
{
  public:
	TextureSample(std::string wgslName,
	              uint32_t    textureBinding,
	              uint32_t    samplerBinding) :
		m_name(std::move(wgslName)),
		m_textureBinding(textureBinding),
		m_samplerBinding(samplerBinding) {}

	const char *typeName() const override { return "TextureSample"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {{"uv", SlotType::Vec2, "vec2<f32>(0.0)"}};
	}
	std::vector<SlotSpec> outputs() const override
	{
		return {{"rgba", SlotType::Vec4, ""}};
	}

	std::vector<BindingDecl> declarations() const override;

	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;

  private:
	std::string m_name;
	uint32_t    m_textureBinding;
	uint32_t    m_samplerBinding;
};

/**
 * @brief Terminal "material output" node. Inputs are the PBR channels the
 *        engine's material struct exposes:
 *
 *   - `albedo`    : Vec3 (defaults to white)
 *   - `metallic`  : F32  (defaults to 0)
 *   - `roughness` : F32  (defaults to 0.5)
 *   - `emission`  : Vec3 (defaults to black)
 *
 * Output slot 0 (`material`) is the type the compiler treats as the graph's
 * final result — by convention it carries the `albedo` value so a freshly-
 * wired graph "just shows the colour" with no extra plumbing. The full PBR
 * integration (writing all four channels into the engine's `Material`
 * struct) is wired in a follow-up.
 */
class MaterialOutput : public Node
{
  public:
	const char *typeName() const override { return "MaterialOutput"; }

	std::vector<SlotSpec> inputs() const override
	{
		return {
			{"albedo",    SlotType::Vec3, "vec3<f32>(1.0)"},
			{"metallic",  SlotType::F32,  "0.0"},
			{"roughness", SlotType::F32,  "0.5"},
			{"emission",  SlotType::Vec3, "vec3<f32>(0.0)"},
		};
	}

	std::vector<SlotSpec> outputs() const override
	{
		return {{"material", SlotType::Vec3, ""}};
	}

	std::string emit(
		const std::vector<std::string> &ins,
		const std::vector<std::string> &outs) const override;
};

} // namespace engine::shadergraph::nodes
