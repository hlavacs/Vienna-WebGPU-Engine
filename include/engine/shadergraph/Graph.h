#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::shadergraph
{

/**
 * @brief WGSL value types a slot can carry.
 *
 * Opaque types (Texture2D, Sampler) are valid for inputs/outputs but cannot
 * be emitted as `let` values — they must be passed through unchanged. The
 * compiler treats them specially: only Texture2D/Sampler edges produce no
 * intermediate variable; the consumer reads the source slot's identifier
 * directly.
 */
enum class SlotType : uint8_t
{
	F32,
	Vec2,
	Vec3,
	Vec4,
	Mat3,
	Mat4,
	Texture2D,
	Sampler,
};

/// WGSL type spelling for @p t (`"f32"`, `"vec3<f32>"`, ...). Stable string
/// returned for code-emission and diagnostics.
const char *wgslTypeName(SlotType t);

/// True for types that cannot be assigned to a `let` binding (textures,
/// samplers). The compiler routes their edges as identifier substitution
/// instead of intermediate-variable emission.
bool isOpaque(SlotType t);

/// One named slot on a node (input or output). `defaultExpr` provides a
/// WGSL literal used when an input is unconnected (e.g. `"0.0"` for F32,
/// `"vec3<f32>(0.0)"` for Vec3). Opaque slots don't have defaults —
/// the compiler rejects an unconnected opaque input.
struct SlotSpec
{
	std::string name;
	SlotType    type;
	std::string defaultExpr;
};

/**
 * @brief Module-level declaration a node needs in scope: a texture/sampler
 *        binding, a uniform, a const, or a helper fn.
 *
 * The compiler collects declarations from every node in the graph,
 * deduplicates by exact text match, and emits them once at the top of the
 * generated WGSL (before the per-node `let` bindings). A `TextureSample`
 * node, for instance, contributes its texture and sampler bindings here so
 * the consumer shader has them visible.
 */
struct BindingDecl
{
	/// The full WGSL line(s) to inject at module scope. Include a trailing
	/// newline so deduplicated lines concatenate cleanly.
	std::string wgsl;
};

/// Opaque identifier for a node within a Graph. 0 = invalid.
struct NodeId
{
	uint32_t value = 0;
	bool valid() const { return value != 0; }
	bool operator==(const NodeId &o) const { return value == o.value; }
	bool operator!=(const NodeId &o) const { return value != o.value; }
};

/**
 * @brief Base class for every node in a shader graph.
 *
 * Each subclass declares its input + output slots and an emitter that
 * produces a WGSL snippet binding the outputs from the inputs. The
 * compiler:
 *   - resolves every input to either a connected upstream variable name
 *     or the slot's `defaultExpr` literal
 *   - allocates unique variable names for every non-opaque output
 *     (`n<id>_<slot>`)
 *   - calls `emit(inputExprs, outputVars)` and appends the result to the
 *     fragment body
 *
 * Stateless from the compiler's perspective — node subclasses may carry
 * constant data (a literal value, a uniform index, a texture binding) but
 * never depend on neighbouring nodes' state.
 */
class Node
{
public:
	virtual ~Node() = default;

	/// Short type-name for diagnostics (e.g. `"ConstantF32"`, `"Multiply"`).
	virtual const char *typeName() const = 0;

	/// Input + output specs. Order is the binding order — `inputExprs[i]`
	/// in `emit()` matches `inputs()[i]`.
	virtual std::vector<SlotSpec> inputs()  const = 0;
	virtual std::vector<SlotSpec> outputs() const = 0;

	/**
	 * @brief Produce the WGSL snippet that computes this node's outputs.
	 * @param inputExprs  WGSL expressions for each input slot (either an
	 *                    upstream variable name or the slot's default).
	 * @param outputVars  Unique variable names allocated for each non-opaque
	 *                    output slot. Opaque outputs get the same string as
	 *                    the upstream identifier (substituted directly).
	 * @return WGSL fragment ending with a newline. The compiler concatenates
	 *         these in topological order.
	 */
	virtual std::string emit(
		const std::vector<std::string> &inputExprs,
		const std::vector<std::string> &outputVars) const = 0;

	/**
	 * @brief Module-level declarations this node needs in scope (texture/
	 *        sampler bindings, uniform decls, helper fns).
	 *
	 * Default empty — most nodes (constants, math) need nothing. Nodes that
	 * sample resources (TextureSample, UniformRead) override to declare
	 * their bindings. The compiler deduplicates by exact text match and
	 * emits the unique set at module scope before the fragment-shader body.
	 */
	virtual std::vector<BindingDecl> declarations() const { return {}; }
};

/// Directed edge from one node's output slot to another node's input slot.
struct Edge
{
	NodeId   srcNode;
	uint32_t srcSlot = 0;
	NodeId   dstNode;
	uint32_t dstSlot = 0;
};

/**
 * @brief Container of nodes + edges with a compile-to-WGSL pass.
 *
 * Workflow:
 *   - construct a Graph
 *   - addNode(...) to register nodes; each call returns a fresh NodeId
 *   - connect(src, dst) to wire output slots to input slots
 *   - compile(outputNode) to walk the graph from the designated output
 *     node and emit the WGSL fragment-shader body
 *
 * Compilation is single-pass and deterministic: topo sort + per-node
 * emission. Cycles, type mismatches, and dangling opaque inputs are
 * reported as errors in CompileResult.
 */
class Graph
{
public:
	/// Add a node. Ownership transfers to the Graph. Returns the new id.
	NodeId addNode(std::unique_ptr<Node> node);

	/// Wire `srcNode.outputs[srcSlot]` → `dstNode.inputs[dstSlot]`. Last
	/// connection to a given destination slot wins (overwrites previous).
	/// Type compatibility is checked at compile time, not at connect time.
	void connect(NodeId srcNode, uint32_t srcSlot,
	             NodeId dstNode, uint32_t dstSlot);

	/// Bytes-of-effort-friendly result. `success == false` carries a human-
	/// readable error string for the editor / log; on success, `wgsl`
	/// holds the fragment-shader body and `resultExpr` is the variable
	/// name carrying the chosen output slot's value.
	struct CompileResult
	{
		bool        success    = false;
		std::string declarations;  ///< Module-scope decls (deduped). Emit before any function.
		std::string wgsl;          ///< Concatenated `let` bindings + node emits.
		std::string resultExpr;    ///< Variable name for the output node's slot 0.
		std::string error;         ///< Non-empty when success == false.
	};

	/// Walk the graph from @p outputNode (depth-first, post-order) and
	/// emit WGSL. Each non-opaque output slot becomes a unique `let`
	/// binding; opaque slots forward identifiers. Returns CompileResult.
	/// Output slot 0 of @p outputNode is the result.
	CompileResult compile(NodeId outputNode) const;

	/// Number of nodes currently in the graph.
	[[nodiscard]] std::size_t nodeCount() const { return m_nodes.size(); }

private:
	struct Stored
	{
		std::unique_ptr<Node> node;
	};
	std::unordered_map<uint32_t, Stored> m_nodes;
	std::vector<Edge>                    m_edges;
	uint32_t                             m_nextId = 1;
};

} // namespace engine::shadergraph
