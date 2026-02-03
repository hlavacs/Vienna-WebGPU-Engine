#include "engine/scene/Transform.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::scene
{

Transform::Transform() = default;
Transform::~Transform() = default;

void Transform::setLocalPosition(const glm::vec3 &position)
{
	m_localPosition = position;
	markDirty();
}

void Transform::setLocalRotation(const glm::quat &rotation)
{
	// Convert quaternion to Euler angles and store those as primary
	// Note: This may result in different but equivalent Euler angles
	m_localEulerAngles = glm::degrees(glm::eulerAngles(rotation));

	// Cache the quaternion to avoid immediate recomputation
	m_localRotationCache = rotation;
	m_dirtyRotation = false;

	markDirty();
}

void Transform::setLocalEulerAngles(const glm::vec3 &euler)
{
	m_localEulerAngles = euler;
	m_dirtyRotation = true; // Quaternion needs recomputation
	markDirty();
}

void Transform::setLocalScale(const glm::vec3 &scale)
{
	m_localScale = scale;
	markDirty();
}

const glm::vec3 &Transform::getLocalPosition() const
{
	return m_localPosition;
}

const glm::quat &Transform::getLocalRotation() const
{
	if (m_dirtyRotation)
		updateRotationFromEuler();
	return m_localRotationCache;
}

const glm::vec3 &Transform::getLocalEulerAngles() const
{
	return m_localEulerAngles;
}

const glm::vec3 &Transform::getLocalScale() const
{
	return m_localScale;
}

glm::vec3 Transform::getPosition() const
{
	return glm::vec3(getWorldMatrix()[3]);
}

glm::quat Transform::getRotation() const
{
	glm::mat4 world = getWorldMatrix();
	return glm::quat_cast(world);
}

glm::vec3 Transform::getScale() const
{
	glm::mat4 world = getWorldMatrix();
	return {glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))};
}

glm::vec3 Transform::getEulerAngles() const
{
	return glm::degrees(glm::eulerAngles(getRotation()));
}

void Transform::setWorldPosition(const glm::vec3 &position)
{
	if (m_parent)
	{
		// Convert world position to local space
		glm::mat4 invParentWorld = glm::inverse(m_parent->getWorldMatrix());
		glm::vec3 localPos = glm::vec3(invParentWorld * glm::vec4(position, 1.0f));
		setLocalPosition(localPos);
	}
	else
	{
		// No parent, world = local
		setLocalPosition(position);
	}
}

void Transform::setWorldRotation(const glm::quat &rotation)
{
	if (m_parent)
	{
		// Convert world rotation to local space
		glm::quat parentRotation = m_parent->getRotation();
		glm::quat localRot = glm::inverse(parentRotation) * rotation;
		setLocalRotation(localRot);
	}
	else
	{
		// No parent, world = local
		setLocalRotation(rotation);
	}
}

void Transform::setWorldScale(const glm::vec3 &scale)
{
	if (m_parent)
	{
		// Convert world scale to local space by dividing by parent scale
		glm::vec3 parentScale = m_parent->getScale();
		glm::vec3 localScale = scale / parentScale;
		setLocalScale(localScale);
	}
	else
	{
		// No parent, world = local
		setLocalScale(scale);
	}
}

glm::mat4 Transform::getLocalMatrix() const
{
	if (m_dirtyLocal)
		updateLocalMatrix();
	return m_localMatrixCache;
}

glm::mat4 Transform::getWorldMatrix() const
{
	if (m_dirtyWorld)
		updateWorldMatrix();
	return m_worldMatrixCache;
}

void Transform::updateRotationFromEuler() const
{
	// Convert Euler angles to quaternion using XYZ order (Unity-compatible)
	glm::vec3 radians = glm::radians(m_localEulerAngles);

	// Create individual axis rotations
	glm::quat rotX = glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::quat rotY = glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::quat rotZ = glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f));

	// Combine in XYZ order (right-to-left multiplication)
	m_localRotationCache = rotZ * rotY * rotX;
	m_dirtyRotation = false;
}

void Transform::updateLocalMatrix() const
{
	// Ensure rotation quaternion is up to date
	if (m_dirtyRotation)
		updateRotationFromEuler();

	glm::mat4 T = glm::translate(glm::mat4(1.0f), m_localPosition);
	glm::mat4 R = glm::toMat4(m_localRotationCache);
	glm::mat4 S = glm::scale(glm::mat4(1.0f), m_localScale);
	m_localMatrixCache = T * R * S;
	m_dirtyLocal = false;
}

void Transform::updateWorldMatrix() const
{
	if (m_dirtyLocal)
		updateLocalMatrix();

	if (m_parent)
	{
		m_worldMatrixCache = m_parent->getWorldMatrix() * m_localMatrixCache;
	}
	else
	{
		m_worldMatrixCache = m_localMatrixCache;
	}
	m_dirtyWorld = false;
	// Note: Children propagation is handled by SpatialNode hierarchy
}

