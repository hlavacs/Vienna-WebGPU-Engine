#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include "engine/rendering/Model.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/loaders/ObjLoader.h"

namespace engine::resources
{
	/**
	 * @class ModelManager
	 * @brief Manages creation, storage, and retrieval of models within the engine.
	 *
	 * Provides facilities to add and retrieve models, deduplicate by name, and resolve material handles
	 * using the MaterialManager. Models are stored as shared pointers for safe usage across subsystems.
	 * Currently only supports OBJ models.
	 */
	class ModelManager : public ResourceManagerBase<engine::rendering::Model>
	{
	public:
		using Model = engine::rendering::Model;
		using ModelHandle = Model::Handle;
		using ModelPtr = std::shared_ptr<Model>;
		using MaterialHandle = engine::core::Handle<engine::rendering::Material>;

		/**
		 * @brief Constructs a ModelManager with a given MaterialManager and ObjLoader.
		 * @param materialManager Shared pointer to a MaterialManager.
		 * @param objLoader Shared pointer to an ObjLoader.
		 */
		ModelManager(
			std::shared_ptr<MaterialManager> materialManager,
			std::shared_ptr<engine::resources::loaders::ObjLoader> objLoader);

		/**
		 * @brief Creates a Model from an OBJ file and adds it to the manager.
		 * @param objFilePath The path to the OBJ file.
		 * @param name Optional name for the model.
		 * @return Optional shared pointer to the created model, or std::nullopt on failure.
		 */
		[[nodiscard]]
		std::optional<ModelPtr> createModel(const std::string &objFilePath, const std::string &name = "");

		/**
		 * @brief Access the underlying MaterialManager.
		 * @return Shared pointer to the MaterialManager.
		 */
		[[nodiscard]]
		std::shared_ptr<MaterialManager> getMaterialManager() const;

	private:
		std::shared_ptr<MaterialManager> m_materialManager;
		std::shared_ptr<loaders::ObjLoader> m_objLoader;
	};

} // namespace engine::resources
