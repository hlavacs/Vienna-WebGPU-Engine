#pragma once
#include "engine/core/Handle.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Model.h"
#include <glm/glm.hpp>
#include <vector>

namespace engine::rendering
{

struct RenderItem
{
	engine::core::Handle<engine::rendering::Model> model;
	glm::mat4 transform;
};

class RenderCollector
{
  public:
	void addModel(const engine::core::Handle<engine::rendering::Model> &model, const glm::mat4 &transform);
	void addLight(const LightStruct &light);
	void clear();

	const std::vector<RenderItem> &getRenderItems() const;
	const std::vector<LightStruct> &getLights() const;

  private:
	std::vector<engine::rendering::RenderItem> m_renderItems;
	std::vector<engine::rendering::LightStruct> m_lights;
};

} // namespace engine::rendering
