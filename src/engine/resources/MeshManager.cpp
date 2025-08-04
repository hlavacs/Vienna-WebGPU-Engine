#include "engine/resources/MeshManager.h"

namespace engine::resources
{
std::optional<MeshManager::MeshPtr> MeshManager::createMesh(
	std::vector<engine::rendering::Vertex> vertices,
	std::vector<uint32_t> indices,
	const std::string &name
)
{
	auto mesh = std::make_shared<engine::rendering::Mesh>(std::move(vertices), std::move(indices));
	if (!name.empty())
	{
		mesh->setName(name);
	}

	auto handleOpt = add(mesh);
	if (!handleOpt)
	{
		return std::nullopt;
	}

	return mesh;
}

std::optional<MeshManager::MeshPtr> MeshManager::createEmptyMesh(const std::string &name)
{
	auto mesh = std::make_shared<engine::rendering::Mesh>();
	if (!name.empty())
	{
		mesh->setName(name);
	}

	auto handleOpt = add(mesh);
	if (!handleOpt)
	{
		return std::nullopt;
	}

	return mesh;
}

} // namespace engine::resources