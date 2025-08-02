#pragma once
#include <memory>
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/Mesh.h"

namespace engine::rendering::webgpu
{
	class WebGPUContext;

	class WebGPUMeshFactory : public BaseWebGPUFactory<engine::rendering::Mesh, WebGPUMesh>
	{
	public:
		using BaseWebGPUFactory::BaseWebGPUFactory;

		explicit WebGPUMeshFactory(WebGPUContext &context);

		/**
		 * @brief Create a WebGPUMesh from a Mesh handle.
		 * @param handle Handle to the Mesh.
		 * @return Shared pointer to WebGPUMesh.
		 */
		std::shared_ptr<WebGPUMesh> createFromHandle(
			const engine::rendering::Mesh::Handle &handle) override;
	};
} // namespace engine::rendering::webgpu
