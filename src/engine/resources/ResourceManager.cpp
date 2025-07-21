

#include "engine/resources/ResourceManager.h"
#include "engine/stb_image.h"
#include "engine/io/tiny_obj_loader.h"

#include <fstream>
#include <cstring>

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

		m_textureManager = std::make_shared<engine::resources::TextureManager>(m_textureLoader);
		m_materialManager = std::make_shared<engine::resources::MaterialManager>(m_textureManager);
		m_modelManager = std::make_shared<engine::resources::ModelManager>(
			m_materialManager,
			m_objLoader);
	}

	ShaderModule ResourceManager::loadShaderModule(const path &path, Device device)
	{
		// ToDo: Shader Factory in some way
		std::ifstream file(path);
		if (!file.is_open())
		{
			return nullptr;
		}
		file.seekg(0, std::ios::end);
		size_t size = file.tellg();
		std::string shaderSource(size, ' ');
		file.seekg(0);
		file.read(shaderSource.data(), size);

		ShaderModuleWGSLDescriptor shaderCodeDesc;
		shaderCodeDesc.chain.next = nullptr;
		shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
		shaderCodeDesc.code = shaderSource.c_str();
		ShaderModuleDescriptor shaderDesc;
		shaderDesc.nextInChain = &shaderCodeDesc.chain;
#ifdef WEBGPU_BACKEND_WGPU
		shaderDesc.hintCount = 0;
		shaderDesc.hints = nullptr;
#endif

		return device.createShaderModule(shaderDesc);
	}

	bool ResourceManager::loadGeometryFromObj(const path &path, engine::rendering::Mesh &mesh, bool populateTextureFrame)
	{
		// Load the model using the ModelManager
		if (!m_modelManager)
			return false;

		std::string modelName = path.filename().string();
		auto modelOpt = m_modelManager->createModel(path.string(), modelName);
		if (!modelOpt)
			return false;
		auto &model = *modelOpt;

		// Extract the mesh from the model
		mesh = model->getMesh();
		if (populateTextureFrame)
		{
			mesh.computeTangents();
		}
		return true;
	}

	wgpu::Texture ResourceManager::loadTexture(const path &file, engine::rendering::webgpu::WebGPUContext &context, wgpu::TextureView *pTextureView)
	{
		// Use TextureFactory to create a WebGPUTexture
		auto texDataOpt = m_textureManager->createTextureFromFile(file);
		if (!texDataOpt.has_value())
			return nullptr;
		auto &texData = texDataOpt.value();
		if (!texData->isMipped())
			texData->generateMipmaps();
		// Create GPU texture via factory
		auto gpuTexture = context.textureFactory().createFrom(*texData);
		if (pTextureView)
		{
			*pTextureView = gpuTexture->getTextureView();
		}
		return gpuTexture->getTexture();
	}

	// Loads a model using the ModelManager and applies its properties for WebGPU usage
	bool ResourceManager::loadModel(const path &modelPath, engine::rendering::webgpu::WebGPUContext &context)
	{
		// For now, only OBJ is supported
		if (!m_modelManager)
			return false;

		// Try to find a material for the model (for demo, just use the first material if any)
		auto materialManager = m_modelManager->getMaterialManager();
		engine::core::Handle<engine::rendering::Material> materialHandle;
		if (materialManager)
		{
			// Use the first material if available, else an invalid handle
			auto allMaterials = materialManager->getAll();
			if (!allMaterials.empty())
				materialHandle = allMaterials.front()->getHandle();
		}

		// Use the filename as the model name
		std::string modelName = modelPath.filename().string();
		auto modelOpt = m_modelManager->createModel(modelPath.string(), modelName);
		if (!modelOpt)
			return false;
		auto &model = *modelOpt;

		// --- WebGPU application logic ---
		// 1. Upload mesh data to GPU buffers
		const auto &mesh = model->getMesh();
		auto gpuMesh = context.meshFactory().createFrom(mesh);
		// 2. Get material and associated textures
		const auto &matHandle = model->getMaterial();
		std::shared_ptr<engine::rendering::Material> materialPtr;
		if (materialManager && matHandle.valid())
			materialPtr = materialManager->get(matHandle).value_or(nullptr);

		// 3. For each texture in the material, create a WebGPU texture if needed
		std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> albedoTexture;
		if (materialPtr && materialPtr->hasAlbedoTexture())
		{
			auto texPtr = m_textureManager->get(materialPtr->getAlbedoTexture()).value_or(nullptr);
			if (texPtr)
			{
				albedoTexture = context.textureFactory().createFrom(*texPtr);
			}
		}
		// Repeat for other textures as needed (normal, metallic, etc.)
		// ...

		// 4. Set up bind groups, pipeline, etc. as needed for rendering
		// (This is application-specific and depends on your rendering setup)

		return true;
	}

}