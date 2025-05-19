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

#pragma once

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>
#include "engine/rendering/Vertex.h"
#include "engine/io/ObjLoader.h"
#include <spdlog/spdlog.h>

#include <vector>
#include <filesystem>

namespace engine::core
{
	class ResourceManager
	{
	public:
		// (Just aliases to make notations lighter)
		using path = std::filesystem::path;
		using vec3 = glm::vec3;
		using vec2 = glm::vec2;
		using mat3x3 = glm::mat3x3;

		explicit ResourceManager(std::filesystem::path baseDir);

		// Load a shader from a WGSL file into a new shader module
		static wgpu::ShaderModule loadShaderModule(const path &path, wgpu::Device device);

		// Load an 3D mesh from a standard .obj file into a vertex data buffer
		bool loadGeometryFromObj(const path &path, engine::rendering::Mesh &mesh, bool populateTextureFrame = true);

		// Load an image from a standard image file into a new texture object
		// NB: The texture must be destroyed after use
		static wgpu::Texture loadTexture(const path &path, wgpu::Device device, wgpu::TextureView *pTextureView = nullptr);

	private:
		std::unique_ptr<engine::io::ObjLoader> m_objLoader;
	};
}