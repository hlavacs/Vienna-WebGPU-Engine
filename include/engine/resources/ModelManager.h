#pragma once

#include "engine/rendering/Model.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/MeshManager.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/loaders/GltfLoader.h"
#include "engine/resources/loaders/ObjLoader.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::resources
{
/**
 * @class ModelManager
 * @brief Manages creation and lifetime of Model resources
 *
 * @details
 * - Every created Model is guaranteed to be renderable.
 * - Every Submesh will always have a valid MaterialHandle assigned.
 * - If no material data is provided by the source asset, a default
 *   engine material (e.g. magenta error material) is assigned automatically.
 */
class ModelManager : public engine::resources::ResourceManagerBase<engine::rendering::Model>
{
  public:
	using ModelPtr = std::shared_ptr<engine::rendering::Model>;

	/**
	 * @brief Construct a ModelManager with dependencies
	 * @param meshManager Mesh manager for creating/managing meshes
	 * @param materialManager Material manager for creating/managing materials
	 * @param objLoader OBJ file loader
	 * @param gltfLoader glTF file loader
	 */
	explicit ModelManager(
		std::shared_ptr<MeshManager> meshManager,
		std::shared_ptr<MaterialManager> materialManager,
		std::shared_ptr<loaders::ObjLoader> objLoader,
		std::shared_ptr<loaders::GltfLoader> gltfLoader
	) :
		m_meshManager(std::move(meshManager)),
		m_materialManager(std::move(materialManager)),
		m_objLoader(std::move(objLoader)),
		m_gltfLoader(std::move(gltfLoader))
	{
	}

	/**
	 * @brief Create a model from a file path
	 * @param filePath Path to the model file
	 * @param name Optional name for the model
	 * @param srcCoordSys Source coordinate system of the model file
	 * @param dstCoordSys Destination coordinate system for the model
	 * @return Optional containing the created model if successful
	 */
	std::optional<ModelPtr> createModel(
		const std::filesystem::path &filePath,
		const std::optional<std::string> &name = std::nullopt,
		const engine::math::CoordinateSystem::Cartesian srcCoordSys = engine::math::CoordinateSystem::Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD,
		const engine::math::CoordinateSystem::Cartesian dstCoordSys = engine::math::CoordinateSystem::DEFAULT
	);

	/**
	 * @brief Create a model from parsed OBJ geometry data
	 *
	 * @details
	 * - If the OBJ file does not specify any materials, a single submesh
	 *   covering the full index range is created.
	 * - A default engine material is assigned in this case.
	 *
	 * @param objData Parsed geometry and material data from an OBJ file
	 * @param name Optional name for the model
	 * @return Optional containing the created model if successful
	 */
	std::optional<ModelPtr> createModel(
		const engine::resources::ObjGeometryData &objData,
		const std::optional<std::string> &name = std::nullopt
	);

	/**
	 * @brief Create a model from parsed glTF geometry data
	 *
	 * @details
	 * - Each primitive results in a submesh.
	 * - If a primitive does not reference a valid material, the default
	 *   engine material is assigned.
	 *
	 * @param gltfData Parsed geometry, materials, and optional animations from a glTF file
	 * @param name Optional name for the model
	 * @return Optional containing the created model if successful
	 */
	std::optional<ModelPtr> createModel(
		const engine::resources::GltfGeometryData &gltfData,
		const std::optional<std::string> &name = std::nullopt
	);

	/**
	 * @brief Get the mesh manager
	 * @return Shared pointer to the mesh manager
	 */
	std::shared_ptr<MeshManager> getMeshManager() const;

	/**
	 * @brief Get the material manager
	 * @return Shared pointer to the material manager
	 */
	std::shared_ptr<MaterialManager> getMaterialManager() const;

  private:
	std::shared_ptr<MeshManager> m_meshManager;
	std::shared_ptr<MaterialManager> m_materialManager;
	std::shared_ptr<loaders::ObjLoader> m_objLoader;
	std::shared_ptr<loaders::GltfLoader> m_gltfLoader;
};

} // namespace engine::resources
