#pragma once
#include <vector>
#include <memory>
#include "engine/core/Identifiable.h"

namespace engine::scene::entity {
/**
 * @brief Minimal base node type with parent-child structure and lifecycle.
 */
class Node : public engine::core::Identifiable<Node> {
public:
	using Ptr = std::shared_ptr<Node>;

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

	/** @brief Enable the node. */
	void enable();
	/** @brief Disable the node. */
	void disable();
	/** @brief Is the node enabled? */
	bool isEnabled() const;

	/** @brief Add a child node. */
	void addChild(Ptr child);
	/** @brief Remove a child node. */
	void removeChild(Ptr child);
	/** @brief Get parent node. */
	Node* getParent() const;
	/** @brief Get children. */
	const std::vector<Ptr>& getChildren() const;

protected:
	bool enabled = true;
	bool started = false;
	Node* parent = nullptr;
	std::vector<Ptr> children;
};
} // namespace engine::scene::entity
