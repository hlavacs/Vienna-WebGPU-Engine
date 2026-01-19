#pragma once

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::math
{

struct Frustum
{
	struct Plane
	{
		glm::vec3 normal;
		float d;

		void normalize()
		{
			float length = glm::length(normal);
			if (length > 0.0f)
			{
				normal /= length;
				d /= length;
			}
		}
	};

	Plane leftPlane;
	Plane rightPlane;
	Plane bottomPlane;
	Plane topPlane;
	Plane nearPlane;
	Plane farPlane;

	std::array<const Plane *, 6> asArray() const
	{
		return {&leftPlane, &rightPlane, &bottomPlane, &topPlane, &nearPlane, &farPlane};
	}

	std::array<Plane *, 6> asArray()
	{
		return {&leftPlane, &rightPlane, &bottomPlane, &topPlane, &nearPlane, &farPlane};
	}

	void normalizeAll()
	{
		for (auto *plane : asArray())
		{
			plane->normalize();
		}
	}

private:
	static Frustum extractFromMatrix(const glm::mat4 &clip)
	{
		Frustum f;
		
		// Extract planes using Gribb-Hartmann method
		f.leftPlane = {
			glm::vec3(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0]),
			clip[3][3] + clip[3][0]
		};
		f.rightPlane = {
			glm::vec3(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0]),
			clip[3][3] - clip[3][0]
		};
		f.bottomPlane = {
			glm::vec3(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1]),
			clip[3][3] + clip[3][1]
		};
		f.topPlane = {
			glm::vec3(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1]),
			clip[3][3] - clip[3][1]
		};
		f.nearPlane = {
			glm::vec3(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2]),
			clip[3][3] + clip[3][2]
		};
		f.farPlane = {
			glm::vec3(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2]),
			clip[3][3] - clip[3][2]
		};
		
		f.normalizeAll();
		return f;
	}

	static glm::vec3 computeUpVector(const glm::vec3 &dir)
	{
		return glm::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f 
			? glm::vec3(1, 0, 0) 
			: glm::vec3(0, 1, 0);
	}

public:
	static Frustum fromViewProjection(const glm::mat4 &viewProj)
	{
		return extractFromMatrix(viewProj);
	}

	/**
	 * @brief Creates a perspective frustum (for spot lights)
	 * @param pos Light position
	 * @param dir Light direction (should be normalized)
	 * @param fovDegrees Full cone angle in degrees
	 * @param aspectRatio Aspect ratio (width/height)
	 * @param nearPlaneDist Distance to near plane
	 * @param farPlaneDist Distance to far plane
	 */
	static Frustum perspective(
		const glm::vec3 &pos,
		const glm::vec3 &dir,
		float fovDegrees,
		float aspectRatio,
		float nearPlaneDist,
		float farPlaneDist)
	{
		glm::vec3 up = computeUpVector(dir);
		glm::mat4 view = glm::lookAt(pos, pos + dir, up);
		glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearPlaneDist, farPlaneDist);
		return extractFromMatrix(proj * view);
	}

	/**
	 * @brief Creates an orthographic frustum (for directional lights)
	 * @param center Center point to focus on
	 * @param dir Light direction (should be normalized)
	 * @param halfWidth Half-width of ortho box
	 * @param halfHeight Half-height
	 * @param nearPlaneDist Near distance
	 * @param farPlaneDist Far distance
	 */
	static Frustum orthographic(
		const glm::vec3 &center,
		const glm::vec3 &dir,
		float halfWidth,
		float halfHeight,
		float nearPlaneDist,
		float farPlaneDist)
	{
		glm::vec3 up = computeUpVector(dir);
		glm::mat4 view = glm::lookAt(center - dir * farPlaneDist, center, up);
		glm::mat4 proj = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlaneDist, farPlaneDist);
		return extractFromMatrix(proj * view);
	}

	/**
	 * @brief Creates a bounding sphere frustum approximation for point lights
	 * @param center Position of point light
	 * @param radius Range of light
	 */
	static Frustum fromSphere(const glm::vec3 &center, float radius)
	{
		// Axis-aligned bounding box around sphere (already normalized)
		Frustum f;
		f.leftPlane = {glm::vec3(1, 0, 0), -(center.x - radius)};
		f.rightPlane = {glm::vec3(-1, 0, 0), center.x + radius};
		f.bottomPlane = {glm::vec3(0, 1, 0), -(center.y - radius)};
		f.topPlane = {glm::vec3(0, -1, 0), center.y + radius};
		f.nearPlane = {glm::vec3(0, 0, 1), -(center.z - radius)};
		f.farPlane = {glm::vec3(0, 0, -1), center.z + radius};
		return f;
	}
};

} // namespace engine::math
