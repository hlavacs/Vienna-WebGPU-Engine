#pragma once

#include "engine/io/tiny_obj_loader.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/Submesh.h"
#include "engine/resources/loaders/GeometryLoader.h"
#include <optional>
#include <string>
#include <vector>

namespace engine::resources::loaders
{
/**
 * @struct ObjGeometryData
 * @brief Holds parsed geometry and material data from an OBJ file.
 */
struct ObjGeometryData
{
    std::string filePath;
    std::string name;
    std::vector<engine::rendering::Vertex> vertices;
    std::vector<uint32_t> indices;

    struct MaterialRange
    {
        int materialId = -1;          // Index in tinyobj::material_t array
        uint32_t indexOffset = 0;    // Start in the global indices array
        uint32_t indexCount = 0;     // Number of indices for this material
    };

    std::vector<MaterialRange> materialRanges;
    std::vector<tinyobj::material_t> materials;
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
	 *        The loader sets its default source coordinate system internally.
	 * @param basePath The base directory path where OBJ files are located.
	 * @param logger Optional shared pointer to a spdlog logger for logging.
	 */
	explicit ObjLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger = nullptr);

	// Getters/Setters for source coordinate system
	engine::math::CoordinateSystem::Cartesian getSourceCoordinateSystem() const { return m_srcCoordSys; }
	void setSourceCoordinateSystem(engine::math::CoordinateSystem::Cartesian srcCoordSys) { m_srcCoordSys = srcCoordSys; }

	/**
	 * @brief Parses an OBJ file and returns geometry and material data.
	 * @param file The relative or absolute file path to load the geometry from.
	 * @param srcCoordSys Optional: source coordinate system for this load (overrides loader default if set)
	 * @param dstCoordSys Optional: destination coordinate system (defaults to CoordinateSystem::DEFAULT)
	 * @return An optional ObjGeometryData object containing the parsed geometry and materials.
	 *         Returns std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<ObjGeometryData> load(const std::filesystem::path &file, std::optional<engine::math::CoordinateSystem::Cartesian> srcCoordSys = std::nullopt, std::optional<engine::math::CoordinateSystem::Cartesian> dstCoordSys = std::nullopt);
};

} // namespace engine::resources::loaders
