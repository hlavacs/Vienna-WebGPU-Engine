#pragma once

#include "engine/math/AABB.h"
#include "engine/rendering/Mesh.h"
#include "engine/resources/ResourceManagerBase.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine::resources
{
/**
 * @class MeshManager
 * @brief Manages creation and lifetime of Mesh resources
 */
class MeshManager : public engine::resources::ResourceManagerBase<engine::rendering::Mesh>
{
  public:
	using MeshPtr = std::shared_ptr<engine::rendering::Mesh>;

	/**
	 * @brief Create a mesh from vertices and indices
	 * @param vertices Vector of vertices
	 * @param indices Vector of indices
	 * @param name Optional name for the mesh
	 * @return Optional containing the created mesh if successful
	 */
	std::optional<MeshPtr> createMesh(
		std::vector<engine::rendering::Vertex> vertices,
		std::vector<uint32_t> indices,
		engine::math::AABB boundingBox = engine::math::AABB(),
		const std::string &name = ""
	);

	/**
	 * @brief Create an empty mesh
	 * @param name Optional name for the mesh
	 * @return Optional containing the created mesh if successful
	 */
	std::optional<MeshPtr> createEmptyMesh(const std::string &name = "");
};

} // namespace engine::resources