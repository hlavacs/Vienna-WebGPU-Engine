// Sample project script: a self-registering "Rotator" behaviour.
//
// Copy this (or write your own) into your project's `scripts/` folder next to your
// .vproj. Build Game compiles every .cpp there into the game; VIENNA_REGISTER_NODE
// adds the type, and the public fields listed in reflect() are automatically saved
// to the scene, restored on load (keeping their defaults for anything absent), and
// shown as editable rows in the inspector - one field list drives all three.

#include <glm/glm.hpp>

#include "engine/reflection/Reflector.h"
#include "engine/scene/NodeTypeRegistry.h"
#include "engine/scene/nodes/SpatialNode.h"
#include "engine/scene/nodes/UpdateNode.h"

namespace
{
using engine::scene::nodes::SpatialNode;
using engine::scene::nodes::UpdateNode;

/// Spins its node around @ref axis. SpatialNode gives it a transform, UpdateNode a
/// per-frame update() (both virtually inherit Node, so there is one shared Node).
class Rotator : public SpatialNode, public UpdateNode
{
  public:
	glm::vec3 axis{0.0f, 1.0f, 0.0f}; // default start values, like Unity's serialized fields
	float degreesPerSecond = 45.0f;

	void update(float deltaTime) override
	{
		getTransform().rotate(axis * degreesPerSecond * deltaTime, true);
	}

	// List each field once; the engine handles save + load + inspector from it.
	// Pick a widget where it helps: r.range() for a slider, r.color() for a picker.
	void reflect(engine::reflection::Reflector &r) override
	{
		r("axis", axis);
		r.range("degreesPerSecond", degreesPerSecond, -360.0f, 360.0f);
	}
};

REGISTER_NODE(Rotator);
} // namespace
