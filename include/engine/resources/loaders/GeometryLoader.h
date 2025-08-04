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

namespace engine::resources::loaders
{
/**
 * @class GeometryLoader
 * @brief Abstract base class for geometry loaders that read mesh data from files.
 *
 * Provides a common interface for loading 3D geometry meshes from various file formats.
 * Supports loading meshes either with indexed vertices or as non-indexed (expanded) vertex arrays.
 *
 * Derived classes such as ObjLoader or GltfLoader implement format-specific parsing logic and set their own default source coordinate system.
 * The source coordinate system can be queried or overridden via getter/setter.
 *
 * @note The class also manages base path for asset loading and supports logging through an injected logger.
 */
class GeometryLoader : public engine::debug::Loggable
{
  public:
	virtual ~GeometryLoader() = default;

	// Getters
	const std::filesystem::path &getBasePath() const { return m_basePath; }
	const std::shared_ptr<spdlog::logger> &getLogger() const { return m_logger; }
	engine::math::CoordinateSystem::Cartesian getSourceCoordinateSystem() const { return m_srcCoordSys; }

	// Setters
	void setBasePath(const std::filesystem::path &basePath) { m_basePath = basePath; }
	void setLogger(const std::shared_ptr<spdlog::logger> &logger) { m_logger = logger; }
	void setSourceCoordinateSystem(engine::math::CoordinateSystem::Cartesian srcCoordSys) { m_srcCoordSys = srcCoordSys; }

  protected:
	/**
	 * @brief Constructs the GeometryLoader with a base path, and optional logger.
	 *        Derived classes must set m_srcCoordSys to their default in their constructor.
	 * @param basePath The base filesystem path to resolve relative files.
	 * @param logger Optional shared pointer to a logger instance.
	 */
	explicit GeometryLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger = nullptr);

	std::filesystem::path m_basePath;
	engine::math::CoordinateSystem::Cartesian m_srcCoordSys;

	static glm::mat3x3 computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN);
};

} // namespace engine::resources::loaders
