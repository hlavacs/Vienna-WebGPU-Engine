
#pragma once

#include <vector>
#include <filesystem>

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "engine/rendering/Vertex.h"
#include "engine/resources/loaders/ObjLoader.h"
#include "engine/resources/loaders/GltfLoader.h"
#include "engine/resources/loaders/TextureLoader.h"
#include "engine/resources/TextureManager.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/ModelManager.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

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

		// Load a shader from a WGSL file into a new shader module
		static wgpu::ShaderModule loadShaderModule(const path &path, wgpu::Device device);

		// Load an 3D mesh from a standard .obj file into a vertex data buffer
		bool loadGeometryFromObj(const path &path, engine::rendering::Mesh &mesh, bool populateTextureFrame = true);

		// Load an image from a standard image file into a new texture object
		// NB: The texture must be destroyed after use
		wgpu::Texture loadTexture(const path &path, engine::rendering::webgpu::WebGPUContext& context, wgpu::TextureView *pTextureView = nullptr);

		// Load a model using the ModelManager and apply its properties for WebGPU usage
		bool loadModel(const path &modelPath, engine::rendering::webgpu::WebGPUContext& context);

	public:
		std::shared_ptr<engine::resources::loaders::ObjLoader> m_objLoader;
		std::shared_ptr<engine::resources::loaders::GltfLoader> m_gltfLoader;
		std::shared_ptr<engine::resources::loaders::TextureLoader> m_textureLoader;
		std::shared_ptr<engine::resources::TextureManager> m_textureManager;
		std::shared_ptr<engine::resources::MaterialManager> m_materialManager;
		std::shared_ptr<engine::resources::ModelManager> m_modelManager;
	};
}