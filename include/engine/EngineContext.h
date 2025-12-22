#pragma once
#include <memory>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/polar_coordinates.hpp>

namespace engine
{

// Forward declarations
class GameEngine;

namespace input
{
class InputManager;
}

namespace rendering::webgpu
{
class WebGPUContext;
}

namespace resources
{
class ResourceManager;
}

namespace scene
{
class SceneManager;
}

/**
 * @brief Provides access to core engine systems for nodes and other subsystems.
 * This prevents circular dependencies and allows nodes to access engine services.
 */
class EngineContext
{
  public:
	EngineContext() = default;
	~EngineContext() = default;

	// Non-copyable, non-movable
	EngineContext(const EngineContext&) = delete;
	EngineContext& operator=(const EngineContext&) = delete;
	EngineContext(EngineContext&&) = delete;
	EngineContext& operator=(EngineContext&&) = delete;

	// Access to core systems
	input::InputManager* getInputManager() const { return m_inputManager; }
	rendering::webgpu::WebGPUContext* getWebGPUContext() const { return m_webgpuContext; }
	resources::ResourceManager* getResourceManager() const { return m_resourceManager; }
	scene::SceneManager* getSceneManager() const { return m_sceneManager; }
	
	// Convenient direct access (less typing for node code)
	input::InputManager* input() const { return m_inputManager; }
	rendering::webgpu::WebGPUContext* gpu() const { return m_webgpuContext; }
	resources::ResourceManager* resources() const { return m_resourceManager; }
	scene::SceneManager* scenes() const { return m_sceneManager; }

	// Called by GameEngine during initialization
	void setInputManager(input::InputManager* manager) { m_inputManager = manager; }
	void setWebGPUContext(rendering::webgpu::WebGPUContext* context) { m_webgpuContext = context; }
	void setResourceManager(resources::ResourceManager* manager) { m_resourceManager = manager; }
	void setSceneManager(scene::SceneManager* manager) { m_sceneManager = manager; }

  private:
	input::InputManager* m_inputManager = nullptr;
	rendering::webgpu::WebGPUContext* m_webgpuContext = nullptr;
	resources::ResourceManager* m_resourceManager = nullptr;
	scene::SceneManager* m_sceneManager = nullptr;
};

} // namespace engine
