#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <memory>

namespace engine::core {

// Forward declarations for mesh/material/camera types
class Mesh;
class Material;
class Camera;

struct RenderState {
    // Example: snapshot of render data for a frame
    std::vector<glm::mat4> transforms;
    std::vector<std::shared_ptr<Mesh>> meshes;
    std::vector<std::shared_ptr<Material>> materials;
    std::shared_ptr<Camera> camera;
    // Add more as needed
};

} // namespace engine::core
