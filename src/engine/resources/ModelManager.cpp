#include "engine/resources/ModelManager.h"

namespace engine::resources
{
	ModelManager::ModelManager(
		std::shared_ptr<MaterialManager> materialManager,
		std::shared_ptr<loaders::ObjLoader> objLoader)
		: m_materialManager(std::move(materialManager)), m_objLoader(std::move(objLoader)) {}

	std::optional<ModelManager::ModelPtr> ModelManager::createModel(const std::string &filePath, const std::string &name)
	{
		if (!m_objLoader)
			return std::nullopt;
		auto objDataOpt = m_objLoader->load(filePath, true, engine::math::CoordinateSystem::Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD, engine::math::CoordinateSystem::DEFAULT);
		if (!objDataOpt)
			return std::nullopt;
		const auto &objData = *objDataOpt;

		// Build Mesh from parsed geometry
		engine::rendering::Mesh mesh(std::move(objData.vertices), std::move(objData.indices));
		mesh.computeTangents();

		// Try to create a material from the first parsed material, if any
		MaterialHandle matHandle;
		if (m_materialManager && !objData.materials.empty()) {
			auto matOpt = m_materialManager->createMaterial(objData.materials[0]);
			if (matOpt && *matOpt)
				matHandle = (*matOpt)->getHandle();
		}
		// TODO: Multiple materials support

		// Use parsed name if no name provided
		std::string modelName = name.empty() ? objData.name : name;
		auto model = std::make_shared<Model>(std::move(mesh), matHandle, filePath, modelName);
		auto handleOpt = add(model);
		if (!handleOpt)
			return std::nullopt;
		return model;
	}

	std::shared_ptr<MaterialManager> ModelManager::getMaterialManager() const
	{
		return m_materialManager;
	}

} // namespace engine::resources
