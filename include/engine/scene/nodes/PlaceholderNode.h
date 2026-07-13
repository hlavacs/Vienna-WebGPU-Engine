#pragma once

#include <string>
#include <utility>

#include "engine/scene/nodes/SpatialNode.h"

namespace engine::scene::nodes
{

/**
 * @brief Stand-in for a node whose type the current build does not know.
 *
 * Created by the scene loader when a saved scene references a type that was
 * never registered (e.g. a custom node compiled into a different build). It
 * keeps the original type name and the node's serialized data verbatim, plus a
 * live transform and its place in the hierarchy, so:
 *  - the scene loads instead of failing,
 *  - nothing is lost: re-saving emits the preserved data unchanged, and opening
 *    the scene in a build that has the real type restores it fully,
 *  - the editor can flag it and offer to replace or remove it.
 *
 * It carries no behaviour - the missing type's C++ code only runs where it is
 * compiled in.
 */
class PlaceholderNode : public SpatialNode
{
  public:
	using Ptr = std::shared_ptr<PlaceholderNode>;

	explicit PlaceholderNode(std::string missingType = {}, std::string preservedProps = "{}") :
		m_missingType(std::move(missingType)), m_preservedProps(std::move(preservedProps))
	{
	}

	~PlaceholderNode() override = default;

	/** @brief The original (unknown-to-this-build) type name. */
	[[nodiscard]] const std::string &getMissingType() const { return m_missingType; }
	void setMissingType(std::string missingType) { m_missingType = std::move(missingType); }

	/** @brief The original type-specific props as raw JSON text, re-emitted on save. */
	[[nodiscard]] const std::string &getPreservedProps() const { return m_preservedProps; }
	void setPreservedProps(std::string preservedProps) { m_preservedProps = std::move(preservedProps); }

  private:
	std::string m_missingType;
	std::string m_preservedProps;
};

} // namespace engine::scene::nodes
