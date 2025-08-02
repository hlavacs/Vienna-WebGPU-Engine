#include "engine/resources/loaders/GltfLoader.h"
#include "engine/rendering/Mesh.h"
#include <tiny_gltf.h>

namespace engine::resources::loaders
{
	GltfLoader::GltfLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger)
		: GeometryLoader(std::move(basePath), std::move(logger)) {}

	std::optional<engine::rendering::Mesh> GltfLoader::load(const std::filesystem::path &file, bool indexed)
	{
		std::string filePath = (m_basePath / file).string();
		logInfo("Loading GLTF file: " + filePath);

		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err, warn;

		bool success = false;
		if (file.extension() == ".glb")
			success = loader.LoadBinaryFromFile(&model, &err, &warn, filePath);
		else
			success = loader.LoadASCIIFromFile(&model, &err, &warn, filePath);

		if (!warn.empty())
			logWarn(warn);
		if (!err.empty())
			logError(err);

		if (!success)
		{
			logError("Failed to load GLTF: " + filePath);
			return std::nullopt;
		}

		if (model.meshes.empty())
		{
			logError("GLTF file contains no meshes: " + filePath);
			return std::nullopt;
		}

		const tinygltf::Mesh &gltfMesh = model.meshes[0];
		std::vector<engine::rendering::Vertex> vertices;
		std::vector<uint32_t> indices;

		// Assume all primitives are triangles
		bool triangulated = true;

		for (const auto &primitive : gltfMesh.primitives)
		{
			if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
			{
				logWarn("GLTF primitive is not triangles, skipping");
				continue;
			}

			// Access vertex attributes
			const auto &posAccessorIt = primitive.attributes.find("POSITION");
			if (posAccessorIt == primitive.attributes.end())
			{
				logError("Missing POSITION attribute");
				continue;
			}

			const tinygltf::Accessor &posAccessor = model.accessors[posAccessorIt->second];
			const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
			const tinygltf::Buffer &posBuffer = model.buffers[posView.buffer];
			const float *posData = reinterpret_cast<const float *>(&posBuffer.data[posView.byteOffset + posAccessor.byteOffset]);

			// Optional normal
			const float *normalData = nullptr;
			if (auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end())
			{
				const auto &accessor = model.accessors[it->second];
				const auto &view = model.bufferViews[accessor.bufferView];
				const auto &buffer = model.buffers[view.buffer];
				normalData = reinterpret_cast<const float *>(&buffer.data[view.byteOffset + accessor.byteOffset]);
			}

			// Optional texcoord
			const float *uvData = nullptr;
			if (auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end())
			{
				const auto &accessor = model.accessors[it->second];
				const auto &view = model.bufferViews[accessor.bufferView];
				const auto &buffer = model.buffers[view.buffer];
				uvData = reinterpret_cast<const float *>(&buffer.data[view.byteOffset + accessor.byteOffset]);
			}

			const size_t vertexCount = posAccessor.count;
			for (size_t i = 0; i < vertexCount; ++i)
			{
				engine::rendering::Vertex v{};
				v.position = {posData[i * 3 + 0], posData[i * 3 + 1], posData[i * 3 + 2]};

				if (normalData)
					v.normal = {normalData[i * 3 + 0], normalData[i * 3 + 1], normalData[i * 3 + 2]};
				if (uvData)
					v.uv = {uvData[i * 2 + 0], uvData[i * 2 + 1]};

				vertices.push_back(v);
			}

			if (false && indexed)
			{
				const tinygltf::Accessor &indexAccessor = model.accessors[primitive.indices];
				const tinygltf::BufferView &indexView = model.bufferViews[indexAccessor.bufferView];
				const tinygltf::Buffer &indexBuffer = model.buffers[indexView.buffer];
				const uint8_t *data = &indexBuffer.data[indexView.byteOffset + indexAccessor.byteOffset];

				for (size_t i = 0; i < indexAccessor.count; ++i)
				{
					uint32_t idx = 0;
					switch (indexAccessor.componentType)
					{
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
						idx = static_cast<uint32_t>(data[i]);
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
						idx = static_cast<const uint16_t *>(reinterpret_cast<const void *>(data))[i];
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
						idx = reinterpret_cast<const uint32_t *>(data)[i];
						break;
					default:
						logError("Unsupported index type");
						return std::nullopt;
					}
					indices.push_back(idx);
				}
			}
		}

		if (vertices.empty())
		{
			logError("No vertices found in GLTF mesh");
			return std::nullopt;
		}

		if (indexed && !indices.empty())
		{
			logInfo("Loaded indexed GLTF with " + std::to_string(vertices.size()) + " unique vertices and " + std::to_string(indices.size()) + " indices");
			return std::make_optional<engine::rendering::Mesh>(std::move(vertices), std::move(indices), triangulated);
		}
		else
		{
			logInfo("Loaded non-indexed GLTF with " + std::to_string(vertices.size()) + " vertices");
			return std::make_optional<engine::rendering::Mesh>(std::move(vertices), triangulated);
		}
	}

} // namespace engine::resources::loaders
