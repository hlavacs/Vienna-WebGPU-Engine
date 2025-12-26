#include "engine/resources/ResourceManager.h"
#include "engine/io/tiny_obj_loader.h"
#include "engine/stb_image.h"

#include <cstring>
#include <fstream>

using namespace wgpu;

namespace engine::resources
{

std::shared_ptr<spdlog::logger> getOrCreateLogger(const std::string &name, spdlog::level::level_enum level = spdlog::level::info)
{
	auto logger = spdlog::get(name);
	if (!logger)
	{
		logger = spdlog::stdout_color_mt(name);
		logger->set_level(level);
	}
	return logger;
}

ResourceManager::ResourceManager(path baseDir)
{
	m_objLoader = std::make_shared<engine::resources::loaders::ObjLoader>(baseDir, getOrCreateLogger("ResourceManager_ObjLoader"));

	m_gltfLoader = std::make_shared<engine::resources::loaders::GltfLoader>(baseDir, getOrCreateLogger("ResourceManager_GltfLoader"));

	m_textureLoader = std::make_shared<engine::resources::loaders::TextureLoader>(baseDir, getOrCreateLogger("ResourceManager_TextureLoader"));

	m_meshManager = std::make_shared<engine::resources::MeshManager>();
	m_textureManager = std::make_shared<engine::resources::TextureManager>(m_textureLoader);
	m_materialManager = std::make_shared<engine::resources::MaterialManager>(m_textureManager);
	m_modelManager = std::make_shared<engine::resources::ModelManager>(
		m_meshManager,
		m_materialManager,
		m_objLoader
	);
}

} // namespace engine::resources