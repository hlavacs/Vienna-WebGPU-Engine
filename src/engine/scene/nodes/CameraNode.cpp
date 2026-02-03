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

	// Initial update
	updateMatrices();
}

void CameraNode::lookAt(const glm::vec3 &target, const glm::vec3 &up)
{
	m_transform.lookAt(target, up);
	m_dirtyView = m_dirtyFrustum = true;
}

void CameraNode::pan(float deltaX, float deltaY)
{
	// Calculate right vector in world space
	glm::vec3 right = m_transform.right();
	glm::vec3 up = m_transform.up();

	// Calculate movement delta in world space
	glm::vec3 movement = right * deltaX + up * deltaY;

	// Apply movement to position
	m_transform.translate(movement, false);
	m_dirtyView = m_dirtyFrustum = true;
}

void CameraNode::tilt(float deltaX, float deltaY)
{
	// Create rotation quaternions for pitch and yaw
	// Yaw around world up vector (Y-axis)
	glm::quat yawQuat = glm::angleAxis(glm::radians(deltaX), glm::vec3(0.0f, 1.0f, 0.0f));

	// Pitch around local right vector (X-axis)
	glm::vec3 rightAxis = m_transform.right();
	glm::quat pitchQuat = glm::angleAxis(glm::radians(deltaY), rightAxis);

	// Apply yaw first (global) then pitch (local)
	glm::quat currentRotation = m_transform.getRotation();
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
	m_transform.setLocalRotation(newRotation);
	m_dirtyView = m_dirtyFrustum = true;
}

void CameraNode::dolly(float delta)
{
	glm::vec3 forward = glm::normalize(m_transform.forward());

	// Pure geometric dolly: caller decides scaling/curve
	m_transform.translate(forward * delta, false);

	m_dirtyView = true;
	m_dirtyFrustum = true;
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
	if (!m_dirtyView && !m_dirtyProjection && m_lastTransformVersion == m_transform.getVersion())
		return;

	if (m_dirtyView || m_lastTransformVersion != m_transform.getVersion())
	{
		glm::vec3 pos = m_transform.getPosition();
		m_viewMatrix = glm::lookAt(
			pos,
			pos + m_transform.forward(),
			m_transform.up() // Not world up, use transform's up because of tilt
		);
		m_dirtyView = false;
	}

	if (m_dirtyProjection|| m_lastTransformVersion != m_transform.getVersion())
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

	m_lastTransformVersion = m_transform.getVersion();
	m_viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
}

glm::vec3 CameraNode::getPosition() const
{
	return m_transform.getPosition();
}

const engine::math::Frustum &CameraNode::getFrustum() const
{
	updateFrustumPlanes();
	return m_frustum;
}

void CameraNode::updateFrustumPlanes() const
{
	if (!m_dirtyFrustum)
		return;
	updateMatrices();
	glm::mat4 vp = m_viewProjectionMatrix;
	m_frustum = engine::math::Frustum::fromViewProjection(vp);
	m_dirtyFrustum = false;
}
} // namespace engine::scene::nodes