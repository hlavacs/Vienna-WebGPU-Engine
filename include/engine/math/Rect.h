#pragma once

#include <algorithm>
#include <glm/glm.hpp>

namespace engine::math
{
/**
 * @brief Axis-aligned rectangle defined by min and max points.
 */
struct Rect
{
	Rect(glm::vec2 minPoint = glm::vec2(0.0f), glm::vec2 maxPoint = glm::vec2(0.0f)) : min(minPoint), max(maxPoint) {}
	Rect(glm::vec4 vec) : min(glm::vec2(vec.x, vec.y)), max(glm::vec2(vec.z, vec.w)) {}
	Rect(float minX, float minY, float maxX, float maxY) : min(glm::vec2(minX, minY)), max(glm::vec2(maxX, maxY)) {}

	Rect(const Rect &other) = default;
	Rect &operator=(const Rect &other) = default;

	glm::vec2 min; ///< Minimum corner (bottom-left)
	glm::vec2 max; ///< Maximum corner (top-right)

	/**
	 * @brief Computes the width of the rectangle.
	 * @return Width as float.
	 */
	float width() const { return max.x - min.x; }

	/**
	 * @brief Computes the height of the rectangle.
	 * @return Height as float.
	 */
	float height() const { return max.y - min.y; }

	/**
	 * @brief Computes the area of the rectangle.
	 * @return Area as float.
	 */
	float area() const { return width() * height(); }

	/**
	 * @brief Checks if a point is inside the rectangle.
	 * @param point Point to test.
	 * @return True if point is inside, false otherwise.
	 */
	bool contains(const glm::vec2 &point) const

	{
		return (point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y);
	}

	/**
	 * @brief Expands the rectangle to include another rectangle.
	 * @param other Rectangle to include.
	 */
	void expandToInclude(const Rect &other)
	{
		min.x = std::min(min.x, other.min.x);
		min.y = std::min(min.y, other.min.y);
		max.x = std::max(max.x, other.max.x);
		max.y = std::max(max.y, other.max.y);
	}

	/**
	 * @brief Checks if this rectangle intersects with another rectangle.
	 * @param other Rectangle to test intersection with.
	 * @return True if rectangles intersect, false otherwise.
	 */
	bool intersects(const Rect &other) const
	{
		return (min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y);
	}

	/**
	 * @brief Gets the rectangle as a vec4 (min.x, min.y, max.x, max.y).
	 * @return glm::vec4 representation.
	 */
	glm::vec4 toVec4() const { return glm::vec4(min.x, min.y, max.x, max.y); }
};
} // namespace engine::math