#pragma once

#include <filesystem>
#include <memory>

namespace engine::scene
{
class Scene;
namespace nodes
{
class Node;
}

/**
 * @class SceneSerializer
 * @brief Saves and loads a Scene to/from a JSON file.
 *
 * Asset references support two save modes:
 *  - save() stores portable engine path tokens - "resource://..." for engine
 *    resources and "asset://..." for project assets - resolved on load via
 *    PathProvider. The scene loads on any machine that has the same project
 *    assets and the engine.
 *  - exportSelfContained() additionally copies every referenced project asset
 *    into an "assets/" folder next to the .json and rewrites those references
 *    to scene-relative paths, producing a folder that can be moved as-is:
 *
 *        MyScene/
 *          scene.json   - node tree, transforms, light / camera / model data
 *          assets/      - copies of the models / textures it references
 *
 *    Engine resources stay "resource://" tokens in both modes; the engine
 *    provides them on every machine, so they are never copied.
 *
 * Scripts are not yet a runtime concept in the engine. Each node carries a
 * "scripts" array that round-trips untouched, reserving the slot for when a
 * behaviour system lands.
 */
class SceneSerializer
{
  public:
	/**
	 * @brief Write @p scene to @p sceneFilePath (a .json inside the scene folder).
	 * @param scene The scene to serialize.
	 * @param sceneFilePath Destination .json path. Its parent directory is the
	 *        scene folder that asset paths are made relative to.
	 * @return True on success; false on any I/O or encoding error (logged).
	 */
	static bool save(const Scene &scene, const std::filesystem::path &sceneFilePath);

	/**
	 * @brief Like save(), but copies every referenced project asset into
	 * `<sceneFolder>/assets/` and stores scene-relative paths, so the folder is
	 * self-contained and can be moved between machines as-is. Engine resources
	 * stay "resource://" tokens.
	 * @param scene The scene to serialize.
	 * @param sceneFilePath Destination .json path; its parent is the scene folder.
	 * @return True on success; false on any I/O error (logged).
	 */
	static bool exportSelfContained(const Scene &scene, const std::filesystem::path &sceneFilePath);

	/**
	 * @brief Build a Scene from @p sceneFilePath.
	 *
	 * Model nodes are created with their (lazy) source path so the model loads
	 * when the scene is activated by the SceneManager. The returned scene is not
	 * yet registered with any SceneManager - the caller owns that step.
	 * @param sceneFilePath The .json to read.
	 * @return The reconstructed scene, or nullptr on failure (logged).
	 */
	static std::shared_ptr<Scene> load(const std::filesystem::path &sceneFilePath);

	/**
	 * @brief Deep-copy a node and its subtree, reusing the per-type
	 * (de)serialization so every property is carried over. The clone gets fresh
	 * node ids and is detached - the caller adds it to a parent. Asset references
	 * resolve as for a default (token) save.
	 * @return The cloned node, or nullptr if it could not be cloned.
	 */
	static std::shared_ptr<nodes::Node> cloneNode(const nodes::Node &node);
};

} // namespace engine::scene
