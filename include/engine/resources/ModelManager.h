#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include "engine/rendering/Model.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/MeshManager.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/loaders/ObjLoader.h"

namespace engine::resources
{
	/**
	 * @class ModelManager
	 * @brief Manages creation and lifetime of Model resources
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
		 */
		ModelManager(
			std::shared_ptr<MeshManager> meshManager,
			std::shared_ptr<MaterialManager> materialManager,
			std::shared_ptr<loaders::ObjLoader> objLoader);

		/**
		 * @brief Create a model from a file path
		 * @param filePath Path to the model file
		 * @param name Optional name for the model
		 * @return Optional containing the created model if successful
		 */
		std::optional<ModelPtr> createModel(const std::string &filePath, const std::string &name = "");

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
	};

} // namespace engine::resources
