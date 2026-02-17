#include "engine/resources/loaders/ObjLoader.h"
#include <filesystem>
#include <unordered_map>

namespace engine::resources::loaders
{
std::optional<ObjGeometryData> ObjLoader::load(
	const std::filesystem::path &file,
	std::optional<engine::math::CoordinateSystem::Cartesian> srcCoordSysOpt,
	std::optional<engine::math::CoordinateSystem::Cartesian> dstCoordSysOpt
)
{
	auto srcCoordSys = srcCoordSysOpt.value_or(m_srcCoordSys);
	auto dstCoordSys = dstCoordSysOpt.value_or(engine::math::CoordinateSystem::DEFAULT);
	std::filesystem::path filePath = resolvePath(file);
	logInfo("Loading OBJ file: '{}'", filePath.string());

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err, warn;

	bool triangulate = true;

	bool success = tinyobj::LoadObj(
		&attrib,
		&shapes,
		&materials,
		&warn,
		&err,
		filePath.string().c_str(),
		filePath.parent_path().string().c_str(),
		triangulate
	);

	if (!warn.empty())
		logWarn(warn);
	if (!err.empty())
		logError(err);

	if (!success)
	{
		logError("Failed to load OBJ: " + filePath.string());
		return std::nullopt;
	}

	ObjGeometryData data;
	data.filePath = filePath.string();
	data.name = !shapes.empty() ? shapes[0].name : file.filename().string();
	data.materials = std::move(materials);
	std::unordered_map<engine::rendering::Vertex, uint32_t> uniqueVertices;

	size_t estimatedIndexCount = 0;
	for (const auto &shape : shapes)
		estimatedIndexCount += shape.mesh.indices.size();

	data.vertices.reserve(estimatedIndexCount / 2);
	data.indices.reserve(estimatedIndexCount);

	int currentMaterial = -1;
	ObjGeometryData::MaterialRange currentRange;

	data.boundingBox.min = glm::vec3(std::numeric_limits<float>::max());
	data.boundingBox.max = glm::vec3(std::numeric_limits<float>::lowest());

	for (const auto &shape : shapes)
	{
		size_t faceOffset = 0;
		for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
		{
			int matId = shape.mesh.material_ids[f];

			if (matId != currentMaterial)
			{
				if (currentRange.indexCount > 0)
					data.materialRanges.push_back(currentRange);

				currentRange.materialId = matId;
				currentRange.indexOffset = static_cast<uint32_t>(data.indices.size());
				currentRange.indexCount = 0;
				currentMaterial = matId;
			}

			for (size_t v = 0; v < shape.mesh.num_face_vertices[f]; ++v)
			{
				const auto &index = shape.mesh.indices[faceOffset + v];
				engine::rendering::Vertex vertex{};

				glm::vec3 pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};
				vertex.position = math::CoordinateSystem::transform(pos, srcCoordSys, dstCoordSys);
				data.boundingBox.expandToFit(vertex.position);
				if (index.normal_index >= 0)
				{
					glm::vec3 nrm = {
						attrib.normals[3 * index.normal_index + 0],
						attrib.normals[3 * index.normal_index + 1],
						attrib.normals[3 * index.normal_index + 2]
					};
					vertex.normal = math::CoordinateSystem::transform(nrm, srcCoordSys, dstCoordSys);
				}

				if (index.texcoord_index >= 0)
				{
					vertex.uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
					};
				}

				if (!attrib.colors.empty() && attrib.colors.size() > 3 * index.vertex_index + 2)
				{
					vertex.color = {
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2]
					};
				}
				else
				{
					vertex.color = {1.f, 1.f, 1.f};
				}

				auto [it, inserted] = uniqueVertices.try_emplace(vertex, static_cast<uint32_t>(data.vertices.size()));
				if (inserted)
					data.vertices.push_back(vertex);

				uint32_t finalIndex = it->second;
				data.indices.push_back(finalIndex);
				currentRange.indexCount++;
			}

			faceOffset += shape.mesh.num_face_vertices[f];
		}
	}

	if (currentRange.indexCount > 0)
		data.materialRanges.push_back(currentRange);

	data.indices.shrink_to_fit();
	data.vertices.shrink_to_fit();
	data.materialRanges.shrink_to_fit();

	return data;
}

} // namespace engine::resources::loaders
