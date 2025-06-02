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
		m_objLoader = std::make_unique<engine::resources::loaders::ObjLoader>(baseDir, getOrCreateLogger("ResourceManager_ObjLoader"));

		m_gltfLoader = std::make_unique<engine::resources::loaders::GltfLoader>(baseDir, getOrCreateLogger("ResourceManager_GltfLoader"));

		m_textureLoader = std::make_unique<engine::resources::loaders::TextureLoader>(baseDir, getOrCreateLogger("ResourceManager_TextureLoader"));
		m_textureManager = std::make_unique<engine::resources::TextureManager>(std::move(m_textureLoader));
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

	wgpu::Texture ResourceManager::loadTexture(const path &file, Device device, TextureView *pTextureView)
	{
		// ToDo: Texture Factory in some way
		auto texDataOpt = m_textureManager->createTextureFromFile(file);
		if (!texDataOpt.has_value())
		{
			return nullptr;
		}

		auto &texData = texDataOpt.value(); 
		if(!texData->isMipped())
			texData->generateMipmaps();

		// Use the width, height, channels and data variables here
		TextureDescriptor textureDesc;
		textureDesc.dimension = TextureDimension::_2D;
		// ToDo: Texture Format based on jpeg/png/...
		textureDesc.format = TextureFormat::RGBA8Unorm; // by convention for bmp, png and jpg file. Be careful with other formats.
		textureDesc.size = {texData->getWidth(), texData->getHeight(), 1};
		textureDesc.mipLevelCount = texData->getMipLevelCount();
		textureDesc.sampleCount = 1;
		textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
		textureDesc.viewFormatCount = 0;
		textureDesc.viewFormats = nullptr;
		Texture texture = device.createTexture(textureDesc);

		// Upload data to the GPU texture
		writeMipMaps(device, texture, texData);

		if (pTextureView)
		{
			TextureViewDescriptor textureViewDesc;
			textureViewDesc.aspect = TextureAspect::All;
			textureViewDesc.baseArrayLayer = 0;
			textureViewDesc.arrayLayerCount = 1;
			textureViewDesc.baseMipLevel = 0;
			textureViewDesc.mipLevelCount = textureDesc.mipLevelCount;
			textureViewDesc.dimension = TextureViewDimension::_2D;
			textureViewDesc.format = textureDesc.format;
			*pTextureView = texture.createView(textureViewDesc);
		}

		return texture;
	}

	wgpu::Texture ResourceManager::createNeutralNormalTexture(wgpu::Device device, wgpu::TextureView *pTextureView)
	{
		// 1x1 RGBA texture (neutral normal: (0.5, 0.5, 1.0))
		const uint8_t pixel[4] = {128, 128, 255, 255};

		// Create the texture
		wgpu::TextureDescriptor textureDesc = {};
		textureDesc.dimension = wgpu::TextureDimension::_2D;
		textureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
		textureDesc.size = {1, 1, 1};
		textureDesc.mipLevelCount = 1;
		textureDesc.sampleCount = 1;
		textureDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;

		wgpu::Texture texture = device.createTexture(textureDesc);

		// Upload the pixel data
		wgpu::ImageCopyTexture dst = {};
		dst.texture = texture;
		dst.mipLevel = 0;
		dst.origin = {0, 0, 0};
		dst.aspect = wgpu::TextureAspect::All;

		wgpu::TextureDataLayout layout = {};
		layout.offset = 0;
		layout.bytesPerRow = 4;
		layout.rowsPerImage = 1;

		wgpu::Extent3D extent = {1, 1, 1};

		device.getQueue().writeTexture(dst, pixel, sizeof(pixel), layout, extent);

		// Create texture view if requested
		if (pTextureView)
		{
			wgpu::TextureViewDescriptor viewDesc = {};
			viewDesc.dimension = wgpu::TextureViewDimension::_2D;
			viewDesc.format = textureDesc.format;
			viewDesc.baseMipLevel = 0;
			viewDesc.mipLevelCount = 1;
			viewDesc.baseArrayLayer = 0;
			viewDesc.arrayLayerCount = 1;
			viewDesc.aspect = wgpu::TextureAspect::All;

			*pTextureView = texture.createView(viewDesc);
		}

		return texture;
	}

}