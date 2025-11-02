#pragma once
#include "engine/scene/SpatialNode.h"
#include "engine/scene/entity/UpdateNode.h"
#include "engine/scene/entity/RenderNode.h"
#include <memory>
#include <glm/glm.hpp>

namespace engine::scene {

/**
 * @brief Node representing a camera in the scene.
 * Stores transform and projection parameters directly in the node.
 * No separate Camera class required - all camera data is contained here.
 * Uses standard transform operations with utility methods for common camera operations.
 * Inherits from both SpatialNode, UpdateNode and RenderNode, using virtual inheritance to avoid diamond inheritance issues.
 */
class CameraNode : public SpatialNode, public entity::UpdateNode, public entity::RenderNode {
public:
	using Ptr = std::shared_ptr<CameraNode>;
	
	// Constructor - always creates an internal camera
	CameraNode();
	virtual ~CameraNode() = default;

	// Override transform setter to also update camera
	void setTransform(const std::shared_ptr<Transform>& t);
	
	// Camera utility methods (calculate and set appropriate transforms)
	void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
	void pan(float deltaX, float deltaY);
	void tilt(float deltaX, float deltaY);
	void dolly(float delta);
	
	// Camera parameter setters (projection related)
	void setFov(float fovDegrees);
	void setAspect(float aspect);
	void setNearFar(float near, float far);
	void setPerspective(bool perspective); // true for perspective, false for orthographic
	void setOrthographicSize(float size);  // for orthographic projection
	
	// Camera parameter getters
	float getFov() const;
	float getAspect() const;
	float getNear() const;
	float getFar() const;
	bool isPerspective() const;
	float getOrthographicSize() const;
	
	// Camera matrices directly accessible
	const glm::mat4& getViewMatrix() const { return m_viewMatrix; }
	const glm::mat4& getProjectionMatrix() const { return m_projectionMatrix; }
	const glm::mat4& getViewProjectionMatrix() const { return m_viewProjectionMatrix; }
	
	// Get camera position (from transform)
	glm::vec3 getPosition() const;
	
	// Override methods from UpdateNode
	void update(float deltaTime) override;
	void lateUpdate(float deltaTime) override;
	
	// Override methods from RenderNode
	void preRender() override;
	
private:
	// Recalculate matrices from transform and parameters
	void updateMatrices();
	
protected:
	// Camera matrices calculated directly in the node
	glm::mat4 m_viewMatrix = glm::mat4(1.0f);
	glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
	glm::mat4 m_viewProjectionMatrix = glm::mat4(1.0f);
	
	// Camera parameters
	float m_fov = 45.0f;
	float m_aspect = 16.0f / 9.0f;
	float m_near = 0.1f;
	float m_far = 100.0f;
	bool m_isPerspective = true;
	float m_orthographicSize = 5.0f;
};
} // namespace engine::scene
