#include "engine/resources/loaders/GltfLoader.h"
#include "engine/rendering/Mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

namespace engine::resources::loaders
{

// ── Node transform helpers ───────────────────────────────────────────────────

static glm::mat4 getNodeMatrix(const tinygltf::Node &node)
{
	if (!node.matrix.empty())
	{
		// glTF matrix is column-major, same as glm
		return glm::mat4(
			node.matrix[0],
			node.matrix[1],
			node.matrix[2],
			node.matrix[3],
			node.matrix[4],
			node.matrix[5],
			node.matrix[6],
			node.matrix[7],
			node.matrix[8],
			node.matrix[9],
			node.matrix[10],
			node.matrix[11],
			node.matrix[12],
			node.matrix[13],
			node.matrix[14],
			node.matrix[15]
		);
	}

	glm::mat4 result = glm::mat4(1.0f);

	if (!node.translation.empty())
		result = glm::translate(result, glm::vec3(float(node.translation[0]), float(node.translation[1]), float(node.translation[2])));

	if (!node.rotation.empty())
		// glTF quaternion is xyzw, glm expects wxyz
		result = result * glm::mat4_cast(glm::quat(float(node.rotation[3]), float(node.rotation[0]), float(node.rotation[1]), float(node.rotation[2])));

	if (!node.scale.empty())
		result = glm::scale(result, glm::vec3(float(node.scale[0]), float(node.scale[1]), float(node.scale[2])));

	return result;
}

// ── Primitive processor ──────────────────────────────────────────────────────

static void processPrimitive(
	const tinygltf::Model &model,
	const tinygltf::Primitive &primitive,
	const glm::mat4 &worldTransform,
	engine::resources::GltfGeometryData &data,
	const engine::math::CoordinateSystem::Cartesian srcCoordSys,
	const engine::math::CoordinateSystem::Cartesian dstCoordSys
)
{
	engine::resources::GltfGeometryData::PrimitiveRange range{};
	range.materialId = primitive.material;
	range.vertexOffset = static_cast<uint32_t>(data.vertices.size());
	range.indexOffset = static_cast<uint32_t>(data.indices.size());

	// ── Positions (required) ─────────────────────────────────────────────────
	auto posIt = primitive.attributes.find("POSITION");
	if (posIt == primitive.attributes.end())
		return;

	const auto &posAccessor = model.accessors[posIt->second];
	const auto &posView = model.bufferViews[posAccessor.bufferView];
	const auto &posBuffer = model.buffers[posView.buffer];
	const size_t posStride = posAccessor.ByteStride(posView);

	range.vertexCount = static_cast<uint32_t>(posAccessor.count);
	data.vertices.resize(data.vertices.size() + range.vertexCount);

	// Normal matrix — inverse transpose handles non-uniform scale correctly
	const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(worldTransform)));

	// ── Optional attributes — resolve accessors once outside vertex loop ─────

	// Normals
	const tinygltf::Accessor *nrmAccessor = nullptr;
	const tinygltf::BufferView *nrmView = nullptr;
	const tinygltf::Buffer *nrmBuffer = nullptr;
	size_t nrmStride = 0;
	if (auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end())
	{
		nrmAccessor = &model.accessors[it->second];
		nrmView = &model.bufferViews[nrmAccessor->bufferView];
		nrmBuffer = &model.buffers[nrmView->buffer];
		nrmStride = nrmAccessor->ByteStride(*nrmView);
	}

	// UVs
	const tinygltf::Accessor *uvAccessor = nullptr;
	const tinygltf::BufferView *uvView = nullptr;
	const tinygltf::Buffer *uvBuffer = nullptr;
	size_t uvStride = 0;
	if (auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end())
	{
		uvAccessor = &model.accessors[it->second];
		uvView = &model.bufferViews[uvAccessor->bufferView];
		uvBuffer = &model.buffers[uvView->buffer];
		uvStride = uvAccessor->ByteStride(*uvView);
	}

	// Tangents
	const tinygltf::Accessor *tanAccessor = nullptr;
	const tinygltf::BufferView *tanView = nullptr;
	const tinygltf::Buffer *tanBuffer = nullptr;
	size_t tanStride = 0;
	if (auto it = primitive.attributes.find("TANGENT"); it != primitive.attributes.end())
	{
		tanAccessor = &model.accessors[it->second];
		tanView = &model.bufferViews[tanAccessor->bufferView];
		tanBuffer = &model.buffers[tanView->buffer];
		tanStride = tanAccessor->ByteStride(*tanView);
	}

	// ── Vertex loop ──────────────────────────────────────────────────────────
	for (size_t i = 0; i < posAccessor.count; ++i)
	{
		engine::rendering::Vertex vertex{};

		// Position — apply coord system transform then node world transform
		size_t posOffset = posView.byteOffset + posAccessor.byteOffset + i * posStride;
		const auto *posPtr = reinterpret_cast<const float *>(&posBuffer.data[posOffset]);
		glm::vec3 pos{posPtr[0], posPtr[1], posPtr[2]};
		pos = engine::math::CoordinateSystem::transform(pos, srcCoordSys, dstCoordSys);
		vertex.position = glm::vec3(worldTransform * glm::vec4(pos, 1.0f));

		// Normal — use normal matrix to correctly handle non-uniform scale
		if (nrmAccessor)
		{
			size_t nrmOffset = nrmView->byteOffset + nrmAccessor->byteOffset + i * nrmStride;
			const auto *nrmPtr = reinterpret_cast<const float *>(&nrmBuffer->data[nrmOffset]);
			glm::vec3 nrm{nrmPtr[0], nrmPtr[1], nrmPtr[2]};
			nrm = engine::math::CoordinateSystem::transform(nrm, srcCoordSys, dstCoordSys);
			vertex.normal = glm::normalize(normalMatrix * nrm);
		}

		// UV
		if (uvAccessor)
		{
			size_t uvOffset = uvView->byteOffset + uvAccessor->byteOffset + i * uvStride;
			const auto *uvPtr = reinterpret_cast<const float *>(&uvBuffer->data[uvOffset]);
			vertex.uv = {uvPtr[0], uvPtr[1]};
		}

		// Tangent — vec4 in glTF: xyz = tangent direction, w = bitangent handedness
		if (tanAccessor)
		{
			size_t tanOffset = tanView->byteOffset + tanAccessor->byteOffset + i * tanStride;
			const auto *tanPtr = reinterpret_cast<const float *>(&tanBuffer->data[tanOffset]);
			glm::vec4 tan{tanPtr[0], tanPtr[1], tanPtr[2], tanPtr[3]};
			tan = engine::math::CoordinateSystem::transform(tan, srcCoordSys, dstCoordSys);
			glm::vec3 tanXyz = glm::normalize(normalMatrix * glm::vec3(tan));
			vertex.tangent = glm::vec4(tanXyz, tan.w);
		}

		vertex.color = {1.f, 1.f, 1.f};
		data.vertices[range.vertexOffset + i] = vertex;
	}

	// ── Index buffer ─────────────────────────────────────────────────────────
	if (primitive.indices >= 0)
	{
		const auto &idxAccessor = model.accessors[primitive.indices];
		const auto &idxView = model.bufferViews[idxAccessor.bufferView];
		const auto &idxBuffer = model.buffers[idxView.buffer];
		const size_t idxStride = idxAccessor.ByteStride(idxView);

		range.indexCount = static_cast<uint32_t>(idxAccessor.count);
		data.indices.reserve(data.indices.size() + idxAccessor.count);

		for (size_t i = 0; i < idxAccessor.count; ++i)
		{
			size_t idxOffset = idxView.byteOffset + idxAccessor.byteOffset + i * idxStride;
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
			default:
				break;
			}
			// Offset by vertexOffset so indices point into the global vertex buffer
			data.indices.push_back(range.vertexOffset + idx);
		}
	}
	else
	{
		// No index buffer — generate sequential indices
		range.indexCount = range.vertexCount;
		data.indices.reserve(data.indices.size() + range.vertexCount);
		for (uint32_t i = 0; i < range.vertexCount; ++i)
			data.indices.push_back(range.vertexOffset + i);
	}

	data.primitives.push_back(range);
}

