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

std::optional<ModelManager::ModelPtr> ModelManager::createModel(
	const std::string &filePath,
	const std::string &name,
	const engine::math::CoordinateSystem::Cartesian srcCoordSys,
	const engine::math::CoordinateSystem::Cartesian dstCoordSys
)
{
	std::string modelName = filePath;
	auto optModel = getByName(modelName);
	if (optModel.has_value())
		return *optModel;

	if (!m_objLoader || !m_meshManager)
		return std::nullopt;

	auto objDataOpt = m_objLoader->load(filePath, srcCoordSys, dstCoordSys);
	if (!objDataOpt)
		return std::nullopt;
	const auto &objData = *objDataOpt;

	// Build Mesh from parsed geometry using the MeshManager
	auto meshOpt = m_meshManager->createMesh(
		std::move(objData.vertices),
		std::move(objData.indices),
		objData.boundingBox,
		modelName
	);
	if (!meshOpt || !(*meshOpt))
		return std::nullopt;

	auto &mesh = *meshOpt;
	mesh->computeTangents();

	// Get the mesh handle
	engine::rendering::MeshHandle meshHandle = mesh->getHandle();

	auto model = std::make_shared<engine::rendering::Model>(
		meshHandle,
		objData.filePath,
		modelName
	);

	if (m_materialManager && !objData.materialRanges.empty())
	{
		std::filesystem::path textureBasePath = std::filesystem::path(objData.filePath).parent_path();
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
				// ToDo: Wrapper for material properties so other formats can be supported too example gltf
				auto matOpt = m_materialManager->createMaterial(objData.materials[matId], textureBasePath.string() + "/");
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
