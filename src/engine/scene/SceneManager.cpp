#include "engine/scene/SceneManager.h"
#include <spdlog/spdlog.h>

namespace engine::scene
{

std::shared_ptr<Scene> SceneManager::createScene(const std::string &sceneName)
{
	// Check if scene already exists
	if (hasScene(sceneName))
	{
		spdlog::warn("Scene '{}' already exists, returning existing scene", sceneName);
		return m_scenes[sceneName];
	}

	// Create new scene
	auto scene = std::make_shared<Scene>();
	scene->setEngineContext(m_engineContext); // Set engine context for the scene
	scene->getRoot()->setEngineContext(m_engineContext); // Set context for root node
	m_scenes[sceneName] = scene;
	
	spdlog::info("Created scene '{}'", sceneName);
	return scene;
}

void SceneManager::registerScene(const std::string &sceneName, std::shared_ptr<Scene> scene)
{
	if (!scene)
	{
		spdlog::error("Cannot register null scene '{}'", sceneName);
		return;
	}

	if (hasScene(sceneName))
	{
		spdlog::warn("Scene '{}' already exists, overwriting", sceneName);
	}

	scene->setEngineContext(m_engineContext); // Set engine context
	scene->getRoot()->setEngineContext(m_engineContext); // Set context for root node
	m_scenes[sceneName] = scene;
	spdlog::info("Registered scene '{}'", sceneName);
}

bool SceneManager::loadScene(const std::string &sceneName)
{
	auto it = m_scenes.find(sceneName);
	if (it == m_scenes.end())
	{
		spdlog::error("Scene '{}' not found, cannot load", sceneName);
		return false;
	}

	// Unload previous scene if any
	if (m_activeScene)
	{
		spdlog::info("Unloading scene '{}'", m_activeSceneName);
		// TODO: Add scene cleanup/unload callback here if needed
	}

	// Load new scene
	m_activeScene = it->second;
	m_activeSceneName = sceneName;
	
	spdlog::info("Loaded scene '{}'", sceneName);
	return true;
}

std::shared_ptr<Scene> SceneManager::getScene(const std::string &sceneName) const
{
	auto it = m_scenes.find(sceneName);
	if (it != m_scenes.end())
	{
		return it->second;
	}
	return nullptr;
}

void SceneManager::removeScene(const std::string &sceneName)
{
	// Don't allow removing the active scene
	if (sceneName == m_activeSceneName)
	{
		spdlog::warn("Cannot remove active scene '{}', unload it first", sceneName);
		return;
	}

	auto it = m_scenes.find(sceneName);
	if (it != m_scenes.end())
	{
		m_scenes.erase(it);
		spdlog::info("Removed scene '{}'", sceneName);
	}
}

void SceneManager::clearAllScenes()
{
	m_activeScene = nullptr;
	m_activeSceneName.clear();
	m_scenes.clear();
	spdlog::info("Cleared all scenes");
}

bool SceneManager::hasScene(const std::string &sceneName) const
{
	return m_scenes.find(sceneName) != m_scenes.end();
}

void SceneManager::setEngineContext(engine::EngineContext* context)
{
	m_engineContext = context;
	// Update all existing scenes
	for (auto& [name, scene] : m_scenes)
	{
		if (scene)
		{
			scene->setEngineContext(context);
			if (scene->getRoot())
			{
				scene->getRoot()->setEngineContext(context);
			}
		}
	}
}

} // namespace engine::scene
