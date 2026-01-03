#pragma once

#include <array>
#include <glm/glm.hpp>

namespace engine::math
{

/**
 * @brief Represents a plane in the view frustum.
 */
struct Frustum
{
	struct Plane
	{
		glm::vec3 normal;
		float d;
	};
	Plane leftPlane;
	Plane rightPlane;
	Plane bottomPlane;
	Plane topPlane;
	Plane nearPlane;
	Plane farPlane;

	/**
	 * @brief Returns the frustum planes as an array.
	 * @return Span of 6 planes.
	 */
	std::array<const Plane *, 6> asArray() const
	{
		return std::array<const Plane *, 6>(
			{&leftPlane, &rightPlane, &bottomPlane, &topPlane, &nearPlane, &farPlane}
		);
	}

	/**
	 * @brief Returns the frustum planes as an array.
	 * @return Span of 6 planes.
	 */
	std::array<Plane *, 6> asArray()
	{
		return std::array<Plane *, 6>(
			{&leftPlane, &rightPlane, &bottomPlane, &topPlane, &nearPlane, &farPlane}
		);
	}
};
} // namespace engine::math