#include "engine/scene/SpatialNode.h"

namespace engine::scene {
SpatialNode::SpatialNode() : m_transform(std::make_shared<Transform>()) {}
} // namespace engine::scene