// ── Node traversal ───────────────────────────────────────────────────────────

static void processNode(
	const tinygltf::Model &model,
	int nodeIndex,
	const glm::mat4 &parentTransform,
	engine::resources::GltfGeometryData &data,
	const engine::math::CoordinateSystem::Cartesian srcCoordSys,
	const engine::math::CoordinateSystem::Cartesian dstCoordSys
)
{
	const auto &node = model.nodes[nodeIndex];
	const glm::mat4 localTransform = getNodeMatrix(node);
	const glm::mat4 worldTransform = parentTransform * localTransform;

	if (node.mesh >= 0)
	{
		const auto &gltfMesh = model.meshes[node.mesh];
		for (const auto &primitive : gltfMesh.primitives)
			processPrimitive(model, primitive, worldTransform, data, srcCoordSys, dstCoordSys);
	}

	for (int childIndex : node.children)
		processNode(model, childIndex, worldTransform, data, srcCoordSys, dstCoordSys);
}

// ── Main load function ───────────────────────────────────────────────────────

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

	engine::resources::GltfGeometryData data;
	data.filePath = filePath.string();
	data.name = file.stem().string();

	// Setup MaterialContext — move arrays out of model before scene traversal
	data.materialContext = std::make_shared<engine::resources::GltfMaterialContext>(
		std::move(model.materials),
		std::move(model.textures),
		std::move(model.images),
		std::move(model.samplers)
	);

	// Traverse the default scene graph — respects node hierarchy and transforms
	const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
	if (sceneIndex < static_cast<int>(model.scenes.size()))
	{
		const auto &scene = model.scenes[sceneIndex];
		for (int nodeIndex : scene.nodes)
			processNode(model, nodeIndex, glm::mat4(1.0f), data, srcCoordSys, dstCoordSys);
	}
	else
	{
		// Fallback — no scene defined, iterate meshes directly without transforms
		logWarn("No valid scene found in glTF file, falling back to raw mesh iteration.");
		for (const auto &gltfMesh : model.meshes)
			for (const auto &primitive : gltfMesh.primitives)
				processPrimitive(model, primitive, glm::mat4(1.0f), data, srcCoordSys, dstCoordSys);
	}

	if (data.vertices.empty())
	{
		logError("No vertices loaded from GLTF file: '{}'", filePath.string());
		return std::nullopt;
	}

	// ── Bounding box ─────────────────────────────────────────────────────────
	data.boundingBox.min = glm::vec3(std::numeric_limits<float>::max());
	data.boundingBox.max = glm::vec3(std::numeric_limits<float>::lowest());
	for (const auto &v : data.vertices)
		data.boundingBox.expandToFit(v.position);

	logInfo("Loaded '{}': {} vertices, {} indices, {} primitives", data.name, data.vertices.size(), data.indices.size(), data.primitives.size());

	return data;
}

} // namespace engine::resources::loaders