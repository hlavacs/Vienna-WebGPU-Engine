#pragma once
/**
 * @file WebGPUModel.h
 * @brief GPU-side model: holds references to WebGPUMesh and WebGPUMaterial.
 */
#include <memory>
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu
{

	/**
	 * @class WebGPUModel
	 * @brief Combines WebGPUMesh and WebGPUMaterial for rendering.
	 */
	class WebGPUModel
	{
	public:
		/**
		 * @brief Construct from GPU resources.
		 */
		WebGPUModel(std::shared_ptr<WebGPUMesh> mesh, std::shared_ptr<WebGPUMaterial> material);
		/** @brief Get mesh. */
		std::shared_ptr<WebGPUMesh> getMesh() const;
		/** @brief Get material. */
		std::shared_ptr<WebGPUMaterial> getMaterial() const;

	private:
		std::shared_ptr<WebGPUMesh> m_mesh;			///< Mesh resource.
		std::shared_ptr<WebGPUMaterial> m_material; ///< Material resource.
	};

} // namespace engine::rendering::webgpu
