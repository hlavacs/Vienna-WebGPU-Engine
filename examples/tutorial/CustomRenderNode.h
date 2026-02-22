#pragma once

#include "engine/NodeSystem.h"
#include "engine/rendering/BindGroupDataProvider.h"
#include "engine/rendering/BindGroupEnums.h"

namespace demo
{
class CustomRenderNode : public engine::scene::nodes::ModelRenderNode
{
  public:
	/**
	 * @brief Custom render node that demonstrates how to provide custom uniform data to shaders.
	 * Must match the expected layout in the shader's bind group (e.g., "TileUniforms" struct in shader).
	 */
	struct TileUniforms
	{
		glm::vec2 tileOffset = glm::vec2(0.0f); //< Offset for texture tiling, can be used for animation or variation
		glm::vec2 tileSize = glm::vec2(1.0f);	//< Size of the texture tile, can be used for scaling or repetition
	};

	CustomRenderNode(
		const engine::rendering::Model::Ptr model,
		uint32_t layer = 0
	) : engine::scene::nodes::ModelRenderNode(model, layer)
	{
	}

	virtual void preRender(std::vector<engine::rendering::BindGroupDataProvider> &outProviders)
	{
		// Tutorial 02 - Step 7: Implement preRender() method
	}

	TileUniforms tileUniforms;
};
} // namespace demo