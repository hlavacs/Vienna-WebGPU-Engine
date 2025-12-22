#include "engine/scene/CameraNode.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::scene
{

CameraNode::CameraNode()
{
	addNodeType(entity::NodeType::Camera);
	
	// Initialize camera parameters
	m_fov = 45.0f;
	m_aspect = 16.0f / 9.0f;
	m_near = 0.1f;
	m_far = 100.0f;

	// Position the camera initially
	if (m_transform)
	{
		// Set position looking toward origin (negative Z direction)
		m_transform->setLocalPosition({0.0f, 0.0f, -5.0f});

		// Default orientation looking down the Z axis (no rotation needed for -Z direction)
		m_transform->setLocalRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
	}

	// Initial update
	updateMatrices();
}

void CameraNode::setTransform(const std::shared_ptr<Transform> &t)
{
	m_transform = t;
	updateMatrices();
}

void CameraNode::lookAt(const glm::vec3 &target, const glm::vec3 &up)
{
	if (!m_transform)
		return;

	m_transform->lookAt(target, up);

	updateMatrices();
}

void CameraNode::pan(float deltaX, float deltaY)
{
	if (!m_transform)
		return;

	// Calculate right vector in world space
	glm::vec3 right = m_transform->right();
	glm::vec3 up = m_transform->up();

	// Calculate movement delta in world space
	glm::vec3 movement = right * deltaX + up * deltaY;

	// Apply movement to position
	m_transform->translate(movement, false);

	// Update camera matrices
	updateMatrices();
}

void CameraNode::tilt(float deltaX, float deltaY)
{
	if (!m_transform)
		return;

	// Create rotation quaternions for pitch and yaw
	// Yaw around world up vector (Y-axis)
	glm::quat yawQuat = glm::angleAxis(glm::radians(deltaX), glm::vec3(0.0f, 1.0f, 0.0f));

	// Pitch around local right vector (X-axis)
	glm::vec3 rightAxis = m_transform->right();
	glm::quat pitchQuat = glm::angleAxis(glm::radians(deltaY), rightAxis);

	// Apply yaw first (global) then pitch (local)
	glm::quat currentRotation = m_transform->getRotation();
	glm::quat newRotation = pitchQuat * currentRotation * yawQuat;

	// Normalize to prevent drift
	newRotation = glm::normalize(newRotation);

	// Check for pitch limits to prevent gimbal lock
	// Convert to euler angles temporarily just to check pitch limits
	glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(newRotation));

	// Fix euler angles discontinuity
	if (eulerAngles.x > 90.0f)
		eulerAngles.x -= 360.0f;

	// Clamp pitch to prevent flipping
	if (eulerAngles.x < -89.0f)
		eulerAngles.x = -89.0f;
	if (eulerAngles.x > 89.0f)
		eulerAngles.x = 89.0f;

	// Convert back if we clamped
	if (eulerAngles.x != glm::degrees(glm::eulerAngles(newRotation)).x)
	{
		newRotation = glm::quat(glm::radians(eulerAngles));
	}

	// Set new rotation
	m_transform->setLocalRotation(newRotation);

	// Update camera matrices
	updateMatrices();
}

void CameraNode::dolly(float delta)
{
	if (!m_transform)
		return;

	// Use exponential zooming for more natural feel
	// This provides smooth zooming at both close and far distances
	float zoomFactor = std::exp(delta * 0.1f); // Scale factor to adjust zoom speed

	// Move camera along its forward vector based on exponential scale
	glm::vec3 forward = m_transform->forward();
	m_transform->translate(forward * delta * 2.0f, false); // Multiply by 2.0 for more responsive zooming

	// Update camera matrices
	updateMatrices();
}

void CameraNode::setFov(float fovDegrees)
{
	m_fov = fovDegrees;
	updateMatrices();
}

void CameraNode::setAspect(float aspect)
{
	m_aspect = aspect;
	updateMatrices();
}

void CameraNode::setNearFar(float near, float far)
{
	m_near = near;
	m_far = far;
	updateMatrices();
}

void CameraNode::setPerspective(bool perspective)
{
	m_isPerspective = perspective;
	updateMatrices();
}

void CameraNode::setOrthographicSize(float size)
{
	m_orthographicSize = size;
	updateMatrices();
}

float CameraNode::getFov() const
{
	return m_fov;
}

float CameraNode::getAspect() const
{
	return m_aspect;
}

float CameraNode::getNear() const
{
	return m_near;
}

float CameraNode::getFar() const
{
	return m_far;
}

bool CameraNode::isPerspective() const
{
	return m_isPerspective;
}

float CameraNode::getOrthographicSize() const
{
	return m_orthographicSize;
}

// View and projection matrix getters are now inline in the header

void CameraNode::update(float deltaTime)
{
	// No specific update needed in the update phase
}

void CameraNode::lateUpdate(float deltaTime)
{
	// Final transform-based updates after all other nodes
	updateMatrices();
}

void CameraNode::preRender()
{
	// Final matrices update before rendering
	// This is where matrices can be updated before being used by the renderer
	updateMatrices();
}

void CameraNode::updateMatrices()
{
	if (!m_transform)
		return;

	glm::vec3 pos = m_transform->getPosition();
	m_viewMatrix = glm::lookAt(pos, pos - m_transform->forward(), glm::vec3(0.0f, 1.0f, 0.0f));

	// Calculate projection matrix based on mode
	if (m_isPerspective)
	{
		m_projectionMatrix = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
	}
	else
	{
		// Orthographic projection
		float halfHeight = m_orthographicSize * 0.5f;
		float halfWidth = halfHeight * m_aspect;
		m_projectionMatrix = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_near, m_far);
	}

	// Calculate combined view-projection matrix
	m_viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
}

glm::vec3 CameraNode::getPosition() const
{
	return m_transform ? m_transform->getPosition() : glm::vec3(0.0f);
}

// No camera handle needed since we store all data directly in the node

} // namespace engine::scene