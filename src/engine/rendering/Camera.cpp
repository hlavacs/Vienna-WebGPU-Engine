#include "engine/rendering/Camera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace engine::rendering {

Camera::Camera() : m_transform(std::make_shared<engine::scene::Transform>()) {
    calculateMatrices();
}

void Camera::calculateMatrices() {
    // Calculate view matrix from transform
    if (m_transform) {
        glm::vec3 position = m_transform->getPosition();
        glm::vec3 forward = m_transform->forward();
        glm::vec3 up = m_transform->up();
        
        m_view = glm::lookAt(position, position + forward, up);
    } else {
        m_view = glm::mat4(1.0f);
    }
    
    // Calculate projection matrix based on projection type
    if (m_projectionType == ProjectionType::Perspective) {
        m_proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
    } else {
        // Orthographic projection
        float halfHeight = m_orthographicSize;
        float halfWidth = halfHeight * m_aspect;
        m_proj = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_near, m_far);
    }
}

} // namespace engine::rendering
