#pragma once

#include "engine/NodeSystem.h"
#include "engine/rendering/BindGroupDataProvider.h"
#include "engine/rendering/BindGroupEnums.h"

namespace demo
{
class CustomRenderNode : public engine::scene::nodes::ModelRenderNode
{
  public:
	struct TileUniforms
	{
		glm::vec2 tileOffset;
		glm::vec2 tileSize;
	};

	CustomRenderNode(
		const engine::rendering::Model::Ptr model,
		uint32_t layer = 0
	) : engine::scene::nodes::ModelRenderNode(model, layer)
	{
	}

	virtual void preRender(std::vector<engine::rendering::BindGroupDataProvider> &outProviders)
	{
		// // Tutorial 2 - Step 7: Implement CustomRenderNode

	}

	TileUniforms tileUniforms;
};
} // namespace demo