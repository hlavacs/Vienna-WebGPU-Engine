#include "engine/resources/loaders/ObjLoader.h"
#include <unordered_map>
#include <filesystem>

namespace engine::resources::loaders
{
	ObjLoader::ObjLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger)
		: GeometryLoader(std::move(basePath), std::move(logger)) {}

	std::optional<engine::rendering::Mesh> ObjLoader::load(const std::filesystem::path &file, bool indexed)
	{
		std::string filePath = (m_basePath / file).string();
		logInfo("Loading OBJ file: " + filePath);

		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string err, warn;

		bool triangulate = true;
		bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filePath.c_str(), m_basePath.string().c_str(), triangulate);

		if (!warn.empty())
			logWarn(warn);
		if (!err.empty())
			logError(err);

		if (!success)
		{
			logError("Failed to load OBJ: " + filePath);
			return std::nullopt;
		}
		if (indexed)
		{
			auto [vertices, indices] = buildVerticesIndexed(shapes, attrib);
			logInfo("Loaded indexed OBJ with " + std::to_string(vertices.size()) + " unique vertices and " + std::to_string(indices.size()) + " indices");

			return engine::rendering::Mesh(std::move(vertices), std::move(indices), triangulate);
		}
		else
		{
			auto vertices = buildVerticesNonIndexed(shapes, attrib);
			logInfo("Loaded non-indexed OBJ with " + std::to_string(vertices.size()) + " vertices");

			return engine::rendering::Mesh(std::move(vertices), triangulate);
		}
	}

	std::vector<engine::rendering::Vertex> ObjLoader::buildVerticesNonIndexed(const std::vector<tinyobj::shape_t> &shapes, const tinyobj::attrib_t &attrib)
	{
		std::vector<engine::rendering::Vertex> vertices;

		for (const auto &shape : shapes)
		{
			size_t offset = vertices.size();
			size_t indexCount = shape.mesh.indices.size();
			vertices.resize(offset + indexCount);

			for (size_t i = 0; i < indexCount; ++i)
			{
				const auto &index = shape.mesh.indices[i];
				auto &vertex = vertices[offset + i];

				vertex.position = {
					attrib.vertices[3 * index.vertex_index + 0],
					-attrib.vertices[3 * index.vertex_index + 2],
					attrib.vertices[3 * index.vertex_index + 1]};
				if (!attrib.colors.empty() && attrib.colors.size() > 3 * index.vertex_index + 2)
				{
					vertex.color = {
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2],
						1.0f};
				}
				else
				{
					vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
				}
				if (index.normal_index >= 0)
				{
					vertex.normal = {
						attrib.normals[3 * index.normal_index + 0],
						-attrib.normals[3 * index.normal_index + 2],
						attrib.normals[3 * index.normal_index + 1]};
				}
				if (index.texcoord_index >= 0)
				{
					vertex.uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						1 - attrib.texcoords[2 * index.texcoord_index + 1]};
				}
			}
		}

		return vertices;
	}

	std::pair<std::vector<engine::rendering::Vertex>, std::vector<uint32_t>> ObjLoader::buildVerticesIndexed(
		const std::vector<tinyobj::shape_t> &shapes,
		const tinyobj::attrib_t &attrib)
	{
		std::vector<engine::rendering::Vertex> vertices;
		std::vector<uint32_t> indices;
		std::unordered_map<engine::rendering::Vertex, uint32_t> uniqueVertices;

		size_t estimatedIndexCount = 0;
		for (const auto &shape : shapes)
			estimatedIndexCount += shape.mesh.indices.size();

		vertices.reserve(estimatedIndexCount / 2); // Rough guess
		indices.reserve(estimatedIndexCount);

		for (const auto &shape : shapes)
		{
			for (const auto &index : shape.mesh.indices)
			{
				engine::rendering::Vertex vertex{};

				vertex.position = {
					attrib.vertices[3 * index.vertex_index + 0],
					-attrib.vertices[3 * index.vertex_index + 2],
					attrib.vertices[3 * index.vertex_index + 1]};
				if (!attrib.colors.empty() && attrib.colors.size() > 3 * index.vertex_index + 2)
				{
					vertex.color = {
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2],
						1.0f};
				}
				else
				{
					vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
				}
				if (index.normal_index >= 0)
				{
					vertex.normal = {
						attrib.normals[3 * index.normal_index + 0],
						-attrib.normals[3 * index.normal_index + 2],
						attrib.normals[3 * index.normal_index + 1]};
				}
				if (index.texcoord_index >= 0)
				{
					vertex.uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						1 - attrib.texcoords[2 * index.texcoord_index + 1]}; // Flip Y
				}

				auto [it, inserted] = uniqueVertices.try_emplace(vertex, static_cast<uint32_t>(vertices.size()));
				if (inserted)
				{
					vertices.push_back(vertex);
				}
				indices.push_back(it->second);
			}
		}

		vertices.shrink_to_fit();
		return {std::move(vertices), std::move(indices)};
	}

} // namespace engine::resources::loaders
