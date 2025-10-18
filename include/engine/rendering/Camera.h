#pragma once
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include <algorithm>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace engine::rendering
{

struct Camera : public engine::core::Identifiable<Camera>,
				public engine::core::Versioned
{
  public:
	using Handle = engine::core::Handle<Camera>;
	using Ptr = std::shared_ptr<Camera>;

	// Constructors
	Camera() = default;

	// Move only
	Camera(Camera &&) noexcept = default;
	Camera &operator=(Camera &&) noexcept = default;

	// No copy
	Camera(const Camera &) = delete;
	Camera &operator=(const Camera &) = delete;

	void setOrbit(float azimuth, float elevation, float distance)
	{
		m_azimuth = azimuth;
		m_elevation = elevation;
		m_distance = distance;
		incrementVersion();
		calculateMatrices();
	}
	
	void setAzimuth(float azimuth)
	{
		m_azimuth = azimuth;
		incrementVersion();
		calculateMatrices();
	}

	void setElevation(float elevation)
	{
		m_elevation = elevation;
		incrementVersion();
		calculateMatrices();
	}

	void setDistance(float distance)
	{
		m_distance = distance;
		incrementVersion();
		calculateMatrices();
	}

	void setFov(float fovDegrees)
	{
		m_fov = fovDegrees;
		incrementVersion();
		calculateMatrices();
	}

	void setAspect(float aspect)
	{
		m_aspect = aspect;
		incrementVersion();
		calculateMatrices();
	}

	void setNear(float near)
	{
		m_near = near;
		incrementVersion();
		calculateMatrices();
	}

	void setFar(float far)
	{
		m_far = far;
		incrementVersion();
		calculateMatrices();
	}

	void setTarget(const glm::vec3 &target)
	{
		m_target = target;
		incrementVersion();
		calculateMatrices();
	}

	void setNearFar(float near, float far)
	{
		m_near = near;
		m_far = far;
		incrementVersion();
		calculateMatrices();
	}

	void orbit(float deltaAzimuth, float deltaElevation)
	{
		m_azimuth += deltaAzimuth;
		m_elevation = std::clamp(m_elevation + deltaElevation, -glm::half_pi<float>() + 1e-5f, glm::half_pi<float>() - 1e-5f);
		incrementVersion();
		calculateMatrices();
	}
	void zoom(float delta)
	{
		m_distance = std::clamp(m_distance * std::exp(-delta), 0.1f, 100.0f);
		incrementVersion();
		calculateMatrices();
	}

	// Getters
	float getAzimuth() const { return m_azimuth; }
	float getElevation() const { return m_elevation; }
	float getDistance() const { return m_distance; }
	float getFov() const { return m_fov; }
	float getAspect() const { return m_aspect; }
	float getNear() const { return m_near; }
	float getFar() const { return m_far; }
	const glm::vec3 &getTarget() const { return m_target; }
	const glm::mat4 &getView() const { return m_view; }
	const glm::mat4 &getProj() const { return m_proj; }
	const glm::mat4 &getViewMatrix() const { return m_view; }
	const glm::mat4 &getProjectionMatrix() const { return m_proj; }
	glm::vec3 getPosition() const
	{
		float x = m_distance * cos(m_elevation) * cos(m_azimuth);
		float y = m_distance * sin(m_elevation);
		float z = m_distance * cos(m_elevation) * sin(m_azimuth);
		return glm::vec3(x, y, z) + m_target;
	}

  private:
	void calculateMatrices()
	{
		glm::vec3 pos = getPosition();
		m_view = glm::lookAt(pos, m_target, glm::vec3(0, 1, 0));
		m_proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
	}

	float m_azimuth, m_elevation, m_distance;
	float m_fov, m_aspect, m_near, m_far;
	glm::mat4 m_view, m_proj;
	glm::vec3 m_target;
};

} // namespace engine::rendering
