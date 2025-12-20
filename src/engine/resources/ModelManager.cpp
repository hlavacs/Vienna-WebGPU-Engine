#include "engine/resources/ModelManager.h"

namespace engine::resources
{
ModelManager::ModelManager(
	std::shared_ptr<MeshManager> meshManager,
	std::shared_ptr<MaterialManager> materialManager,
	std::shared_ptr<loaders::ObjLoader> objLoader
) :
	m_meshManager(std::move(meshManager)),
	m_materialManager(std::move(materialManager)),
	m_objLoader(std::move(objLoader)) {}

std::optional<ModelManager::ModelPtr> ModelManager::createModel(const std::string &filePath, const std::string &name)
{
	std::string modelName = filePath;
	auto optModel = getByName(modelName);
	if(optModel.has_value())
		return *optModel;

	if (!m_objLoader || !m_meshManager)
		return std::nullopt;

	auto objDataOpt = m_objLoader->load(filePath, engine::math::CoordinateSystem::Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD, engine::math::CoordinateSystem::DEFAULT);
	if (!objDataOpt)
		return std::nullopt;
	const auto &objData = *objDataOpt;

	// Build Mesh from parsed geometry using the MeshManager
	auto meshOpt = m_meshManager->createMesh(std::move(objData.vertices), std::move(objData.indices), modelName);
	if (!meshOpt || !(*meshOpt))
		return std::nullopt;

	auto &mesh = *meshOpt;
	mesh->computeTangents();

	// Get the mesh handle
	engine::rendering::MeshHandle meshHandle = mesh->getHandle();

	auto model = std::make_shared<engine::rendering::Model>(
		meshHandle,
		filePath,
		modelName
	);

	if (m_materialManager && !objData.materialRanges.empty())
	{
		for (const auto &range : objData.materialRanges)
		{
			if (range.indexCount == 0)
				continue;

			engine::rendering::Submesh submesh;
			submesh.indexOffset = range.indexOffset;
			submesh.indexCount = range.indexCount;

			int matId = range.materialId;
			if (matId >= 0 && matId < static_cast<int>(objData.materials.size()))
			{
				auto matOpt = m_materialManager->createMaterial(objData.materials[matId]);
				if (matOpt && *matOpt)
					submesh.material = (*matOpt)->getHandle();
			}

			model->addSubmesh(submesh);
		}
	}

	auto handleOpt = add(model);
	if (!handleOpt)
		return std::nullopt;

	return model;
}

std::shared_ptr<MeshManager> ModelManager::getMeshManager() const
{
	return m_meshManager;
}

std::shared_ptr<MaterialManager> ModelManager::getMaterialManager() const
{
	return m_materialManager;
}

} // namespace engine::resources
