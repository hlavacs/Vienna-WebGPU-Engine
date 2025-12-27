#include "engine/resources/ResourceManager.h"
#include "engine/io/tiny_obj_loader.h"
#include "engine/stb_image.h"

#include <cstring>
#include <fstream>

using namespace wgpu;

namespace engine::resources
{

ResourceManager::ResourceManager(path baseDir)
{
	m_objLoader = std::make_shared<engine::resources::loaders::ObjLoader>(baseDir);

	m_gltfLoader = std::make_shared<engine::resources::loaders::GltfLoader>(baseDir);

	m_imageLoader = std::make_shared<engine::resources::loaders::ImageLoader>(baseDir);

	m_meshManager = std::make_shared<engine::resources::MeshManager>();
	m_textureManager = std::make_shared<engine::resources::TextureManager>(m_imageLoader);
	m_materialManager = std::make_shared<engine::resources::MaterialManager>(m_textureManager);
	m_modelManager = std::make_shared<engine::resources::ModelManager>(
		m_meshManager,
		m_materialManager,
		m_objLoader
	);
}

} // namespace engine::resources