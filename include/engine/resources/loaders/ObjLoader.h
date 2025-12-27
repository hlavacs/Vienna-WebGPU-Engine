#pragma once

#include "engine/io/tiny_obj_loader.h"
#include "engine/math/CoordinateSystem.h"
#include "engine/rendering/Submesh.h"
#include "engine/rendering/Vertex.h"
#include "engine/resources/ObjGeometryData.h"
#include "engine/resources/loaders/GeometryLoader.h"
#include <optional>
#include <string>
#include <vector>

namespace engine::resources::loaders
{

/**
 * @class ObjLoader
 * @brief Loads geometry data from OBJ files (parsing only).
 *
 * Inherits from GeometryLoader and implements parsing of 3D mesh data
 * specifically from the Wavefront OBJ file format. Does not create engine::Mesh or Material.
 */
class ObjLoader : public GeometryLoader<engine::resources::ObjGeometryData>
{
  public:
	/**
	 * @brief Constructs an ObjLoader with the given base path and optional logger.
	 *        The loader sets its default source coordinate system internally.
	 * @param basePath The base directory path where OBJ files are located.
	 */
	explicit ObjLoader(std::filesystem::path basePath) :
		GeometryLoader(std::move(basePath))
	{
		// Default source coordinate system for OBJ files
		m_srcCoordSys = engine::math::CoordinateSystem::Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
	}

	/**
	 * @brief Parses an OBJ file and returns geometry and material data.
	 * @param file The relative or absolute file path to load the geometry from.
	 * @param srcCoordSys Optional: source coordinate system for this load (overrides loaders default if set)
	 * @param dstCoordSys Optional: destination coordinate system (defaults to CoordinateSystem::DEFAULT)
	 * @return An optional ObjGeometryData object containing the parsed geometry and materials.
	 *         Returns std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<Loaded> load(const std::filesystem::path &file, std::optional<engine::math::CoordinateSystem::Cartesian> srcCoordSys, std::optional<engine::math::CoordinateSystem::Cartesian> dstCoordSys) override;
};

} // namespace engine::resources::loaders
