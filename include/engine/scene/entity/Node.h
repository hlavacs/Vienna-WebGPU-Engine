#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include "engine/core/Identifiable.h"

namespace engine
{
class EngineContext;
}

namespace engine::rendering
{
class DebugRenderCollector;
}

namespace engine::scene
{
class Scene;
class SceneManager;
}

namespace engine::scene::entity {

// Forward declarations for casting helpers
class RenderNode;
class UpdateNode;
class PhysicsNode;

/**
 * @brief Node type flags for identifying node capabilities.
 * Multiple flags can be combined using bitwise OR.
 */
enum class NodeType : uint32_t {
	None        = 0,
	Base        = 1 << 0,  // Basic node
	Spatial     = 1 << 1,  // Has transform (SpatialNode)
	Update      = 1 << 2,  // Has update/lateUpdate (UpdateNode)
	Render      = 1 << 3,  // Has render methods (RenderNode)
	Physics     = 1 << 4,  // Has fixedUpdate (PhysicsNode)
	Camera      = 1 << 5,  // Camera node
	Light       = 1 << 6,  // Light node
	Model       = 1 << 7,  // Model render node
};

// Bitwise operators for NodeType flags
inline NodeType operator|(NodeType a, NodeType b) {
	return static_cast<NodeType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline NodeType operator&(NodeType a, NodeType b) {
	return static_cast<NodeType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline NodeType& operator|=(NodeType& a, NodeType b) {
	a = a | b;
	return a;
}

inline bool hasNodeType(NodeType flags, NodeType type) {
	return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(type)) != 0;
}

/**
 * @brief Minimal base node type with parent-child structure and lifecycle.
 * Does NOT contain a transform - use SpatialNode for nodes that need transforms.
 */
class Node : public engine::core::Identifiable<Node>, public std::enable_shared_from_this<Node> {
public:
	using Ptr = std::shared_ptr<Node>;
	
	// Allow Scene and SceneManager to set engine context
	friend class engine::scene::Scene;
	friend class engine::scene::SceneManager;

	Node();
	virtual ~Node();

	/** @brief Start the node (called once when enabled for the first time). */
	virtual void start();
	/** @brief Called when node is enabled. */
	virtual void onEnable();
	/** @brief Called when node is disabled. */
	virtual void onDisable();
	/** @brief Called when node is destroyed. */
	virtual void onDestroy();
	
	/** @brief Called during debug rendering to add debug primitives.
	 *  Override this in derived classes to visualize node-specific data.
	 *  @param collector The debug collector to add primitives to.
	 */
	virtual void onDebugDraw(engine::rendering::DebugRenderCollector& collector);

	/** @brief Enable the node. */
	void enable();
	/** @brief Disable the node. */
	void disable();
	/** @brief Is the node enabled? */
	bool isEnabled() const;
	
	/** @brief Enable/disable debug rendering for this node. */
	void setDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
	
	/** @brief Check if debug rendering is enabled for this node. */
	bool isDebugEnabled() const { return m_debugEnabled; }

	/** @brief Add a child node. */
	void addChild(Ptr child);
	/** @brief Remove a child node. */
	void removeChild(Ptr child);
	/** @brief Get parent node. */
	Node* getParent() const;
	/** @brief Get children. */
	const std::vector<Ptr>& getChildren() const;
	
	/** @brief Get the node type flags for this node. */
	NodeType getNodeType() const { return m_nodeType; }
	
	/** @brief Check if this node has a specific type flag. */
	bool hasType(NodeType type) const { 
		return hasNodeType(m_nodeType, type); 
	}
	
	/** @brief Check if this node is a spatial node (has transform). */
	bool isSpatial() const { return hasType(NodeType::Spatial); }
	
	/** @brief Check if this node is a render node. */
	bool isRender() const { return hasType(NodeType::Render); }
	
	/** @brief Check if this node is an update node. */
	bool isUpdate() const { return hasType(NodeType::Update); }
	
	/** @brief Check if this node is a physics node. */
	bool isPhysics() const { return hasType(NodeType::Physics); }
	
	/** @brief Try to cast this node to a RenderNode. Returns nullptr if not a render node. */
	std::shared_ptr<RenderNode> asRenderNode();
	
	/** @brief Try to cast this node to an UpdateNode. Returns nullptr if not an update node. */
	std::shared_ptr<UpdateNode> asUpdateNode();
	
	/** @brief Try to cast this node to a PhysicsNode. Returns nullptr if not a physics node. */
	std::shared_ptr<PhysicsNode> asPhysicsNode();
	
	/** @brief Convert this node to a basic Node::Ptr (useful for adding to parent) */
	template<typename T>
	static Ptr toNodePtr(std::shared_ptr<T> derived) {
		return std::static_pointer_cast<Node>(derived);
	}
	
	/** @brief Convert this node to a basic Node::Ptr (useful for adding to parent) */
	Ptr asNode() {
		return std::static_pointer_cast<Node>(shared_from_this());
	}
	
	/** @brief Get the engine context (access to input, resources, etc.) */
	engine::EngineContext* getEngineContext() const { return m_engineContext; }
	
	/** @brief Convenient read-only access to engine context */
	const engine::EngineContext* engine() const { return m_engineContext; }

protected:
	/** @brief Set the engine context (called by scene/parent) - friend access only */
	void setEngineContext(engine::EngineContext* context);
	
	/** @brief Set the node type flags (to be called by derived classes). */
	void setNodeType(NodeType type) { m_nodeType = type; }
	
	/** @brief Add a node type flag. */
	void addNodeType(NodeType type) { m_nodeType |= type; }

	bool enabled = true;
	bool started = false;
	bool m_debugEnabled = false;
	Node* parent = nullptr;
	std::vector<Ptr> children;
	NodeType m_nodeType = NodeType::Base;
	engine::EngineContext* m_engineContext = nullptr;
};
} // namespace engine::scene::entity
