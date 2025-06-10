#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/Model.h"

namespace engine::rendering::webgpu {

WebGPUModelFactory::WebGPUModelFactory(WebGPUContext& context)
	: BaseWebGPUFactory(context) {}

std::shared_ptr<WebGPUModel> WebGPUModelFactory::createFrom(const engine::rendering::Model& model) {
	// Use mesh and material factories to create GPU resources
	auto mesh = m_context.meshFactory().createFrom(model.getMesh());
	auto materialHandle = model.getMaterial();
	auto material = materialHandle.get().value_or(nullptr);
	std::shared_ptr<WebGPUMaterial> webgpuMaterial = nullptr;
	if (material) {
		webgpuMaterial = m_context.materialFactory().createFrom(*material);
	}
	return std::make_shared<WebGPUModel>(mesh, webgpuMaterial);
}

} // namespace engine::rendering::webgpu
