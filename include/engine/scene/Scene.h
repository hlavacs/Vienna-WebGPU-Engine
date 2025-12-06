#pragma once
#include "engine/scene/entity/Node.h"
#include "engine/scene/CameraNode.h"
#include "engine/rendering/RenderCollector.h"
#include <memory>

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
    
    /** @brief Process a complete frame lifecycle */
    void onFrame(float deltaTime);
    
    /** @brief Update phase - movement, animation, input, gameplay logic */
    void update(float deltaTime);
    
    /** @brief Late update phase - order-dependent logic like camera following */
    void lateUpdate(float deltaTime);
    
    /** @brief Pre-render phase - prepare for rendering */
    void preRender();
    
    /** @brief Render phase - render all objects */
    void render();
    
    /** @brief Post-render phase - cleanup after rendering */
    void postRender();
    
    /** @brief Set the root node of the scene */
    void setRoot(entity::Node::Ptr root) { m_root = root; }
    
    /** @brief Get the root node of the scene */
    entity::Node::Ptr getRoot() const { return m_root; }
    
    /** @brief Set the active camera */
    void setActiveCamera(CameraNode::Ptr camera) { m_activeCamera = camera; }
    
    /** @brief Get the active camera */
    CameraNode::Ptr getActiveCamera() const { return m_activeCamera; }
    
    /** @brief Get the render collector for this frame */
    const engine::rendering::RenderCollector& getRenderCollector() const { return m_renderCollector; }
    
private:
    entity::Node::Ptr m_root;
    CameraNode::Ptr m_activeCamera;
    engine::rendering::RenderCollector m_renderCollector;
};
} // namespace engine::scene