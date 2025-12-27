
#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Vertex.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/MeshManager.h"
#include "engine/resources/ModelManager.h"
#include "engine/resources/TextureManager.h"
#include "engine/resources/loaders/GltfLoader.h"
#include "engine/resources/loaders/ObjLoader.h"
#include "engine/resources/loaders/ImageLoader.h"

namespace engine::resources
{
class ResourceManager
{
  public:
	// (Just aliases to make notations lighter)
	using path = std::filesystem::path;
	using vec3 = glm::vec3;
	using vec2 = glm::vec2;
	using mat3x3 = glm::mat3x3;

	explicit ResourceManager(path baseDir);
  public:
	std::shared_ptr<engine::resources::loaders::ObjLoader> m_objLoader;
	std::shared_ptr<engine::resources::loaders::GltfLoader> m_gltfLoader;
	std::shared_ptr<engine::resources::loaders::ImageLoader> m_imageLoader;
	std::shared_ptr<engine::resources::TextureManager> m_textureManager;
	std::shared_ptr<engine::resources::MeshManager> m_meshManager;
	std::shared_ptr<engine::resources::MaterialManager> m_materialManager;
	std::shared_ptr<engine::resources::ModelManager> m_modelManager;
};
} // namespace engine::resources