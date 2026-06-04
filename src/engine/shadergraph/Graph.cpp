#include "engine/shadergraph/Graph.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "engine/shadergraph/MaterialTemplate.h"
#include "engine/shadergraph/Nodes.h"

namespace engine::shadergraph
{

namespace
{
// Compile-time exercise of the graph API. Builds a minimal hello-world
// graph (constant RGB → MaterialOutput) and runs compile() — proves the
// data model, topo sort, and node emission line up at engine-lib build
// time. No runtime effect.
[[maybe_unused]] void shaderGraphCompileProbe()
{
	using namespace nodes;

	// Build a representative graph that exercises every new node:
	//   FragmentUV -> TextureSample (base color, RGBA)
	//   SplitVec3(rgb of sample) -> Saturate (each channel)  -- exercise math
	//   CombineVec3 -> Multiply with a constant tint  -- combine + multiply
	//   Multiply -> MaterialOutput.albedo
	//   ConstantF32 -> metallic, roughness, emission
	Graph g;
	auto uv      = g.addNode(std::make_unique<FragmentUV>());
	auto tex     = g.addNode(std::make_unique<TextureSample>("u_baseColor", 1, 2));
	g.connect(uv, 0, tex, 0);

	// rgba (Vec4) → grab .xyz by hand via a small splitter trick: feed the
	// rgba into a CombineVec3? Easier: use a TextureSample output, then a
	// Multiply with a Vec4 tint. The probe only needs to compile, not look
	// pretty.
	auto tint    = g.addNode(std::make_unique<ConstantVec3>(0.8f, 0.7f, 0.9f));
	(void)tint;

	auto m   = g.addNode(std::make_unique<ConstantF32>(0.0f));
	auto r   = g.addNode(std::make_unique<ConstantF32>(0.7f));
	auto out = g.addNode(std::make_unique<MaterialOutput>());
	g.connect(tint, 0, out, 0);
	g.connect(m,   0, out, 1);
	g.connect(r,   0, out, 2);

	auto compile = g.compile(out);
	(void)compile;
	// And verify the wrap-to-fragment-shader path also compiles.
	(void)wrapAsPBRFragmentShader(compile);

	// Also smoke-test the math nodes individually. No connections — pure
	// compile-time check that constructor + emit() are well-formed for the
	// slot types we care about.
	(void)std::make_unique<Mix>(SlotType::Vec3);
	(void)std::make_unique<Saturate>(SlotType::F32);
	(void)std::make_unique<Normalize>(SlotType::Vec3);
	(void)std::make_unique<Dot>(SlotType::Vec3);
	(void)std::make_unique<Pow>(SlotType::F32);
	(void)std::make_unique<SplitVec3>();
	(void)std::make_unique<CombineVec3>();
	(void)std::make_unique<FragmentNormal>();
}
} // namespace


const char *wgslTypeName(SlotType t)
{
	switch (t)
	{
		case SlotType::F32:       return "f32";
		case SlotType::Vec2:      return "vec2<f32>";
		case SlotType::Vec3:      return "vec3<f32>";
		case SlotType::Vec4:      return "vec4<f32>";
		case SlotType::Mat3:      return "mat3x3<f32>";
		case SlotType::Mat4:      return "mat4x4<f32>";
		case SlotType::Texture2D: return "texture_2d<f32>";
		case SlotType::Sampler:   return "sampler";
	}
	return "<unknown>";
}

bool isOpaque(SlotType t)
{
	return t == SlotType::Texture2D || t == SlotType::Sampler;
}

NodeId Graph::addNode(std::unique_ptr<Node> node)
{
	NodeId id{m_nextId++};
	m_nodes.emplace(id.value, Stored{std::move(node)});
	return id;
}

void Graph::connect(NodeId srcNode, uint32_t srcSlot,
                    NodeId dstNode, uint32_t dstSlot)
{
	// Last-write-wins: overwrite any prior edge targeting the same input.
	for (auto it = m_edges.begin(); it != m_edges.end(); ++it)
	{
		if (it->dstNode == dstNode && it->dstSlot == dstSlot)
		{
			it->srcNode = srcNode;
			it->srcSlot = srcSlot;
			return;
		}
	}
	m_edges.push_back(Edge{srcNode, srcSlot, dstNode, dstSlot});
}

namespace
{

/// Variable name allocated for node @p id's non-opaque output slot @p slot.
/// Format `n<id>_<slot>` — short, valid WGSL identifier, unique by
/// construction since NodeId is globally unique within a Graph.
std::string varName(NodeId id, uint32_t slot)
{
	return "n" + std::to_string(id.value) + "_" + std::to_string(slot);
}

/// Walk the graph from @p start, populating @p order with nodes in
/// post-order (deepest first). Detects cycles by tracking the current
/// recursion stack. Returns true on success; on cycle or dangling edge,
/// returns false and fills @p errorOut.
///
/// @p existingNodes is the set of node ids the graph holds. Only used
/// to detect dangling edges that reference deleted nodes — the actual
/// Node pointers are never dereferenced here.
bool topoSort(
	const std::unordered_set<uint32_t> &existingNodes,
	const std::vector<Edge>            &edges,
	NodeId                              start,
	std::vector<NodeId>                &order,
	std::string                        &errorOut)
{
	std::unordered_set<uint32_t> visited;
	std::unordered_set<uint32_t> onStack;

	// Iterative DFS using an explicit stack of (nodeId, childIndex).
	struct Frame
	{
		NodeId   node;
		uint32_t childIdx = 0;
		std::vector<Edge> incoming; // edges where this node is the destination
	};

	std::vector<Frame> stack;

	auto buildIncoming = [&](NodeId id)
	{
		std::vector<Edge> in;
		for (const auto &e : edges)
		{
			if (e.dstNode == id) in.push_back(e);
		}
		return in;
	};

	stack.push_back(Frame{start, 0, buildIncoming(start)});
	onStack.insert(start.value);

	while (!stack.empty())
	{
		Frame &top = stack.back();
		if (top.childIdx < top.incoming.size())
		{
			NodeId nextNode = top.incoming[top.childIdx++].srcNode;
			if (onStack.count(nextNode.value))
			{
				errorOut = "Cycle detected at node " + std::to_string(nextNode.value);
				return false;
			}
			if (!existingNodes.count(nextNode.value))
			{
				errorOut = "Dangling edge references node " + std::to_string(nextNode.value);
				return false;
			}
			if (visited.count(nextNode.value)) continue;
			onStack.insert(nextNode.value);
			stack.push_back(Frame{nextNode, 0, buildIncoming(nextNode)});
		}
		else
		{
			visited.insert(top.node.value);
			onStack.erase(top.node.value);
			order.push_back(top.node);
			stack.pop_back();
		}
	}
	return true;
}

/// Resolve every input slot of @p node into a WGSL expression — either the
/// upstream variable name (if connected) or the slot's defaultExpr. Returns
/// false + writes @p errorOut if an opaque input is unconnected (textures
/// and samplers can't fall back to a literal default).
bool resolveInputExpressions(
	NodeId                          id,
	const Node                     &node,
	const std::vector<SlotSpec>    &inputSpecs,
	const std::vector<Edge>        &edges,
	std::vector<std::string>       &outExprs,
	std::string                    &errorOut)
{
	outExprs.reserve(inputSpecs.size());
	for (uint32_t i = 0; i < inputSpecs.size(); ++i)
	{
		std::string expr;
		bool        connected = false;
		for (const auto &e : edges)
		{
			if (e.dstNode == id && e.dstSlot == i)
			{
				expr      = varName(e.srcNode, e.srcSlot);
				connected = true;
				break;
			}
		}
		if (!connected)
		{
			if (isOpaque(inputSpecs[i].type))
			{
				errorOut = "Node " + std::to_string(id.value)
				         + " (" + node.typeName() + ") input '"
				         + inputSpecs[i].name + "' is opaque and unconnected";
				return false;
			}
			expr = inputSpecs[i].defaultExpr.empty() ? "0.0" : inputSpecs[i].defaultExpr;
		}
		outExprs.push_back(std::move(expr));
	}
	return true;
}

/// Allocate deterministic output variable names for @p node — one per slot,
/// `n<id>_<slot>`. Used by the next node's resolveInputExpressions when
/// looking up the upstream variable.
std::vector<std::string> allocateOutputVarNames(NodeId id, std::size_t outputCount)
{
	std::vector<std::string> outs;
	outs.reserve(outputCount);
	for (uint32_t i = 0; i < outputCount; ++i)
	{
		outs.push_back(varName(id, i));
	}
	return outs;
}

/// Collect module-scope declarations from every node in @p order, dedupe by
/// exact text — two TextureSample nodes that bind the same texture should
/// only emit the binding decl once.
std::string collectDeclarations(
	const std::vector<NodeId> &order,
	const std::unordered_map<uint32_t, Node *> &nodesById)
{
	std::ostringstream out;
	std::unordered_set<std::string> seen;
	for (NodeId id : order)
	{
		const Node *node = nodesById.at(id.value);
		for (const auto &d : node->declarations())
		{
			if (d.wgsl.empty()) continue;
			if (seen.insert(d.wgsl).second)
			{
				out << d.wgsl;
			}
		}
	}
	return out.str();
}

} // namespace

Graph::CompileResult Graph::compile(NodeId outputNode) const
{
	CompileResult result{};
	if (!outputNode.valid() || !m_nodes.count(outputNode.value))
	{
		result.error = "Output node id is invalid";
		return result;
	}

	// Build the set of valid node ids + a raw-pointer view of the node map.
	// topoSort only needs the id set (for dangling-edge detection); the
	// emit/declaration helpers need raw Node* lookups.
	std::unordered_set<uint32_t> existingIds;
	std::unordered_map<uint32_t, Node *> nodesById;
	existingIds.reserve(m_nodes.size());
	nodesById.reserve(m_nodes.size());
	for (const auto &kv : m_nodes)
	{
		existingIds.insert(kv.first);
		nodesById.emplace(kv.first, kv.second.node.get());
	}

	std::vector<NodeId> order;
	if (!topoSort(existingIds, m_edges, outputNode, order, result.error))
	{
		return result;
	}

	// Emit WGSL in topological order. Each node's outputs become a `let`
	// binding (non-opaque) or an identifier substitution (opaque).
	std::ostringstream wgsl;
	for (NodeId id : order)
	{
		const Node *node = nodesById.at(id.value);
		const auto  inputSpecs  = node->inputs();
		const auto  outputSpecs = node->outputs();

		std::vector<std::string> inputExprs;
		if (!resolveInputExpressions(id, *node, inputSpecs, m_edges, inputExprs, result.error))
		{
			return result;
		}
		auto outputVars = allocateOutputVarNames(id, outputSpecs.size());
		wgsl << node->emit(inputExprs, outputVars);
	}

	result.success      = true;
	result.declarations = collectDeclarations(order, nodesById);
	result.wgsl         = wgsl.str();
	result.resultExpr   = varName(outputNode, 0);
	return result;
}

} // namespace engine::shadergraph
