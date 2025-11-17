#include "engine/rendering/RenderCollector.h"

namespace engine::rendering {

void RenderCollector::addModel(const engine::core::Handle<engine::rendering::Model> &model, const glm::mat4 &transform) {
    m_renderItems.push_back(RenderItem{model, transform});
}

void RenderCollector::addLight(const LightStruct &light) {
    m_lights.push_back(light);
}

void RenderCollector::clear() {
    m_renderItems.clear();
    m_lights.clear();
}

const std::vector<RenderItem> &RenderCollector::getRenderItems() const {
    return m_renderItems;
}

const std::vector<LightStruct> &RenderCollector::getLights() const {
    return m_lights;
}

} // namespace engine::rendering
