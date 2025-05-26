#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <glm/glm.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/debug/Loggable.h"

namespace engine::resources
{
	/**
	 * @class GeometryLoader
	 * @brief Abstract base class for geometry loaders that read mesh data from files.
	 *
	 * Provides a common interface for loading 3D geometry meshes from various file formats.
	 * Supports loading meshes either with indexed vertices or as non-indexed (expanded) vertex arrays.
	 *
	 * Derived classes such as ObjLoader or GltfLoader implement format-specific parsing logic.
	 *
	 * @note The class also manages base path for asset loading and supports logging through an injected logger.
	 */
	class GeometryLoader : public engine::debug::Loggable
	{
	public:
		virtual ~GeometryLoader() = default;

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
		virtual std::optional<engine::rendering::Mesh> load(const std::filesystem::path &file, bool indexed = true) = 0;

		// Getters
		const std::filesystem::path &getBasePath() const { return m_basePath; }
		const std::shared_ptr<spdlog::logger> &getLogger() const { return m_logger; }

		// Setters
		void setBasePath(const std::filesystem::path &basePath) { m_basePath = basePath; }
		void setLogger(const std::shared_ptr<spdlog::logger> &logger) { m_logger = logger; }

	protected:
		/**
		 * @brief Constructs the GeometryLoader with a base path and optional logger.
		 * @param basePath The base filesystem path to resolve relative files.
		 * @param logger Optional shared pointer to a logger instance.
		 */
		explicit GeometryLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger = nullptr);

		std::filesystem::path m_basePath;

		
		static glm::mat3x3 computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN);
	};

} // namespace engine::resources
