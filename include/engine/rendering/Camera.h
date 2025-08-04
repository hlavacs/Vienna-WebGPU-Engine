#pragma once
#include <glm/glm.hpp>

namespace engine::rendering
{

class Camera
{
  public:
	Camera();
	void setOrbit(float azimuth, float elevation, float distance);
	void setAspect(float aspect);
	void setFov(float fovDegrees);
	void setNearFar(float near, float far);

	void orbit(float deltaAzimuth, float deltaElevation);
	void zoom(float delta);

	const glm::mat4 &getViewMatrix() const;
	const glm::mat4 &getProjectionMatrix() const;
	glm::vec3 getPosition() const;

	void update();

  private:
	float m_azimuth, m_elevation, m_distance;
	float m_fov, m_aspect, m_near, m_far;
	glm::mat4 m_view, m_proj;
	glm::vec3 m_target;
};

} // namespace engine::rendering
