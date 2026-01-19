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
	_localPosition = position;
	markDirty();
}

void Transform::setLocalRotation(const glm::quat &rotation)
{
	_localRotation = rotation;
	markDirty();
}

void Transform::setLocalEulerAngles(const glm::vec3 &euler)
{
	_localRotation = glm::quat(glm::radians(euler));
	markDirty();
}

void Transform::setLocalScale(const glm::vec3 &scale)
{
	_localScale = scale;
	markDirty();
}

const glm::vec3 &Transform::getLocalPosition() const { return _localPosition; }
const glm::quat &Transform::getLocalRotation() const { return _localRotation; }
glm::vec3 Transform::getLocalEulerAngles() const { return glm::degrees(glm::eulerAngles(_localRotation)); }
const glm::vec3 &Transform::getLocalScale() const { return _localScale; }

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
	return glm::vec3(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
}

glm::vec3 Transform::getEulerAngles() const
{
	return glm::degrees(glm::eulerAngles(getRotation()));
}

void Transform::setWorldPosition(const glm::vec3 &position)
{
	if (_parent)
	{
		// Convert world position to local space
		glm::mat4 invParentWorld = glm::inverse(_parent->getWorldMatrix());
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
	if (_parent)
	{
		// Convert world rotation to local space
		glm::quat parentRotation = _parent->getRotation();
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
	if (_parent)
	{
		// Convert world scale to local space by dividing by parent scale
		glm::vec3 parentScale = _parent->getScale();
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
	glm::mat4 T = glm::translate(glm::mat4(1.0f), _localPosition);
	glm::mat4 R = glm::toMat4(_localRotation);
	glm::mat4 S = glm::scale(glm::mat4(1.0f), _localScale);
	return T * R * S;
}

glm::mat4 Transform::getWorldMatrix() const
{
	if (_dirty)
		updateWorldMatrix();
	return _worldMatrixCache;
}

void Transform::updateWorldMatrix() const
{
	if (_parent)
	{
		_worldMatrixCache = _parent->getWorldMatrix() * getLocalMatrix();
	}
	else
	{
		_worldMatrixCache = getLocalMatrix();
	}
	_dirty = false;
	// Note: Children propagation is handled by SpatialNode hierarchy
}

glm::vec3 Transform::forward() const
{
	return glm::normalize(-glm::vec3(getWorldMatrix()[2]));
}

glm::vec3 Transform::right() const
{
	return glm::normalize(glm::vec3(getWorldMatrix()[0]));
}

glm::vec3 Transform::up() const
{
	return glm::normalize(glm::vec3(getWorldMatrix()[1]));
}

void Transform::translate(const glm::vec3 &delta, bool local)
{
	if (local)
	{
		setLocalPosition(_localPosition + delta);
	}
	else
	{
		setLocalPosition(getLocalPosition() + glm::vec3(getWorldMatrix() * glm::vec4(delta, 0.0f)));
	}
}

void Transform::rotate(const glm::vec3 &eulerDegrees, bool local)
{
	glm::quat rot = glm::quat(glm::radians(eulerDegrees));
	if (local)
	{
		setLocalRotation(_localRotation * rot);
	}
	else
	{
		setLocalRotation(rot * _localRotation);
	}
}

void Transform::lookAt(const glm::vec3 &target, const glm::vec3 &worldUp)
{
	glm::vec3 worldPos = getPosition();
	glm::vec3 forward = target - worldPos;
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

	glm::mat3 rotMatrix(right, up, forward);
	glm::quat worldRotation = glm::quat_cast(rotMatrix);

	if (_parent)
		_localRotation = glm::inverse(_parent->getRotation()) * worldRotation;
	else
		_localRotation = worldRotation;

	markDirty();
}

void Transform::setParentInternal(Ptr parent, bool keepWorld)
{
	if (_parent == parent)
		return;

	glm::mat4 world = getWorldMatrix();
	_parent = parent;

	if (keepWorld)
	{
		glm::mat4 invParent = _parent ? glm::inverse(_parent->getWorldMatrix()) : glm::mat4(1.0f);
		glm::mat4 local = invParent * world;
		_localPosition = glm::vec3(local[3]);
		_localRotation = glm::quat_cast(local);
		_localScale = glm::vec3(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
	}
	markDirty();
}

Transform::Ptr Transform::getParent() const { return _parent; }

void Transform::markDirty()
{
	_dirty = true;
	// Note: Children propagation is handled by SpatialNode hierarchy
}

} // namespace engine::scene
