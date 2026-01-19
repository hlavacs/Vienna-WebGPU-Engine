#include "engine/resources/ModelManager.h"

namespace engine::resources
{

std::optional<ModelManager::ModelPtr> ModelManager::createModel(
	const std::string &filePath,
	const std::optional<std::string> &name,
	const engine::math::CoordinateSystem::Cartesian srcCoordSys,
	const engine::math::CoordinateSystem::Cartesian dstCoordSys
)
{
	std::string modelName = name.value_or(filePath);
	auto existing = getByName(modelName);
	if (existing.has_value())
		return *existing;

	std::filesystem::path path(filePath);
	auto ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".obj")
	{
		if (!m_objLoader)
			return std::nullopt;

		auto objDataOpt = m_objLoader->load(filePath, srcCoordSys, dstCoordSys);
		if (!objDataOpt)
			return std::nullopt;

		return createModel(*objDataOpt, modelName);
	}
	else if (ext == ".gltf" || ext == ".glb")
	{
		if (!m_gltfLoader)
			return std::nullopt;

		auto gltfDataOpt = m_gltfLoader->load(filePath, srcCoordSys, dstCoordSys);
		if (!gltfDataOpt)
			return std::nullopt;

		return createModel(*gltfDataOpt, modelName);
	}
	else
	{
		logError("Unsupported model format: '{}'", filePath);
		return std::nullopt;
	}
}

// Overload: create model from ObjGeometryData
std::optional<ModelManager::ModelPtr> ModelManager::createModel(
	const engine::resources::ObjGeometryData &objData,
	const std::optional<std::string> &name
)
{
	std::string modelName = name.value_or(objData.name);

	if (!m_meshManager)
		return std::nullopt;

	// Build Mesh from parsed geometry
	auto meshOpt = m_meshManager->createMesh(
		objData.vertices,
		objData.indices,
		objData.boundingBox,
		modelName
	);
	if (!meshOpt || !(*meshOpt))
		return std::nullopt;

	auto &mesh = *meshOpt;
	mesh->computeTangents();

	auto meshHandle = mesh->getHandle();

	auto model = std::make_shared<engine::rendering::Model>(
		meshHandle,
		objData.filePath,
		modelName
	);

	// Assign materials if available
	if (m_materialManager && !objData.materials.empty())
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
				auto matOpt = m_materialManager->createMaterial(objData.materials[matId], textureBasePath.string() + "/");
				if (matOpt && *matOpt)
					submesh.material = (*matOpt)->getHandle();
			} else {
				// Use default material if no valid material assigned
				submesh.material = m_materialManager->getDefaultMaterial();
			}

			model->addSubmesh(submesh);
		}
	}

	if (model->getSubmeshCount() == 0)
	{
		engine::rendering::Submesh submesh;
		submesh.indexOffset = 0;
		submesh.indexCount  = static_cast<uint32_t>(objData.indices.size());
		submesh.material    = m_materialManager ? m_materialManager->getDefaultMaterial() : engine::rendering::MaterialHandle{};
		model->addSubmesh(submesh);
	}

	auto handleOpt = add(model);
	if (!handleOpt)
		return std::nullopt;

	return model;
}

// Overload: create model from GltfGeometryData
std::optional<ModelManager::ModelPtr> ModelManager::createModel(
	const engine::resources::GltfGeometryData &gltfData,
	const std::optional<std::string> &name
)
{
	std::string modelName = name.value_or(gltfData.name);

	if (!m_meshManager)
		return std::nullopt;

	// Build a mesh per primitive
	std::vector<engine::rendering::MeshHandle> meshHandles;
	for (const auto &prim : gltfData.primitives)
	{
		if (prim.indexCount == 0)
			continue;

		auto meshOpt = m_meshManager->createMesh(
			std::vector<engine::rendering::Vertex>(gltfData.vertices.begin() + prim.vertexOffset, gltfData.vertices.begin() + prim.vertexOffset + prim.vertexCount),
			std::vector<uint32_t>(gltfData.indices.begin() + prim.indexOffset, gltfData.indices.begin() + prim.indexOffset + prim.indexCount),
			gltfData.boundingBox,
			modelName
		);
		if (!meshOpt || !(*meshOpt))
			continue;

		auto &mesh = *meshOpt;
		mesh->computeTangents();
		meshHandles.push_back(mesh->getHandle());
	}

	if (meshHandles.empty())
		return std::nullopt;

	auto model = std::make_shared<engine::rendering::Model>(
		meshHandles.front(), // or handle multiple meshes inside Model if supported
		gltfData.filePath,
		modelName
	);

	// Assign submeshes & materials
	if (m_materialManager && gltfData.materialContext && !gltfData.materialContext->materials.empty())
	{
		std::filesystem::path textureBasePath = std::filesystem::path(gltfData.filePath).parent_path();
		for (size_t i = 0; i < gltfData.primitives.size(); ++i)
		{
			const auto &prim = gltfData.primitives[i];
			if (prim.indexCount == 0)
				continue;

			engine::rendering::Submesh submesh;
			submesh.indexOffset = prim.indexOffset;
			submesh.indexCount = prim.indexCount;

			int matId = prim.materialId;
			if (matId >= 0 && matId < static_cast<int>(gltfData.materialContext->materials.size()))
			{
				auto matOpt = m_materialManager->createMaterial(
					gltfData.materialContext->materials[matId],
					gltfData.materialContext->textures,
					gltfData.materialContext->images,
					textureBasePath.string() + "/"
				);
				if (matOpt && matOpt.value())
					submesh.material = matOpt.value()->getHandle();
			} else {
				// Use default material if no valid material assigned
				submesh.material = m_materialManager->getDefaultMaterial();
			}

			model->addSubmesh(submesh);
		}
	}

	if (model->getSubmeshCount() == 0)
	{
		engine::rendering::Submesh submesh;
		submesh.indexOffset = 0;
		submesh.indexCount  = static_cast<uint32_t>(gltfData.indices.size());
		submesh.material    = m_materialManager ? m_materialManager->getDefaultMaterial() : engine::rendering::MaterialHandle{};
		model->addSubmesh(submesh);
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
