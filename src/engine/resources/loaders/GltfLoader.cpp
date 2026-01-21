#include "engine/resources/loaders/GltfLoader.h"
#include "engine/rendering/Mesh.h"
#include <tiny_gltf.h>

namespace engine::resources::loaders
{

std::optional<engine::resources::GltfGeometryData> GltfLoader::load(
	const std::filesystem::path &file,
	std::optional<engine::math::CoordinateSystem::Cartesian> srcCoordSysOpt,
	std::optional<engine::math::CoordinateSystem::Cartesian> dstCoordSysOpt
)
{
	auto srcCoordSys = srcCoordSysOpt.value_or(m_srcCoordSys);
	auto dstCoordSys = dstCoordSysOpt.value_or(engine::math::CoordinateSystem::DEFAULT);
	std::filesystem::path filePath = resolvePath(file);
	logInfo("Loading GLTF file: '{}'", filePath.string());

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err, warn;

	bool success = (file.extension() == ".glb")
					   ? loader.LoadBinaryFromFile(&model, &err, &warn, filePath.string())
					   : loader.LoadASCIIFromFile(&model, &err, &warn, filePath.string());

	if (!warn.empty())
		logWarn(warn);
	if (!err.empty())
		logError(err);
	if (!success)
	{
		logError("Failed to load GLTF: '{}'", filePath.string());
		return std::nullopt;
	}
	if (model.meshes.empty())
	{
		logError("GLTF file contains no meshes: '{}'", filePath.string());
		return std::nullopt;
	}

	const tinygltf::Mesh &gltfMesh = model.meshes[0];
	engine::resources::GltfGeometryData data;
	data.filePath = filePath.string();
	data.name = gltfMesh.name.empty() ? file.stem().string() : gltfMesh.name;

	// Setup MaterialContext
	data.materialContext = std::make_shared<engine::resources::GltfMaterialContext>(
		engine::resources::GltfMaterialContext{
			model.materials,
			model.textures,
			model.images,
			model.samplers
		}
	);

	uint32_t vertexBase = 0;
	uint32_t indexBase = 0;

	for (const auto &primitive : gltfMesh.primitives)
	{
		engine::resources::GltfGeometryData::PrimitiveRange range{};
		range.materialId = primitive.material;
		range.vertexOffset = vertexBase;
		range.indexOffset = indexBase;

		// Vertices
		const auto &posAccessor = model.accessors.at(primitive.attributes.at("POSITION"));
		const auto &posView = model.bufferViews[posAccessor.bufferView];
		const auto &posBuffer = model.buffers[posView.buffer];

		range.vertexCount = static_cast<uint32_t>(posAccessor.count);
		data.vertices.reserve(data.vertices.size() + range.vertexCount);

		for (size_t i = 0; i < posAccessor.count; ++i)
		{
			engine::rendering::Vertex vertex{};
			size_t offset = posView.byteOffset + posAccessor.byteOffset + i * posAccessor.ByteStride(posView);
			const float *posPtr = reinterpret_cast<const float *>(&posBuffer.data[offset]);
			glm::vec3 pos{posPtr[0], posPtr[1], posPtr[2]};
			vertex.position = math::CoordinateSystem::transform(pos, srcCoordSys, dstCoordSys);

			// Optional normals
			if (primitive.attributes.count("NORMAL"))
			{
				const auto &nrmAccessor = model.accessors.at(primitive.attributes.at("NORMAL"));
				const auto &nrmView = model.bufferViews[nrmAccessor.bufferView];
				const auto &nrmBuffer = model.buffers[nrmView.buffer];
				size_t nrmOffset = nrmView.byteOffset + nrmAccessor.byteOffset + i * nrmAccessor.ByteStride(nrmView);
				const float *nrmPtr = reinterpret_cast<const float *>(&nrmBuffer.data[nrmOffset]);
				glm::vec3 nrm{nrmPtr[0], nrmPtr[1], nrmPtr[2]};
				vertex.normal = math::CoordinateSystem::transform(nrm, srcCoordSys, dstCoordSys);
			}

			// Optional UV
			if (primitive.attributes.count("TEXCOORD_0"))
			{
				const auto &uvAccessor = model.accessors.at(primitive.attributes.at("TEXCOORD_0"));
				const auto &uvView = model.bufferViews[uvAccessor.bufferView];
				const auto &uvBuffer = model.buffers[uvView.buffer];
				size_t uvOffset = uvView.byteOffset + uvAccessor.byteOffset + i * uvAccessor.ByteStride(uvView);
				const float *uvPtr = reinterpret_cast<const float *>(&uvBuffer.data[uvOffset]);
				vertex.uv = {uvPtr[0], 1.0f - uvPtr[1]};
			}

			vertex.color = {1.f, 1.f, 1.f};
			data.vertices.push_back(vertex);
		}

		vertexBase += range.vertexCount;

		// Indices
		if (primitive.indices >= 0)
		{
			const auto &idxAccessor = model.accessors[primitive.indices];
			const auto &idxView = model.bufferViews[idxAccessor.bufferView];
			const auto &idxBuffer = model.buffers[idxView.buffer];

			range.indexCount = static_cast<uint32_t>(idxAccessor.count);
			data.indices.reserve(data.indices.size() + idxAccessor.count);

			for (size_t i = 0; i < idxAccessor.count; ++i)
			{
				size_t idxOffset = idxView.byteOffset + idxAccessor.byteOffset + i * idxAccessor.ByteStride(idxView);
				uint32_t idx = 0;
				switch (idxAccessor.componentType)
				{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					idx = *reinterpret_cast<const uint16_t *>(&idxBuffer.data[idxOffset]);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					idx = *reinterpret_cast<const uint32_t *>(&idxBuffer.data[idxOffset]);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					idx = *reinterpret_cast<const uint8_t *>(&idxBuffer.data[idxOffset]);
					break;
				}
				data.indices.push_back(idx + range.vertexOffset);
			}
		}
		else
		{
			range.indexCount = range.vertexCount;
			for (uint32_t i = 0; i < range.vertexCount; ++i)
				data.indices.push_back(range.vertexOffset + i);
		}

		indexBase += range.indexCount;
		data.primitives.push_back(range);
	}

	// Bounding box
	data.boundingBox.min = glm::vec3(std::numeric_limits<float>::max());
	data.boundingBox.max = glm::vec3(std::numeric_limits<float>::lowest());
	for (const auto &v : data.vertices)
		data.boundingBox.expandToFit(v.position);

	return data;
}

} // namespace engine::resources::loaders
