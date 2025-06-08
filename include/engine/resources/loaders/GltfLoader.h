#include "engine/resources/loaders/GeometryLoader.h"

namespace engine::resources::loaders
{
	class GltfLoader : public GeometryLoader
	{
	public:
		/**
		 * @brief Constructs an ObjLoader with the given base path and optional logger.
		 * @param basePath The base directory path where OBJ files are located.
		 * @param logger Optional shared pointer to a spdlog logger for logging.
		 */
		explicit GltfLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger = nullptr);

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
		std::optional<engine::rendering::Mesh> load(const std::filesystem::path &file, bool indexed = true);

	protected:
	};

} // namespace engine::resources::loaders