/**
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://github.com/eliemichel/LearnWebGPU
 *
 * MIT License
 * Copyright (c) 2022-2023 Elie Michel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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

		m_textureManager = std::make_shared<engine::resources::TextureManager>(std::move(m_textureLoader));
		m_materialManager = std::make_shared<engine::resources::MaterialManager>(std::move(m_textureLoader));
		m_modelManager = std::make_shared<engine::resources::ModelManager>(
			std::move(m_materialManager),
			std::move(m_objLoader));
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
		// ToDo: Mesh Factory in some way
		bool indexed = false;
		std::optional<engine::rendering::Mesh> meshResult = std::nullopt;
		if (path.extension() == ".obj")
		{
			meshResult = m_objLoader->load(path, indexed);
		}
		else if (path.extension() == ".gltf" || path.extension() == ".glb")
		{
			meshResult = m_gltfLoader->load(path, indexed);
		}
		else
		{
			// ToDo: Log
			return false;
		}
		if (!meshResult)
			return false;

		mesh = std::move(*meshResult);
		if (populateTextureFrame)
		{
			mesh.computeTangents();
		}

		return true;
	}

	// Auxiliary function for loadTexture
	static void writeMipMaps(
		wgpu::Device device,
		wgpu::Texture texture,
		const engine::rendering::Texture::Ptr &tex)
	{
		wgpu::Queue queue = device.getQueue();

		ImageCopyTexture destination;
		destination.texture = texture;
		destination.origin = {0, 0, 0};
		destination.aspect = wgpu::TextureAspect::All;

		auto &mipmaps = tex->getMipmaps();
		auto width = tex->getWidth();
		auto height = tex->getHeight();

		TextureDataLayout textureDataLayout{};
		textureDataLayout.offset = 0;

		for (uint32_t level = 0; level < mipmaps.size(); ++level)
		{
			// Calculate mip size for this level
			uint32_t mipWidth = std::max(1u, width >> level);
			uint32_t mipHeight = std::max(1u, height >> level);

			textureDataLayout.bytesPerRow = tex->getChannels() * mipWidth;
			textureDataLayout.rowsPerImage = mipHeight;

			destination.mipLevel = level;

			// Upload mip level data directly
			queue.writeTexture(
				destination,
				mipmaps[level].data(),
				mipmaps[level].size(),
				textureDataLayout,
				{mipWidth, mipHeight, 1});
		}

		queue.release();
	}

	wgpu::Texture ResourceManager::loadTexture(const path &file, engine::rendering::webgpu::WebGPUContext& context, wgpu::TextureView *pTextureView)
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
		if (pTextureView) {
			*pTextureView = gpuTexture->getTextureView();
		}
		return gpuTexture->getTexture();
	}

	wgpu::Texture ResourceManager::createNeutralNormalTexture(engine::rendering::webgpu::WebGPUContext& context, wgpu::TextureView *pTextureView)
	{
		// 1x1 RGBA texture (neutral normal: (0.5, 0.5, 1.0))
		engine::rendering::Texture neutralTex(1, 1, 4);
		neutralTex.setPixel(0, 0, {128, 128, 255, 255});
		auto gpuTexture = context.textureFactory().createFrom(neutralTex);
		if (pTextureView) {
			*pTextureView = gpuTexture->getTextureView();
		}
		return gpuTexture->getTexture();
	}

	// Loads a model using the ModelManager and applies its properties for WebGPU usage
	bool ResourceManager::loadModel(const path &modelPath, engine::rendering::webgpu::WebGPUContext& context)
	{
		// For now, only OBJ is supported
		if (!m_modelManager)
			return false;

		// Try to find a material for the model (for demo, just use the first material if any)
		auto materialManager = m_modelManager->getMaterialManager();
		engine::core::Handle<engine::rendering::Material> materialHandle;
		if (materialManager) {
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
		if (materialPtr && materialPtr->albedoTexture.valid()) {
			auto texPtr = m_textureManager->get(materialPtr->albedoTexture).value_or(nullptr);
			if (texPtr) {
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