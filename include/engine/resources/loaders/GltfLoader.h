#pragma once

#include "engine/resources/GltfGeometryData.h"
#include "engine/resources/loaders/GeometryLoader.h"

namespace engine::resources::loaders
{
class GltfLoader : public GeometryLoader<engine::resources::GltfGeometryData>
{
  public:
	/**
	 * @brief Constructs an ObjLoader with the given base path and optional logger.
	 * @param basePath The base directory path where OBJ files are located.
	 */
	explicit GltfLoader(std::filesystem::path basePath) :
		GeometryLoader(std::move(basePath))
	{
		// Default source coordinate system for GLTF files
		m_srcCoordSys = engine::math::CoordinateSystem::Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
	}

	/**
	 * @brief Parses a GLTF/GLB file and returns geometry data.
	 * @param file The relative or absolute file path to load the geometry from.
	 * @param srcCoordSys Optional: source coordinate system for this load (overrides loaders default if set)
	 * @param dstCoordSys Optional: destination coordinate system (defaults to CoordinateSystem::DEFAULT)
	 * @return An optional GeometryData object containing the parsed geometry.
	 *         Returns std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<Loaded> load(const std::filesystem::path &file, std::optional<engine::math::CoordinateSystem::Cartesian> srcCoordSys, std::optional<engine::math::CoordinateSystem::Cartesian> dstCoordSys) override;
};

} // namespace engine::resources::loaders