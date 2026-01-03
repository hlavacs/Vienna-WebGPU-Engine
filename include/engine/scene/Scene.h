#pragma once

#include <memory>
#include <set>
#include <vector>

#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderProxies.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/Node.h"

namespace engine
{
class EngineContext;
class GameEngine;
} // namespace engine

namespace engine::scene
{
/**
 * @brief Main scene class that manages the scene graph and frame lifecycle.
 */
class Scene
{
  public:
	using Ptr = std::shared_ptr<Scene>;

	Scene();
	virtual ~Scene() = default;

	/** @brief Set the root node of the scene */
	void setRoot(nodes::Node::Ptr root) { m_root = root; }

	/** @brief Get the root node of the scene */
	nodes::Node::Ptr getRoot() const { return m_root; }

	/** @brief Set the main camera.
	 * @param camera The camera node to set as the main camera.
	 * @note This camera will be used for UI rendering and as the default view.
	 */
	void setMainCamera(nodes::CameraNode::Ptr camera)
	{
		m_cameras.emplace(camera);
		m_mainCamera = camera;
	}

	/** @brief Get the active camera */
	nodes::CameraNode::Ptr getMainCamera() const { return m_mainCamera; }

	/** @brief Get all active cameras in the scene
	 * @note The main camera is always first in the returned vector
	 */
	std::vector<std::shared_ptr<nodes::CameraNode>> getActiveCameras() const
	{
		if (!m_mainCamera || !m_mainCamera->isEnabled())
			return {}; // If no main camera we will not rendrer anything

		std::vector<std::shared_ptr<nodes::CameraNode>> result;
		result.push_back(m_mainCamera);

		// ToDo: Order by some criteria (layer, priority, etc.)
		for (auto &cam : m_cameras)
		{
			if (cam != m_mainCamera && cam->isEnabled())
				result.push_back(cam);
		}

		return result;
	}

	/** @brief Add a camera to the scene
	 * @return true if added, false if already present
	 */
	bool addCamera(nodes::CameraNode::Ptr camera)
	{
		return m_cameras.emplace(camera).second;
	}

	/** @brief Get the debug render collector for this frame */
	const engine::rendering::DebugRenderCollector &getDebugCollector() const { return m_debugCollector; }

	/** @brief Set the engine context for node access to engine systems */
	void setEngineContext(engine::EngineContext *context) { m_engineContext = context; }

	/** @brief Get the engine context */
	engine::EngineContext *getEngineContext() const { return m_engineContext; }

  protected:
	friend class engine::GameEngine;

	/** @brief Update phase - movement, animation, input, gameplay logic */
	void update(float deltaTime);

	/** @brief Late update phase - order-dependent logic like camera following */
	void lateUpdate(float deltaTime);

	/**
	 * @brief Collect render proxies from scene graph.
	 * 
	 * Traverses the scene graph and collects RenderProxy objects from all enabled RenderNodes.
	 * These proxies are then processed by the renderer to populate the RenderCollector.
	 * 
	 * @param outProxies Vector to append RenderProxy objects to.
	 * @param outLights Vector to append light data to (lights are collected separately).
	 */
	void collectRenderProxies(
		std::vector<std::shared_ptr<engine::rendering::RenderProxy>> &outProxies,
		std::vector<engine::rendering::LightStruct> &outLights
	);

	/** @brief Collect debug primitives from nodes with debug enabled */
	void collectDebugData();

	/** @brief Pre-render phase - prepare nodes for rendering (GPU resource updates) */
	void preRender();

	/** @brief Post-render phase - cleanup after rendering */
	void postRender();

  private:
	nodes::Node::Ptr m_root;
	nodes::CameraNode::Ptr m_mainCamera;
	std::set<nodes::CameraNode::Ptr> m_cameras;
	engine::rendering::DebugRenderCollector m_debugCollector;
	engine::EngineContext *m_engineContext = nullptr;
};
} // namespace engine::scene