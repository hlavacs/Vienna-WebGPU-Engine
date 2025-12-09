#pragma once
#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/entity/Node.h"
#include <memory>

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
	void setRoot(entity::Node::Ptr root) { m_root = root; }

	/** @brief Get the root node of the scene */
	entity::Node::Ptr getRoot() const { return m_root; }

	/** @brief Set the active camera */
	void setActiveCamera(CameraNode::Ptr camera) { m_activeCamera = camera; }

	/** @brief Get the active camera */
	CameraNode::Ptr getActiveCamera() const { return m_activeCamera; }

	/** @brief Get the render collector for this frame */
	const engine::rendering::RenderCollector &getRenderCollector() const { return m_renderCollector; }

	/** @brief Get the debug render collector for this frame */
	const engine::rendering::DebugRenderCollector &getDebugCollector() const { return m_debugCollector; }

	/** @brief Set the engine context for node access to engine systems */
	void setEngineContext(engine::EngineContext *context) { m_engineContext = context; }

	/** @brief Get the engine context */
	engine::EngineContext *getEngineContext() const { return m_engineContext; }

  protected:
	friend class engine::GameEngine;

	/** @brief Process a complete frame lifecycle */
	void onFrame(float deltaTime);

	/** @brief Update phase - movement, animation, input, gameplay logic */
	void update(float deltaTime);

	/** @brief Late update phase - order-dependent logic like camera following */
	void lateUpdate(float deltaTime);

	/** @brief Collect render data from scene graph into RenderCollector and sort */
	void collectRenderData();

	/** @brief Collect debug primitives from nodes with debug enabled */
	void collectDebugData();

	/** @brief Pre-render phase - prepare nodes for rendering (GPU resource updates) */
	void preRender();

	/** @brief Post-render phase - cleanup after rendering */
	void postRender();

  private:
	entity::Node::Ptr m_root;
	CameraNode::Ptr m_activeCamera;
	engine::rendering::RenderCollector m_renderCollector;
	engine::rendering::DebugRenderCollector m_debugCollector;
	engine::EngineContext *m_engineContext = nullptr;
};
} // namespace engine::scene