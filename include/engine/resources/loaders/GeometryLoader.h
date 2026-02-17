#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/debug/Loggable.h"
#include "engine/math/CoordinateSystem.h"
#include "engine/rendering/Mesh.h"
#include "engine/resources/loaders/LoaderBase.h"

namespace engine::resources::loaders
{

/*
 * @class GeometryLoader
 * @brief Abstract base class for geometry loaders handling 3D mesh data.
 *        Provides coordinate system management and common functionality.
 *
 * Inherits from LoaderBase<T>, where T is the type of GeometryData being loaded.
 * Derived classes must implement the load method to parse specific file formats.
 * @tparam T The type of GeometryData to be loaded (e.g., ObjGeometryData, GltfGeometryData).
 */
template <typename T>
class GeometryLoader : public LoaderBase<T>
{
  public:
	using Loaded = typename LoaderBase<T>::Loaded;

	~GeometryLoader() override = default;

	[[nodiscard]] engine::math::CoordinateSystem::Cartesian getSourceCoordinateSystem() const { return m_srcCoordSys; }

	void setSourceCoordinateSystem(engine::math::CoordinateSystem::Cartesian srcCoordSys) { m_srcCoordSys = srcCoordSys; }

	/**
	 * @brief Loads geometry data from a file using default coordinate systems.
	 * @param file Relative or absolute path to the geometry file.
	 * @return Optional Ptr to the loaded GeometryData, std::nullopt on failure.
	 * @note Uses the loader's default source coordinate system and the engine's default destination coordinate system.
	 */
	[[nodiscard]]
	std::optional<Loaded> load(const std::filesystem::path &file) override
	{
		return load(file, std::nullopt, std::nullopt);
	}

	/**
	 * @brief Loads geometry data from a file, with optional coordinate system overrides.
	 * @param file Relative or absolute path to the geometry file.
	 * @param srcCoordSys Optional: source coordinate system for this load (overrides loaders default if set)
	 * @param dstCoordSys Optional: destination coordinate system (defaults to CoordinateSystem::DEFAULT
	 */
	[[nodiscard]]
	virtual std::optional<Loaded> load(const std::filesystem::path &file, std::optional<engine::math::CoordinateSystem::Cartesian> srcCoordSys, std::optional<engine::math::CoordinateSystem::Cartesian> dstCoordSys) = 0;

  protected:
	/**
	 * @brief Constructs the GeometryLoader with a base path, and optional logger.
	 *        Derived classes must set m_srcCoordSys to their default in their constructor.
	 * @param basePath The base filesystem path to resolve relative files.
	 */
	explicit GeometryLoader(std::filesystem::path basePath) :
		LoaderBase<T>(std::move(basePath))
	{
	}

	engine::math::CoordinateSystem::Cartesian m_srcCoordSys{math::CoordinateSystem::DEFAULT};

	static glm::mat3x3 computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN);
};

} // namespace engine::resources::loaders
