#include "engine/scene/SceneManager.h"
#include "engine/EngineContext.h"
#include "engine/resources/ResourceManager.h"
#include "engine/scene/nodes/ModelRenderNode.h"
#include <spdlog/spdlog.h>

namespace engine::scene
{

std::shared_ptr<Scene> SceneManager::getActiveScene() const
{
	std::lock_guard<std::mutex> lock(m_sceneMutex);
	return m_activeScene;
}

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
	if (scene->getRoot() && m_engineContext)
	{
		// setEngineContext will automatically propagate to all children
		scene->getRoot()->setEngineContext(m_engineContext);
	}
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

	scene->setEngineContext(m_engineContext);			 // Set engine context
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

	auto newScene = it->second;

	// Initialize the new scene first (loads resources, etc.)
	if (newScene && newScene->getRoot())
	{
		initializeNodeTree(newScene->getRoot());
	}

	// Store old scene for cleanup
	auto oldScene = m_activeScene;
	auto oldSceneName = m_activeSceneName;

	// Switch to new scene (protected by mutex)
	{
		std::lock_guard<std::mutex> lock(m_sceneMutex);
		m_activeScene = newScene;
		m_activeSceneName = sceneName;
	}

	// Activate the new scene (starts nodes)
	activateScene(m_activeScene);

	// Mark scene as loaded
	if (m_activeScene)
	{
		m_activeScene->setLoaded(true);
	}

	// Cleanup old scene resources (but keep structure for reuse)
	if (oldScene)
	{
		spdlog::info("Cleaning up scene '{}' resources", oldSceneName);
		cleanupSceneResources(oldScene);
	}

	spdlog::info("Loaded scene '{}'", sceneName);
	return true;
}

std::future<bool> SceneManager::loadSceneAsync(const std::string &sceneName)
{
	auto it = m_scenes.find(sceneName);
	if (it == m_scenes.end())
	{
		spdlog::error("Scene '{}' not found, cannot load", sceneName);
		std::promise<bool> promise;
		promise.set_value(false);
		return promise.get_future();
	}

	if (m_isLoading)
	{
		spdlog::warn("Already loading scene '{}', cannot load '{}'", m_loadingSceneName, sceneName);
		std::promise<bool> promise;
		promise.set_value(false);
		return promise.get_future();
	}

	m_isLoading = true;
	m_loadingSceneName = sceneName;

	spdlog::info("Starting async load of scene '{}'", sceneName);

	auto newScene = it->second;

	// Launch async initialization - ONLY initialize, don't touch m_activeScene
	auto initFuture = std::async(std::launch::async, [this, newScene, sceneName]() -> bool
	{
		// Initialize all nodes in the scene tree (loads resources)
		// This runs on worker thread - no scene switching here!
		if (newScene && newScene->getRoot())
		{
			initializeNodeTree(newScene->getRoot());
		}

		spdlog::info("Scene '{}' initialization complete", sceneName);
		return true;
	});

	// Wait for initialization to complete
	bool success = initFuture.get();

	if (!success)
	{
		m_isLoading = false;
		m_loadingSceneName.clear();
		return std::async(std::launch::deferred, []() { return false; });
	}

	// Now on the calling thread, do the scene switch
	auto oldScene = m_activeScene;
	auto oldSceneName = m_activeSceneName;

	// Switch to new scene (on main thread)
	{
		std::lock_guard<std::mutex> lock(m_sceneMutex);
		m_activeScene = newScene;
		m_activeSceneName = sceneName;
	}

	// Activate new scene
	activateScene(m_activeScene);

	// Mark scene as loaded
	if (m_activeScene)
	{
		m_activeScene->setLoaded(true);
	}

	// Cleanup old scene resources
	if (oldScene)
	{
		spdlog::info("Cleaning up scene '{}' resources", oldSceneName);
		cleanupSceneResources(oldScene);
	}

	m_isLoading = false;
	m_loadingSceneName.clear();

	spdlog::info("Completed scene load of '{}'", sceneName);

	// Return a completed future
	return std::async(std::launch::deferred, []() { return true; });
}

void SceneManager::initializeNodeTree(std::shared_ptr<engine::scene::nodes::Node> node)
{
	if (!node)
		return;

	// Check if this is a ModelRenderNode that needs loading
	auto modelRenderNode = std::dynamic_pointer_cast<engine::scene::nodes::ModelRenderNode>(node);
	if (modelRenderNode && modelRenderNode->needsLoading())
	{
		// Load the model through the resource manager
		if (m_engineContext && m_engineContext->resources())
		{
			auto modelManager = m_engineContext->resources()->m_modelManager;
			if (modelManager)
			{
				auto modelOpt = modelManager->createModel(modelRenderNode->getModelPath());
				if (modelOpt && *modelOpt)
				{
					modelRenderNode->setLoadedModel((*modelOpt)->getHandle());
				}
				else
				{
					spdlog::error("Failed to load model: {}", modelRenderNode->getModelPath().string());
				}
			}
		}
	}

	// Initialize this node (can be called multiple times for scene reuse)
	node->initialize();

	// Recursively initialize all children
	for (auto &child : node->getChildren())
	{
		initializeNodeTree(child);
	}
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

void SceneManager::setEngineContext(engine::EngineContext *context)
{
	m_engineContext = context;
	// Update all existing scenes
	for (auto &[name, scene] : m_scenes)
	{
		if (scene)
		{
			scene->setEngineContext(context);
			if (scene->getRoot())
			{
				// setEngineContext will automatically propagate to all children
				scene->getRoot()->setEngineContext(context);
			}
		}
	}
}

void SceneManager::activateScene(std::shared_ptr<Scene> scene)
{
	if (!scene)
		return;

	// Ensure engine context is set - it will automatically propagate to all nodes
	if (scene->getRoot() && m_engineContext)
	{
		scene->getRoot()->setEngineContext(m_engineContext);
	}

	// Start all enabled nodes that haven't been started yet
	if (scene->getRoot())
	{
		startNodeTree(scene->getRoot());
	}

	spdlog::debug("Activated scene with cameras and nodes");
}

void SceneManager::cleanupSceneResources(std::shared_ptr<Scene> scene)
{
	if (!scene)
		return;

	// Mark scene as unloaded
	scene->setLoaded(false);

	// Just call onDestroy on the root - it will handle all children recursively
	if (scene->getRoot())
	{
		scene->getRoot()->onDestroy();
		// Mark as not started so it can be re-initialized
		scene->getRoot()->started = false;
	}

	spdlog::debug("Cleaned up scene resources (structure preserved for reuse)");
}

void SceneManager::startNodeTree(std::shared_ptr<engine::scene::nodes::Node> node)
{
	if (!node)
		return;

	// Start this node if it's enabled and hasn't been started
	if (node->isEnabled() && !node->started)
	{
		node->start();
	}

	// Recursively start all children
	for (auto &child : node->getChildren())
	{
		startNodeTree(child);
	}
}

} // namespace engine::scene
