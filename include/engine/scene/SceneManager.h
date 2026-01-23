#pragma once

#include "engine/scene/Scene.h"
#include <map>
#include <memory>
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
	 * @brief Get the currently active scene
	 * @return Pointer to active scene, or nullptr if none
	 */
	[[nodiscard]] std::shared_ptr<Scene> getActiveScene() const { return m_activeScene; }

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
};

} // namespace engine::scene
