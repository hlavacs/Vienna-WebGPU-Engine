#pragma once

#include "engine/resources/loaders/GeometryLoader.h"
#include "engine/io/tiny_obj_loader.h"
#include <vector>
#include <optional>
#include <string>
#include "engine/rendering/Vertex.h"

namespace engine::resources::loaders
{
	/**
	 * @struct ObjGeometryData
	 * @brief Holds parsed geometry and material data from an OBJ file.
	 */
	struct ObjGeometryData {
		std::vector<engine::rendering::Vertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<tinyobj::material_t> materials;
		std::string filePath;
		std::string name;
	};

	/**
	 * @class ObjLoader
	 * @brief Loads geometry data from OBJ files (parsing only).
	 *
	 * Inherits from GeometryLoader and implements parsing of 3D mesh data
	 * specifically from the Wavefront OBJ file format. Does not create engine::Mesh or Material.
	 */
	class ObjLoader : public GeometryLoader
	{
	public:
		/**
		 * @brief Constructs an ObjLoader with the given base path and optional logger.
		 * @param basePath The base directory path where OBJ files are located.
		 * @param logger Optional shared pointer to a spdlog logger for logging.
		 */
		explicit ObjLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger = nullptr);

		/**
		 * @brief Parses an OBJ file and returns geometry and material data.
		 * @param file The relative or absolute file path to load the geometry from.
		 * @param indexed If true, parses the mesh with indexing (unique vertices + indices).
		 *                If false, parses the mesh as non-indexed (expanded vertices, no indices).
		 *                Default is true.
		 * @return An optional ObjGeometryData object containing the parsed geometry and materials.
		 *         Returns std::nullopt on failure.
		 */
		[[nodiscard]]
		std::optional<ObjGeometryData> load(const std::filesystem::path &file, bool indexed = true);

	protected:
		[[nodiscard]]
		std::pair<std::vector<engine::rendering::Vertex>, std::vector<uint32_t>> buildVerticesIndexed(const std::vector<tinyobj::shape_t> &shapes, const tinyobj::attrib_t &attrib);
		[[nodiscard]]
		std::vector<engine::rendering::Vertex> buildVerticesNonIndexed(const std::vector<tinyobj::shape_t> &shapes, const tinyobj::attrib_t &attrib);
	};

} // namespace engine::resources::loaders
