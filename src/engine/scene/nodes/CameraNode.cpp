#include "engine/scene/nodes/CameraNode.h"
#include "engine/resources/ResourceManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::scene::nodes
{

CameraNode::CameraNode() : m_fov(45.0f), m_aspect(16.0f / 9.0f), m_near(0.1f), m_far(100.0f)
{
	addNodeType(nodes::NodeType::Camera);

	// Initialize camera parameters

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
	m_dirtyView = m_dirtyFrustum = true;
}

void CameraNode::lookAt(const glm::vec3 &target, const glm::vec3 &up)
{
	if (!m_transform)
		return;

	m_transform->lookAt(target, up);
	m_dirtyView = m_dirtyFrustum = true;
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
	m_dirtyView = m_dirtyFrustum = true;
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
	m_dirtyView = m_dirtyFrustum = true;
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

	m_dirtyView = m_dirtyFrustum = true;
}

void CameraNode::setFov(float fovDegrees)
{
	m_fov = fovDegrees;
	m_dirtyProjection = m_dirtyFrustum = true;
}

void CameraNode::setNearFar(float _near, float _far)
{
	m_near = _near;
	m_far = _far;
	m_dirtyProjection = m_dirtyFrustum = true;
}

void CameraNode::setPerspective(bool perspective)
{
	m_isPerspective = perspective;
	m_dirtyProjection = m_dirtyFrustum = true;
}

void CameraNode::setOrthographicSize(float size)
{
	m_orthographicSize = size;
	m_dirtyProjection = m_dirtyFrustum = true;
}

// View and projection matrix getters are now inline in the header

void CameraNode::update(float deltaTime)
{
}

void CameraNode::lateUpdate(float deltaTime)
{
}

void CameraNode::preRender()
{
	// Final matrices update before rendering
	// This is where matrices can be updated before being used by the renderer
	updateMatrices();
	updateFrustumPlanes();
}

void CameraNode::updateMatrices() const
{
	if (!m_transform)
		return;

	if (!m_dirtyView && !m_dirtyProjection)
		return;

	if (m_dirtyView)
	{
		glm::vec3 pos = m_transform->getPosition();
		m_viewMatrix = glm::lookAt(
			pos,
			pos - m_transform->forward(),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);
		m_dirtyView = false;
	}

	if (m_dirtyProjection)
	{
		if (m_isPerspective)
		{
			m_projectionMatrix = glm::perspective(
				glm::radians(m_fov),
				m_aspect,
				m_near,
				m_far
			);
		}
		else
		{
			float halfHeight = m_orthographicSize * 0.5f;
			float halfWidth = halfHeight * m_aspect;
			m_projectionMatrix = glm::ortho(
				-halfWidth,
				halfWidth,
				-halfHeight,
				halfHeight,
				m_near,
				m_far
			);
		}
		m_dirtyProjection = false;
	}

	m_viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
}

glm::vec3 CameraNode::getPosition() const
{
	return m_transform ? m_transform->getPosition() : glm::vec3(0.0f);
}

const engine::math::Frustum &CameraNode::getFrustum() const
{
	// ToDo: Lazy update only when needed
	updateFrustumPlanes();
	return m_frustum;
}

void CameraNode::updateFrustumPlanes() const
{
	if (!m_dirtyFrustum)
		return;
	glm::mat4 vp = m_viewProjectionMatrix;

	// Left
	m_frustum.leftPlane.normal = glm::vec3(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0]);
	m_frustum.leftPlane.d = vp[3][3] + vp[3][0];

	// Right
	m_frustum.rightPlane.normal = glm::vec3(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0]);
	m_frustum.rightPlane.d = vp[3][3] - vp[3][0];

	// Bottom
	m_frustum.bottomPlane.normal = glm::vec3(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1]);
	m_frustum.bottomPlane.d = vp[3][3] + vp[3][1];
	// Top
	m_frustum.topPlane.normal = glm::vec3(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1]);
	m_frustum.topPlane.d = vp[3][3] - vp[3][1];

	// Near
	m_frustum.nearPlane.normal = glm::vec3(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2]);
	m_frustum.nearPlane.d = vp[3][3] + vp[3][2];
	// Far
	m_frustum.farPlane.normal = glm::vec3(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2]);
	m_frustum.farPlane.d = vp[3][3] - vp[3][2];

	// Normalize
	for (auto &p : m_frustum.asArray())
	{
		float len = glm::length(p->normal);
		p->normal /= len;
		p->d /= len;
	}
}
} // namespace engine::scene::nodes