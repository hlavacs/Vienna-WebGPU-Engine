#pragma once

#include "engine/scene/Scene.h"
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace engine
{
class EngineContext;
}

namespace engine::scene
{

/**
 * @brief Manages multiple scenes and handles scene transitions
 *
 * The SceneManager is responsible for:
 * - Creating and registering scenes
 * - Switching between scenes
 * - Managing the active scene lifecycle
 *
 * ToDo: Lazy Loading of scenes from disk and async transitions with loading screens
 */
class SceneManager
{
  public:
	SceneManager() = default;
	~SceneManager() = default;

	/**
	 * @brief Create a new scene with a given name
	 * @param sceneName The name/ID for the scene
	 * @return Shared pointer to the newly created scene
	 */
	std::shared_ptr<Scene> createScene(const std::string &sceneName);

	/**
	 * @brief Register an existing scene
	 * @param sceneName The name/ID for the scene
	 * @param scene The scene to register
	 */
	void registerScene(const std::string &sceneName, std::shared_ptr<Scene> scene);

	/**
	 * @brief Load a scene by name and make it the active scene
	 * @param sceneName The name of the scene to load
	 * @return true if scene was found and loaded, false otherwise
	 */
	bool loadScene(const std::string &sceneName);

	/**
	 * @brief Load a scene asynchronously with initialization
	 * Calls initialize() on all nodes in the scene tree before activating
	 * @param sceneName The name of the scene to load
	 * @return Future that resolves to true if scene was loaded successfully, false otherwise
	 */
	std::future<bool> loadSceneAsync(const std::string &sceneName);

	/**
	 * @brief Check if a scene is currently loading
	 * @return true if a scene is being loaded asynchronously
	 */
	bool isLoading() const { return m_isLoading; }

	/**
	 * @brief Get the name of the scene currently being loaded
	 * @return Scene name or empty string if not loading
	 */
	const std::string &getLoadingSceneName() const { return m_loadingSceneName; }

	/**
	 * @brief Get the currently active scene
	 * @return Pointer to active scene, or nullptr if none
	 */
	[[nodiscard]] std::shared_ptr<Scene> getActiveScene() const;

	/**
	 * @brief Get the name of the currently active scene
	 * @return Scene name or empty string if none
	 */
	[[nodiscard]] const std::string &getActiveSceneName() const { return m_activeSceneName; }

	/**
	 * @brief Get a scene by name (without making it active)
	 * @param sceneName The name of the scene
	 * @return Pointer to scene, or nullptr if not found
	 */
	[[nodiscard]] std::shared_ptr<Scene> getScene(const std::string &sceneName) const;

	/**
	 * @brief Remove a scene from the manager
	 * @param sceneName The name of the scene to remove
	 */
	void removeScene(const std::string &sceneName);

	/**
	 * @brief Clear all scenes (including active scene)
	 */
	void clearAllScenes();

	/**
	 * @brief Check if a scene exists
	 * @param sceneName The name of the scene
	 * @return true if scene exists, false otherwise
	 */
	[[nodiscard]] bool hasScene(const std::string &sceneName) const;

	/**
	 * @brief Set the engine context for all scenes
	 * @param context The engine context
	 */
	void setEngineContext(engine::EngineContext *context);

  private:
	std::map<std::string, std::shared_ptr<Scene>> m_scenes;
	std::shared_ptr<Scene> m_activeScene = nullptr;
	std::string m_activeSceneName;
	engine::EngineContext *m_engineContext = nullptr;
	mutable std::mutex m_sceneMutex; // Protects m_activeScene access

	// Async loading state
	bool m_isLoading = false;
	std::string m_loadingSceneName;

	/**
	 * @brief Initialize all nodes in a scene tree recursively
	 * @param node The node to initialize (and its children)
	 */
	void initializeNodeTree(std::shared_ptr<engine::scene::nodes::Node> node);

	/**
	 * @brief Activate a scene by starting all enabled nodes
	 * @param scene The scene to activate
	 */
	void activateScene(std::shared_ptr<Scene> scene);

	/**
	 * @brief Clean up scene resources without destroying node structure
	 * @param scene The scene to clean up (keeps structure for reuse)
	 */
	void cleanupSceneResources(std::shared_ptr<Scene> scene);

	/**
	 * @brief Start all enabled nodes in a tree recursively
	 * @param node The root node to start from
	 */
	void startNodeTree(std::shared_ptr<engine::scene::nodes::Node> node);
};

} // namespace engine::scene
