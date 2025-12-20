#include "engine/rendering/webgpu/WebGPUModelFactory.h"

#include "engine/rendering/Model.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUMeshFactory.h"

namespace engine::rendering::webgpu
{

WebGPUModelFactory::WebGPUModelFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context) {}

std::shared_ptr<WebGPUModel> WebGPUModelFactory::createFromHandle(
	const engine::rendering::Model::Handle &modelHandle,
	const WebGPUModelOptions &options
)
{
	auto modelOpt = modelHandle.get();
	if (!modelOpt || !modelOpt.value())
	{
		throw std::runtime_error("Invalid model handle in WebGPUModelFactory::createFromHandle");
	}

	const auto &model = *modelOpt.value();

	// Use mesh and material factories to create GPU resources from handles
	auto meshHandle = model.getMesh();

	std::shared_ptr<WebGPUMesh> mesh = nullptr;

	// Create mesh if available
	if (meshHandle.valid())
	{
		mesh = m_context.meshFactory().createFromHandle(meshHandle);

		std::vector<WebGPUMesh::WebGPUSubmesh> submeshes;
		for (const auto &submesh : model.getSubmeshes())
		{
			WebGPUMesh::WebGPUSubmesh webgpuSubmesh;
			webgpuSubmesh.indexOffset = submesh.indexOffset;
			webgpuSubmesh.indexCount = submesh.indexCount;
			webgpuSubmesh.material = m_context.materialFactory().createFromHandle(submesh.material);
			submeshes.push_back(std::move(webgpuSubmesh));
		}
		mesh->setSubmeshes(std::move(submeshes));
	}

	return std::make_shared<WebGPUModel>(
		m_context,
		modelHandle,
		mesh,
		options
	);
}

std::shared_ptr<WebGPUModel> WebGPUModelFactory::createFromHandle(
	const engine::rendering::Model::Handle &handle
)
{
	return createFromHandle(handle, WebGPUModelOptions{});
}

} // namespace engine::rendering::webgpu
