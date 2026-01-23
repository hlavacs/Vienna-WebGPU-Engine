#pragma once

#include <algorithm>
#include <array>
#include <glm/glm.hpp>

namespace engine::math
{
/**
 * @brief Axis-aligned bounding box
 */
struct AABB
{
	glm::vec3 min = glm::vec3(0.0f); // minimum corner
	glm::vec3 max = glm::vec3(0.0f); // maximum corner

	AABB() = default;

	AABB(const glm::vec3 &minCorner, const glm::vec3 &maxCorner) : min(minCorner), max(maxCorner) {}

	// Expand the box to include a point
	void expandToFit(const glm::vec3 &point)
	{
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	// Expand the box to include another box
	void expandToFit(const AABB &other)
	{
		min = glm::min(min, other.min);
		max = glm::max(max, other.max);
	}

	/**
	 * @brief Get a specific corner of the AABB.
	 * @param index Corner index (0-7).
	 * @return The corner position.
	 * @note Corner index mapping:
	 * 0: (min.x, min.y, min.z)
	 * 1: (max.x, min.y, min.z)
	 * 2: (min.x, max.y, min.z)
	 * 3: (max.x, max.y, min.z)
	 * 4: (min.x, min.y, max.z)
	 * 5: (max.x, min.y, max.z)
	 * 6: (min.x, max.y, max.z)
	 * 7: (max.x, max.y, max.z)
	 */
	[[nodiscard]] glm::vec3 getCorner(int index) const
	{
		return {
			(index & 1) ? max.x : min.x,
			(index & 2) ? max.y : min.y,
			(index & 4) ? max.z : min.z
		};
	}

	/**
	 * @brief Get all corners of the AABB.
	 * @return An array of 8 corner positions.
	 */
	[[nodiscard]] std::array<glm::vec3, 8> getCorners() const
	{
		return {
			getCorner(0),
			getCorner(1),
			getCorner(2),
			getCorner(3),
			getCorner(4),
			getCorner(5),
			getCorner(6),
			getCorner(7)
		};
	}

	/**
	 * @brief Calculate the volume of the AABB.
	 * @return The volume.
	 */
	[[nodiscard]] float getVolume() const
	{
		glm::vec3 size = max - min;
		return size.x * size.y * size.z;
	}

	// Get center
	[[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }

	// Get size (width, height, depth)
	[[nodiscard]] glm::vec3 size() const { return max - min; }

	// Get extent (half-size)
	[[nodiscard]] glm::vec3 extent() const { return (max - min) * 0.5f; }

	// Transform the AABB by a matrix
	[[nodiscard]] AABB transformed(const glm::mat4 &matrix) const
	{
		auto corners = getCorners();

		AABB result;
		result.min = result.max = glm::vec3(matrix * glm::vec4(corners[0], 1.0f));
		for (int i = 1; i < 8; ++i)
		{
			glm::vec3 transformed = glm::vec3(matrix * glm::vec4(corners[i], 1.0f));
			result.expandToFit(transformed);
		}
		return result;
	}
};

} // namespace engine::math