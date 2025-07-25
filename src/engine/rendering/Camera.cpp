#include "engine/rendering/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace engine {
namespace rendering {

Camera::Camera()
	: m_azimuth(0), m_elevation(0), m_distance(5),
	  m_fov(45), m_aspect(1.0f), m_near(0.01f), m_far(100.0f),
	  m_target(0.0f, 0.0f, 0.0f)
{
	update();
}

void Camera::setOrbit(float az, float el, float dist) {
	m_azimuth = az; m_elevation = el; m_distance = dist;
	update();
}
void Camera::setAspect(float aspect) { m_aspect = aspect; update(); }
void Camera::setFov(float fovDegrees) { m_fov = fovDegrees; update(); }
void Camera::setNearFar(float near, float far) { m_near = near; m_far = far; update(); }

void Camera::orbit(float daz, float del) {
	m_azimuth += daz;
	m_elevation = std::clamp(m_elevation + del, -glm::half_pi<float>() + 1e-5f, glm::half_pi<float>() - 1e-5f);
	update();
}
void Camera::zoom(float delta) {
	m_distance = std::clamp(m_distance * std::exp(-delta), 0.1f, 100.0f);
	update();
}

const glm::mat4& Camera::getViewMatrix() const { return m_view; }
const glm::mat4& Camera::getProjectionMatrix() const { return m_proj; }
glm::vec3 Camera::getPosition() const {
	float x = m_distance * cos(m_elevation) * cos(m_azimuth);
	float y = m_distance * sin(m_elevation);
	float z = m_distance * cos(m_elevation) * sin(m_azimuth);
	return glm::vec3(x, y, z) + m_target;
}

void Camera::update() {
	glm::vec3 pos = getPosition();
	m_view = glm::lookAt(pos, m_target, glm::vec3(0, 1, 0));
	m_proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
}

} // namespace rendering
} // namespace engine
