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

	ResourceManager::ResourceManager(std::filesystem::path baseDir)
	{
		auto loggerObjLoader = spdlog::get("ObjLoader");
		if (!loggerObjLoader)
		{
			loggerObjLoader = spdlog::stdout_color_mt("ObjLoader");
			loggerObjLoader->set_level(spdlog::level::info); // Default log level
		}
		m_objLoader = std::make_unique<engine::resources::ObjLoader>(baseDir, loggerObjLoader);
	}

	ShaderModule ResourceManager::loadShaderModule(const path &path, Device device)
	{
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
	

	static void calculateTangentBitangent(
		const glm::vec3 &pos1, const glm::vec3 &pos2, const glm::vec3 &pos3,
		const glm::vec2 &uv1, const glm::vec2 &uv2, const glm::vec2 &uv3,
		glm::vec3 &outTangent, glm::vec3 &outBitangent)
	{
		glm::vec3 edge1 = pos2 - pos1;
		glm::vec3 edge2 = pos3 - pos1;

		glm::vec2 deltaUV1 = uv2 - uv1;
		glm::vec2 deltaUV2 = uv3 - uv1;

		float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

		outTangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
		outTangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
		outTangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

		outBitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
		outBitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
		outBitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
	}

	static void computeTangents(engine::rendering::Mesh &mesh)
	{
		// Reset tangents and bitangents
		for (auto &v : mesh.vertices)
		{
			v.tangent = glm::vec3(0.0f);
			v.bitangent = glm::vec3(0.0f);
		}

		for (size_t i = 0; i < mesh.indices.size(); i += 3)
		{
			engine::rendering::Vertex &v0 = mesh.vertices[mesh.indices[i]];
			engine::rendering::Vertex &v1 = mesh.vertices[mesh.indices[i + 1]];
			engine::rendering::Vertex &v2 = mesh.vertices[mesh.indices[i + 2]];

			glm::vec3 tangent, bitangent;
			calculateTangentBitangent(v0.position, v1.position, v2.position,
									  v0.uv, v1.uv, v2.uv,
									  tangent, bitangent);

			v0.tangent += tangent;
			v1.tangent += tangent;
			v2.tangent += tangent;

			v0.bitangent += bitangent;
			v1.bitangent += bitangent;
			v2.bitangent += bitangent;
		}

		// Orthonormalize per vertex
		for (auto &v : mesh.vertices)
		{
			v.tangent = glm::normalize(v.tangent - v.normal * glm::dot(v.normal, v.tangent));
			float handedness = (glm::dot(glm::cross(v.normal, v.tangent), v.bitangent) < 0.0f) ? -1.0f : 1.0f;
			v.bitangent = handedness * glm::cross(v.normal, v.tangent);
		}
	}


	bool ResourceManager::loadGeometryFromObj(const path &path, engine::rendering::Mesh &mesh, bool populateTextureFrame)
	{
		bool indexed = false;
		auto meshResult = m_objLoader->load(path, indexed);
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
		Device device,
		Texture texture,
		Extent3D textureSize,
		uint32_t mipLevelCount,
		const unsigned char *pixelData)
	{
		Queue queue = device.getQueue();

		// Arguments telling which part of the texture we upload to
		ImageCopyTexture destination;
		destination.texture = texture;
		destination.origin = {0, 0, 0};
		destination.aspect = TextureAspect::All;

		// Arguments telling how the C++ side pixel memory is laid out
		TextureDataLayout source;
		source.offset = 0;

		// Create image data
		Extent3D mipLevelSize = textureSize;
		std::vector<unsigned char> previousLevelPixels;
		Extent3D previousMipLevelSize;
		for (uint32_t level = 0; level < mipLevelCount; ++level)
		{
			// Pixel data for the current level
			std::vector<unsigned char> pixels(4 * mipLevelSize.width * mipLevelSize.height);
			if (level == 0)
			{
				// We cannot really avoid this copy since we need this
				// in previousLevelPixels at the next iteration
				memcpy(pixels.data(), pixelData, pixels.size());
			}
			else
			{
				// Create mip level data
				for (uint32_t i = 0; i < mipLevelSize.width; ++i)
				{
					for (uint32_t j = 0; j < mipLevelSize.height; ++j)
					{
						unsigned char *p = &pixels[4 * (j * mipLevelSize.width + i)];
						// Get the corresponding 4 pixels from the previous level
						unsigned char *p00 = &previousLevelPixels[4 * ((2 * j + 0) * previousMipLevelSize.width + (2 * i + 0))];
						unsigned char *p01 = &previousLevelPixels[4 * ((2 * j + 0) * previousMipLevelSize.width + (2 * i + 1))];
						unsigned char *p10 = &previousLevelPixels[4 * ((2 * j + 1) * previousMipLevelSize.width + (2 * i + 0))];
						unsigned char *p11 = &previousLevelPixels[4 * ((2 * j + 1) * previousMipLevelSize.width + (2 * i + 1))];
						// Average
						p[0] = (p00[0] + p01[0] + p10[0] + p11[0]) / 4;
						p[1] = (p00[1] + p01[1] + p10[1] + p11[1]) / 4;
						p[2] = (p00[2] + p01[2] + p10[2] + p11[2]) / 4;
						p[3] = (p00[3] + p01[3] + p10[3] + p11[3]) / 4;
					}
				}
			}

			// Upload data to the GPU texture
			destination.mipLevel = level;
			source.bytesPerRow = 4 * mipLevelSize.width;
			source.rowsPerImage = mipLevelSize.height;
			queue.writeTexture(destination, pixels.data(), pixels.size(), source, mipLevelSize);

			previousLevelPixels = std::move(pixels);
			previousMipLevelSize = mipLevelSize;
			mipLevelSize.width /= 2;
			mipLevelSize.height /= 2;
		}

		queue.release();
	}

	// Equivalent of std::bit_width that is available from C++20 onward
	static uint32_t bit_width(uint32_t m)
	{
		if (m == 0)
			return 0;
		else
		{
			uint32_t w = 0;
			while (m >>= 1)
				++w;
			return w;
		}
	}

	Texture ResourceManager::loadTexture(const path &path, Device device, TextureView *pTextureView)
	{
		int width, height, channels;
		unsigned char *pixelData = stbi_load(path.string().c_str(), &width, &height, &channels, 4 /* force 4 channels */);
		// If data is null, loading failed.
		if (nullptr == pixelData)
			return nullptr;

		// Use the width, height, channels and data variables here
		TextureDescriptor textureDesc;
		textureDesc.dimension = TextureDimension::_2D;
		textureDesc.format = TextureFormat::RGBA8Unorm; // by convention for bmp, png and jpg file. Be careful with other formats.
		textureDesc.size = {(unsigned int)width, (unsigned int)height, 1};
		textureDesc.mipLevelCount = bit_width(std::max(textureDesc.size.width, textureDesc.size.height));
		textureDesc.sampleCount = 1;
		textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
		textureDesc.viewFormatCount = 0;
		textureDesc.viewFormats = nullptr;
		Texture texture = device.createTexture(textureDesc);

		// Upload data to the GPU texture
		writeMipMaps(device, texture, textureDesc.size, textureDesc.mipLevelCount, pixelData);

		stbi_image_free(pixelData);
		// (Do not use data after this)

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
}