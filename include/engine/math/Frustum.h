#pragma once

#include <array>
#include <vector>

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

	static Frustum fromViewProjection(const glm::mat4 &viewProj)
	{
		return extractFromMatrix(viewProj);
	}

	static Frustum perspective(
		const glm::vec3 &pos,
		const glm::vec3 &dir,
		float fovDegrees,
		float aspectRatio,
		float nearPlaneDist,
		float farPlaneDist
	)
	{
		glm::vec3 up = computeUpVector(dir);
		glm::mat4 view = glm::lookAt(pos, pos + dir, up);
		glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearPlaneDist, farPlaneDist);
		return extractFromMatrix(proj * view);
	}

	static Frustum orthographic(
		const glm::vec3 &center,
		const glm::vec3 &dir,
		float halfWidth,
		float halfHeight,
		float nearPlaneDist,
		float farPlaneDist
	)
	{
		glm::vec3 up = computeUpVector(dir);
		glm::mat4 view = glm::lookAt(center - dir * farPlaneDist, center, up);
		glm::mat4 proj = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlaneDist, farPlaneDist);
		return extractFromMatrix(proj * view);
	}

	static Frustum fromAABB(const glm::vec3 &center, float radius)
	{
		Frustum f{};
		glm::vec3 min = center - glm::vec3(radius);
		glm::vec3 max = center + glm::vec3(radius);

		f.leftPlane = {glm::vec3(1, 0, 0), -min.x};
		f.rightPlane = {glm::vec3(-1, 0, 0), max.x};
		f.bottomPlane = {glm::vec3(0, 1, 0), -min.y};
		f.topPlane = {glm::vec3(0, -1, 0), max.y};
		f.nearPlane = {glm::vec3(0, 0, 1), -min.z};
		f.farPlane = {glm::vec3(0, 0, -1), max.z};

		f.corners[0] = {min.x, min.y, min.z};
		f.corners[1] = {max.x, min.y, min.z};
		f.corners[2] = {max.x, max.y, min.z};
		f.corners[3] = {min.x, max.y, min.z};
		f.corners[4] = {min.x, min.y, max.z};
		f.corners[5] = {max.x, min.y, max.z};
		f.corners[6] = {max.x, max.y, max.z};
		f.corners[7] = {min.x, max.y, max.z};

		return f;
	}

	/**
	 * @brief Compute cascade data for CSM
	 * @param cameraFrustum Camera frustum in world space
	 * @param cameraView Camera view matrix (world to view)
	 * @param lightView Light view matrix (world to light view)
	 * @param cameraNear Camera near plane distance
	 * @param cameraFar Camera far plane distance
	 * @param cascadeCount Number of cascades
	 * @param lambda Split lambda (0=uniform, 1=logarithmic)
	 */
	struct CascadeData
	{
		glm::mat4 viewProj;
		float near;
		float far;
		float cascadeSplit;
	};

	static std::vector<CascadeData> computeCascades(
		const Frustum &cameraFrustum,
		const glm::mat4 &cameraView,
		const glm::mat4 &lightView,
		float cameraNear,
		float cameraFar,
		float lightRange,
		uint32_t cascadeCount,
		float lambda
	)
	{
		std::vector<CascadeData> result;
		result.reserve(cascadeCount);

		// Compute split distances
		std::vector<float> splits(cascadeCount + 1);
		splits[0] = cameraNear;
		splits[cascadeCount] = cameraFar;
		float range = cameraFar - cameraNear;
		float ratio = cameraFar / cameraNear;
		for (uint32_t i = 1; i < cascadeCount; ++i)
		{
			float p = static_cast<float>(i) / static_cast<float>(cascadeCount);
			float uniformSplit = cameraNear + range * p;
			float logSplit = cameraNear * std::pow(ratio, p);
			splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
		}

		// Get camera frustum corners in world space
		auto cameraCorners = cameraFrustum.getCorners();

		// Compute view space distances for each corner
		std::array<float, 8> cornerDists;
		for (int i = 0; i < 8; i++)
		{
			glm::vec3 vs = glm::vec3(cameraView * glm::vec4(cameraCorners[i], 1.f));
			cornerDists[i] = -vs.z; // positive distance from camera
		}

		// Build each cascade
		for (uint32_t c = 0; c < cascadeCount; c++)
		{
			CascadeData data;
			float cascadeNear = splits[c];
			float cascadeFar = splits[c + 1];

			// Interpolate corners between near and far splits (world space)
			std::array<glm::vec3, 8> cascadeCorners;
			for (int i = 0; i < 4; i++)
			{
				float d0 = cornerDists[i];
				float d1 = cornerDists[i + 4];

				float t_near = (cascadeNear - d0) / (d1 - d0);
				t_near = glm::clamp(t_near, 0.f, 1.f);
				cascadeCorners[i] = glm::mix(cameraCorners[i], cameraCorners[i + 4], t_near);

				float t_far = (cascadeFar - d0) / (d1 - d0);
				t_far = glm::clamp(t_far, 0.f, 1.f);
				cascadeCorners[i + 4] = glm::mix(cameraCorners[i], cameraCorners[i + 4], t_far);
			}

			// Compute AABB of cascade corners in light space
			glm::vec3 minLS(FLT_MAX), maxLS(-FLT_MAX);
			for (auto &p : cascadeCorners)
			{
				glm::vec3 ls = glm::vec3(lightView * glm::vec4(p, 1.f));
				minLS = glm::min(minLS, ls);
				maxLS = glm::max(maxLS, ls);
			}

			// Create ortho projection from AABB
			glm::mat4 proj = glm::ortho(minLS.x, maxLS.x, minLS.y, maxLS.y, -lightRange, lightRange * 2.0f);
			data.viewProj = proj * lightView;

			data.near = cascadeNear;
			data.far = cascadeFar;
			data.cascadeSplit = cascadeFar;

			result.push_back(data);
		}

		return result;
	}

	[[nodiscard]] std::array<const Plane *, 6> asArray() const
	{
		return {&leftPlane, &rightPlane, &bottomPlane, &topPlane, &nearPlane, &farPlane};
	}

	[[nodiscard]] const std::array<glm::vec3, 8> &getCorners() const
	{
		return corners;
	}

	[[nodiscard]] glm::vec3 getCenter() const
	{
		glm::vec3 minCorner = corners[0];
		glm::vec3 maxCorner = corners[6];
		return 0.5f * (minCorner + maxCorner);
	}

	[[nodiscard]] const Plane &getLeftPlane() const { return leftPlane; }
	[[nodiscard]] const Plane &getRightPlane() const { return rightPlane; }
	[[nodiscard]] const Plane &getBottomPlane() const { return bottomPlane; }
	[[nodiscard]] const Plane &getTopPlane() const { return topPlane; }
	[[nodiscard]] const Plane &getNearPlane() const { return nearPlane; }
	[[nodiscard]] const Plane &getFarPlane() const { return farPlane; }

  private:
	Plane leftPlane;
	Plane rightPlane;
	Plane bottomPlane;
	Plane topPlane;
	Plane nearPlane;
	Plane farPlane;
	std::array<glm::vec3, 8> corners;

	void normalizeAll()
	{
		for (auto *plane : asArray())
		{
			plane->normalize();
		}
	}

	std::array<Plane *, 6> asArray()
	{
		return {&leftPlane, &rightPlane, &bottomPlane, &topPlane, &nearPlane, &farPlane};
	}

	static Frustum extractFromMatrix(const glm::mat4 &clip)
	{
		Frustum f{};

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
			glm::vec3(clip[0][2], clip[1][2], clip[2][2]),
			clip[3][2]
		};
		f.farPlane = {
			glm::vec3(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2]),
			clip[3][3] - clip[3][2]
		};

		f.normalizeAll();

		glm::mat4 inv = glm::inverse(clip);

		int i = 0;
		for (int z = 0; z <= 1; ++z)
			for (int y = 0; y <= 1; ++y)
				for (int x = 0; x <= 1; ++x)
				{
					float nx = x ? 1.f : -1.f;
					float ny = y ? 1.f : -1.f;
					float nz = float(z);

					glm::vec4 ndc(nx, ny, nz, 1.f);
					glm::vec4 p = inv * ndc;
					p /= p.w;

					f.corners[i++] = glm::vec3(p);
				}

		return f;
	}

	static glm::vec3 computeUpVector(const glm::vec3 &dir)
	{
		return glm::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f
				   ? glm::vec3(1, 0, 0)
				   : glm::vec3(0, 1, 0);
	}
};

} // namespace engine::math