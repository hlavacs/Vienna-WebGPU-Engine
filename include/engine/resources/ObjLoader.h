#pragma once

#include "engine/resources/GeometryLoader.h"
#include "engine/io/tiny_obj_loader.h"

namespace engine::resources
{
	/**
	 * @class ObjLoader
	 * @brief Loads geometry data from OBJ files.
	 *
	 * Inherits from GeometryLoader and implements loading of 3D mesh data
	 * specifically from the Wavefront OBJ file format. Supports both indexed
	 * and non-indexed mesh loading.
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
		 * @brief Loads a mesh from the given file path.
		 * @param file The relative or absolute file path to load the geometry from.
		 * @param indexed If true, loads the mesh with indexing (unique vertices + indices).
		 *                If false, loads the mesh as non-indexed (expanded vertices, no indices).
		 *                Default is true.
		 * @return An optional Mesh object containing the loaded geometry data.
		 *         Returns std::nullopt on failure.
		 */
		[[nodiscard]]
		std::optional<engine::rendering::Mesh> load(const std::filesystem::path &file, bool indexed = true) override;

	protected:
		[[nodiscard]]
		std::pair<std::vector<engine::rendering::Vertex>, std::vector<uint32_t>> buildVerticesIndexed(const std::vector<tinyobj::shape_t>& shapes, const tinyobj::attrib_t &attrib);
		[[nodiscard]]
		std::vector<engine::rendering::Vertex> buildVerticesNonIndexed(const std::vector<tinyobj::shape_t>& shapes, const tinyobj::attrib_t &attrib);
	};

} // namespace engine::resources
