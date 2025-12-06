#include "engine/rendering/RenderCollector.h"

#include <algorithm>

namespace engine::rendering {

void RenderCollector::addModel(
    const engine::core::Handle<engine::rendering::Model> &model,
    const glm::mat4 &transform,
    uint32_t layer)
{
    m_renderItems.push_back(RenderItem{model, transform, layer});
}

void RenderCollector::addLight(const LightStruct &light)
{
    m_lights.push_back(light);
}

void RenderCollector::sort()
{
    // Sort by layer first, then by model handle for batching
    std::sort(m_renderItems.begin(), m_renderItems.end(),
        [](const RenderItem &a, const RenderItem &b) {
            if (a.renderLayer != b.renderLayer)
                return a.renderLayer < b.renderLayer;
            return a.model.id() < b.model.id();
        });
}

void RenderCollector::clear()
{
    m_renderItems.clear();
    m_lights.clear();
}

const std::vector<RenderItem> &RenderCollector::getRenderItems() const
{
    return m_renderItems;
}

const std::vector<LightStruct> &RenderCollector::getLights() const
{
    return m_lights;
}

} // namespace engine::rendering
