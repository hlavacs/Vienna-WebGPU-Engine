#pragma once

#include <nlohmann/json.hpp>

namespace engine::scene::nodes
{
class Node;
}

namespace engine::reflection
{
/// Serialize a node's reflected fields (via Node::reflect) into @p out.
void reflectToJson(const engine::scene::nodes::Node &node, nlohmann::json &out);

/// Restore a node's reflected fields from @p in (missing keys keep their default).
void reflectFromJson(engine::scene::nodes::Node &node, const nlohmann::json &in);

} // namespace engine::reflection
