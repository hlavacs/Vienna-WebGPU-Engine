#include "engine/scene/SpatialNode.h"

namespace engine::scene {
SpatialNode::SpatialNode() : m_transform(std::make_shared<Transform>()) {
	addNodeType(entity::NodeType::Spatial);
}
} // namespace engine::scene
