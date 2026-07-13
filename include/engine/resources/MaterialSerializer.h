#pragma once

#include <filesystem>
#include <memory>

namespace engine::rendering
{
struct Material; // NB: Material is a struct - the class-key must match for MSVC name mangling
}

namespace engine::resources
{
class MaterialManager;
class TextureManager;

/**
 * @brief Reads and writes a single Material as a standalone `.mat.json` file -
 * the unit of the project material library.
 *
 * A material file round-trips the shader name, the typed properties (PBR or
 * Unlit) and the texture-slot references. Textures are stored as portable engine
 * path tokens (asset:// / resource://) and resolved through the TextureManager on
 * load, so a material is shared by reference across all scenes in a project.
 */
class MaterialSerializer
{
  public:
	/** @brief Write @p material to @p file (creating parent directories). */
	static bool save(const engine::rendering::Material &material, const std::filesystem::path &file);

	/**
	 * @brief Load a material file, resolve its textures, and register the result
	 * in @p materials so scenes can reference it by name.
	 * @return the created material, or nullptr on failure.
	 */
	static std::shared_ptr<engine::rendering::Material> load(
		const std::filesystem::path &file,
		MaterialManager &materials,
		TextureManager &textures
	);
};

} // namespace engine::resources
