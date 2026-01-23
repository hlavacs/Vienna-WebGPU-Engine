#pragma once

#include "engine/rendering/Material.h"
#include <cstdint>

namespace engine::rendering
{

struct Submesh
{
	uint32_t indexOffset = 0;
	uint32_t indexCount = 0;
	Material::Handle material;

	[[nodiscard]] bool valid() const
	{
		return indexCount > 0 && material.valid();
	}
};

} // namespace engine::rendering
