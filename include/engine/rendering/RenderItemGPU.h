#pragma once

#include <glm/glm.hpp>
#include <memory>

#include "engine/rendering/Submesh.h"

// Forward declarations
namespace engine::rendering::webgpu
{
class WebGPUModel;
class WebGPUMesh;
class WebGPUMaterial;
class WebGPUBindGroup;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

/**
 * @brief GPU-side render item prepared for actual rendering.
 * Contains GPU resources created once and reused across multiple passes.
 *
 * This structure represents a fully prepared render item with all GPU resources
 * allocated and ready to be used in rendering passes. It's created on-demand
 * from CPU-side RenderItem data and cached in FrameCache.
 */
struct RenderItemGPU
{
	std::shared_ptr<webgpu::WebGPUModel> gpuModel;			  ///< GPU model resource
	webgpu::WebGPUMesh *gpuMesh;							  ///< Raw pointer to GPU mesh (owned by gpuModel)
	std::shared_ptr<webgpu::WebGPUMaterial> gpuMaterial;	  ///< GPU material with textures and properties
	std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup; ///< Per-object uniform bind group
	engine::rendering::Submesh submesh;						  ///< Submesh data (indices, material)
	glm::mat4 worldTransform;								  ///< World transformation matrix
	uint32_t renderLayer;									  ///< Render layer for sorting
	uint64_t objectID;										  ///< Unique object identifier
};

} // namespace engine::rendering
