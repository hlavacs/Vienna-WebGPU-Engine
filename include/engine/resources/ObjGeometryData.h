#pragma once

#include "engine/io/tiny_obj_loader.h"
#include "engine/math/AABB.h"
#include "engine/rendering/Vertex.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::resources
{

/**
 * @struct ObjGeometryData
 * @brief Holds parsed geometry and material data from an OBJ file.
 */
struct ObjGeometryData
{
	std::string filePath;
	std::string name;
	std::vector<engine::rendering::Vertex> vertices;
	std::vector<uint32_t> indices;
	engine::math::AABB boundingBox;

	struct MaterialRange
	{
		int materialId = -1;	  // Index in tinyobj::material_t array
		uint32_t indexOffset = 0; // Start in the global indices array
		uint32_t indexCount = 0;  // Number of indices for this material
	};

	std::vector<MaterialRange> materialRanges;
	std::vector<tinyobj::material_t> materials;
};

} // namespace engine::resources