glm::vec3 Transform::forward() const
{
	return glm::normalize(getRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
}

glm::vec3 Transform::right() const
{
	return glm::normalize(getRotation() * glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 Transform::up() const
{
	return glm::normalize(getRotation() * glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec3 Transform::localForward() const
{
	if (m_dirtyRotation)
		updateRotationFromEuler();
	return glm::normalize(m_localRotationCache * glm::vec3(0.0f, 0.0f, -1.0f));
}

glm::vec3 Transform::localRight() const
{
	if (m_dirtyRotation)
		updateRotationFromEuler();
	return glm::normalize(m_localRotationCache * glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 Transform::localUp() const
{
	if (m_dirtyRotation)
		updateRotationFromEuler();
	return glm::normalize(m_localRotationCache * glm::vec3(0.0f, 1.0f, 0.0f));
}

void Transform::translate(const glm::vec3 &delta, bool local)
{
	if (local)
	{
		setLocalPosition(m_localPosition + delta);
	}
	else
	{
		// World-space translation: convert world delta to local space
		if (m_parent)
		{
			// Transform world direction to local space (direction, so w=0)
			glm::mat4 invParentWorld = glm::inverse(m_parent->getWorldMatrix());
			glm::vec3 localDelta = glm::vec3(invParentWorld * glm::vec4(delta, 0.0f));
			setLocalPosition(m_localPosition + localDelta);
		}
		else
		{
			// No parent, world = local
			setLocalPosition(m_localPosition + delta);
		}
	}
}

void Transform::rotate(const glm::vec3 &eulerDegrees, bool local)
{
	// Apply rotation in XYZ order (Unity-compatible)
	glm::vec3 eulerRadians = glm::radians(eulerDegrees);

	// Create individual axis rotations
	glm::quat rotX = glm::angleAxis(eulerRadians.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::quat rotY = glm::angleAxis(eulerRadians.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::quat rotZ = glm::angleAxis(eulerRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));

	// Combine in XYZ order
	glm::quat deltaRotation = rotZ * rotY * rotX;

	if (local)
	{
		// Apply rotation to current local rotation
		if (m_dirtyRotation)
			updateRotationFromEuler();

		glm::quat newRotation = m_localRotationCache * deltaRotation;

		// Convert back to Euler to maintain continuity
		m_localEulerAngles = glm::degrees(glm::eulerAngles(newRotation));
		m_localRotationCache = newRotation;
		m_dirtyRotation = false;
		markDirty();
	}
	else
	{
		// World-space rotation: apply to world rotation, then convert back to local
		glm::quat currentWorldRot = getRotation();
		glm::quat newWorldRot = deltaRotation * currentWorldRot;

		if (m_parent)
		{
			glm::quat parentRot = m_parent->getRotation();
			glm::quat newLocalRot = glm::inverse(parentRot) * newWorldRot;

			// Convert to Euler and store
			m_localEulerAngles = glm::degrees(glm::eulerAngles(newLocalRot));
			m_localRotationCache = newLocalRot;
			m_dirtyRotation = false;
			markDirty();
		}
		else
		{
			m_localEulerAngles = glm::degrees(glm::eulerAngles(newWorldRot));
			m_localRotationCache = newWorldRot;
			m_dirtyRotation = false;
			markDirty();
		}
	}
}

void Transform::lookAt(const glm::vec3 &target, const glm::vec3 &worldUp)
{
	glm::vec3 worldPos = getPosition();
	glm::vec3 forward = worldPos - target; // Note: looking towards -Z, so forward is from target to position
	float len = glm::length(forward);
	if (len < 1e-5f)
		return;
	forward /= len;

	glm::vec3 right = glm::cross(worldUp, forward);
	if (glm::length2(right) < 1e-6f)
		right = glm::normalize(glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), forward));
	else
		right = glm::normalize(right);

	glm::vec3 up = glm::cross(forward, right);

	// Create rotation matrix with -forward in +Z column (since forward() expects -Z)
	glm::mat3 rotMatrix(right, up, forward);
	glm::quat worldRotation = glm::quat_cast(rotMatrix);

	if (m_parent)
	{
		glm::quat localRot = glm::inverse(m_parent->getRotation()) * worldRotation;
		m_localEulerAngles = glm::degrees(glm::eulerAngles(localRot));
		m_localRotationCache = localRot;
		m_dirtyRotation = false;
	}
	else
	{
		m_localEulerAngles = glm::degrees(glm::eulerAngles(worldRotation));
		m_localRotationCache = worldRotation;
		m_dirtyRotation = false;
	}

	markDirty();
}

void Transform::setParentInternal(Transform *parent, bool keepWorld)
{
	if (m_parent == parent)
		return;

	glm::mat4 world = getWorldMatrix();
	m_parent = parent;

	if (keepWorld)
	{
		glm::mat4 invParent = m_parent ? glm::inverse(m_parent->getWorldMatrix()) : glm::mat4(1.0f);
		glm::mat4 local = invParent * world;

		m_localPosition = glm::vec3(local[3]);

		// Extract rotation and convert to Euler
		glm::quat localRot = glm::quat_cast(local);
		m_localEulerAngles = glm::degrees(glm::eulerAngles(localRot));
		m_localRotationCache = localRot;
		m_dirtyRotation = false;

		// Extract scale
		m_localScale = glm::vec3(
			glm::length(glm::vec3(local[0])),
			glm::length(glm::vec3(local[1])),
			glm::length(glm::vec3(local[2]))
		);
	}
	markDirty();
}

Transform *Transform::getParent() const
{
	return m_parent;
}

void Transform::markDirty()
{
	m_dirtyLocal = true;
	m_dirtyWorld = true;
	incrementVersion();
	// Note: Children propagation is handled by SpatialNode hierarchy
}

} // namespace engine::scene