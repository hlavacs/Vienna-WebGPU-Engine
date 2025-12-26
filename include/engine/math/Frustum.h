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
	Plane left;
	Plane right;
	Plane bottom;
	Plane top;
	Plane near;
	Plane far;

	/**
	 * @brief Returns the frustum planes as an array.
	 * @return Span of 6 planes.
	 */
	std::array<const Plane *, 6> asArray() const
	{
		return std::array<const Plane *, 6>(
			{&left, &right, &bottom, &top, &near, &far}
		);
	}

	/**
	 * @brief Returns the frustum planes as an array.
	 * @return Span of 6 planes.
	 */
	std::array<Plane *, 6> &asArray()
	{
		return std::array<Plane *, 6>(
			{&left, &right, &bottom, &top, &near, &far}
		);
	}
};
} // namespace engine::math