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

	static Frustum fromViewProjection(const glm::mat4 &viewProj)
	{
		Frustum f;
		const glm::mat4 &clip = viewProj;

		f.leftPlane = {glm::vec3(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0]), clip[3][3] + clip[3][0]};
		f.rightPlane = {glm::vec3(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0]), clip[3][3] - clip[3][0]};
		f.bottomPlane = {glm::vec3(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1]), clip[3][3] + clip[3][1]};
		f.topPlane = {glm::vec3(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1]), clip[3][3] - clip[3][1]};
		f.nearPlane = {glm::vec3(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2]), clip[3][3] + clip[3][2]};
		f.farPlane = {glm::vec3(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2]), clip[3][3] - clip[3][2]};

		return f;
	}

	/**
	 * @brief Creates a perspective frustum (for spot lights)
	 * @param pos Light position
	 * @param dir Light direction (normalized)
	 * @param fovDegrees Full cone angle in degrees
	 * @param aspectRatio Aspect ratio (width/height)
	 * @param nearPlane Distance to near plane
	 * @param farPlane Distance to far plane
	 */
	static Frustum perspective(const glm::vec3 &pos, const glm::vec3 &dir, float fovDegrees, float aspectRatio, float nearPlaneDist, float farPlaneDist)
	{
		Frustum f;

		// Build a lookAt matrix from light position
		glm::vec3 up = glm::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
		glm::mat4 view = glm::lookAt(pos, pos + dir, up);
		glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearPlaneDist, farPlaneDist); // square aspect ratio

		glm::mat4 clip = proj * view;

		// Extract planes from clip matrix
		// Left
		f.leftPlane.normal.x = clip[0][3] + clip[0][0];
		f.leftPlane.normal.y = clip[1][3] + clip[1][0];
		f.leftPlane.normal.z = clip[2][3] + clip[2][0];
		f.leftPlane.d = clip[3][3] + clip[3][0];

		// Right
		f.rightPlane.normal.x = clip[0][3] - clip[0][0];
		f.rightPlane.normal.y = clip[1][3] - clip[1][0];
		f.rightPlane.normal.z = clip[2][3] - clip[2][0];
		f.rightPlane.d = clip[3][3] - clip[3][0];

		// Bottom
		f.bottomPlane.normal.x = clip[0][3] + clip[0][1];
		f.bottomPlane.normal.y = clip[1][3] + clip[1][1];
		f.bottomPlane.normal.z = clip[2][3] + clip[2][1];
		f.bottomPlane.d = clip[3][3] + clip[3][1];

		// Top
		f.topPlane.normal.x = clip[0][3] - clip[0][1];
		f.topPlane.normal.y = clip[1][3] - clip[1][1];
		f.topPlane.normal.z = clip[2][3] - clip[2][1];
		f.topPlane.d = clip[3][3] - clip[3][1];

		// Near
		f.nearPlane.normal.x = clip[0][3] + clip[0][2];
		f.nearPlane.normal.y = clip[1][3] + clip[1][2];
		f.nearPlane.normal.z = clip[2][3] + clip[2][2];
		f.nearPlane.d = clip[3][3] + clip[3][2];

		// Far
		f.farPlane.normal.x = clip[0][3] - clip[0][2];
		f.farPlane.normal.y = clip[1][3] - clip[1][2];
		f.farPlane.normal.z = clip[2][3] - clip[2][2];
		f.farPlane.d = clip[3][3] - clip[3][2];

		return f;
	}

	/**
	 * @brief Creates an orthographic frustum (for directional lights)
	 * @param center Center point to focus on
	 * @param dir Light direction
	 * @param halfWidth Half-width of ortho box
	 * @param halfHeight Half-height
	 * @param nearPlane Near distance
	 * @param farPlane Far distance
	 */
	static Frustum orthographic(const glm::vec3 &center, const glm::vec3 &dir, float halfWidth, float halfHeight, float nearPlaneDist, float farPlaneDist)
	{
		Frustum f;

		glm::vec3 up = glm::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
		glm::mat4 view = glm::lookAt(center - dir * farPlaneDist, center, up);
		glm::mat4 proj = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlaneDist, farPlaneDist);

		glm::mat4 clip = proj * view;

		// same plane extraction as above
		auto extractPlane = [&](Plane &plane, int row, int signX, int signY, int signZ)
		{
			plane.normal.x = clip[0][3] + signX * clip[0][row];
			plane.normal.y = clip[1][3] + signX * clip[1][row];
			plane.normal.z = clip[2][3] + signX * clip[2][row];
			plane.d = clip[3][3] + signX * clip[3][row];
		};

		f.leftPlane = {glm::vec3(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0]), clip[3][3] + clip[3][0]};
		f.rightPlane = {glm::vec3(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0]), clip[3][3] - clip[3][0]};
		f.bottomPlane = {glm::vec3(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1]), clip[3][3] + clip[3][1]};
		f.topPlane = {glm::vec3(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1]), clip[3][3] - clip[3][1]};
		f.nearPlane = {glm::vec3(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2]), clip[3][3] + clip[3][2]};
		f.farPlane = {glm::vec3(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2]), clip[3][3] - clip[3][2]};

		return f;
	}

	/**
	 * @brief Creates a bounding sphere frustum approximation for point lights
	 * @param center Position of point light
	 * @param radius Range of light
	 */
	static Frustum fromSphere(const glm::vec3 &center, float radius)
	{
		// This is a loose approximation using AABB around the sphere
		Frustum f;
		f.leftPlane.normal = glm::vec3(1, 0, 0);
		f.leftPlane.d = -(center.x - radius);
		f.rightPlane.normal = glm::vec3(-1, 0, 0);
		f.rightPlane.d = center.x + radius;
		f.bottomPlane.normal = glm::vec3(0, 1, 0);
		f.bottomPlane.d = -(center.y - radius);
		f.topPlane.normal = glm::vec3(0, -1, 0);
		f.topPlane.d = center.y + radius;
		f.nearPlane.normal = glm::vec3(0, 0, 1);
		f.nearPlane.d = -(center.z - radius);
		f.farPlane.normal = glm::vec3(0, 0, -1);
		f.farPlane.d = center.z + radius;
		return f;
	}
};

} // namespace engine::math
