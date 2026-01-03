#include "engine/rendering/RenderCollector.h"

#include <algorithm>
#include <functional>

#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"

namespace engine::rendering
{

void RenderCollector::addModel(
	const engine::core::Handle<engine::rendering::Model> &modelHandle,
	const glm::mat4 &transform,
	uint32_t layer,
	uint64_t objectID
)
{
	auto modelOpt = modelHandle.get();
	if (!modelOpt.has_value())
		return;
	auto model = modelOpt.value();
	auto meshHandle = model->getMesh();
	auto meshOpt = meshHandle.get();
	if (!meshOpt.has_value())
		return;
	auto mesh = meshOpt.value();
	engine::math::AABB worldBounds = mesh->getBoundingBox().transformed(transform);

	if (!isAABBVisible(worldBounds))
		return;

	// Create or retrieve cached object bind group using objectID
	std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup = nullptr;

	if (m_context && m_objectBindGroupLayout)
	{
		if (objectID == 0)
		{
			spdlog::warn("RenderCollector::addModel called with objectID=0 for model {}. Bind group caching will not work efficiently.", modelHandle.id());
			return;
		}

		// Use objectID directly as cache key for static objects
		// This allows bind groups to persist across frames for the same object
		uint64_t cacheKey = objectID;

		// Check if bind group already exists in cache
		auto it = m_objectBindGroupCache.find(cacheKey);
		if (it != m_objectBindGroupCache.end())
		{
			// Found cached bind group - update uniforms if transform changed
			objectBindGroup = it->second;
			
			ObjectUniforms objectUniforms;
			objectUniforms.modelMatrix = transform;
			objectUniforms.normalMatrix = glm::transpose(glm::inverse(transform));
			objectBindGroup->updateBuffer(0, &objectUniforms, sizeof(ObjectUniforms), 0, m_context->getQueue());
		}
		else
		{
			// Create new bind group and cache it
			ObjectUniforms objectUniforms;
			objectUniforms.modelMatrix = transform;
			objectUniforms.normalMatrix = glm::transpose(glm::inverse(transform));

			objectBindGroup = m_context->bindGroupFactory().createBindGroup(m_objectBindGroupLayout);
			objectBindGroup->updateBuffer(0, &objectUniforms, sizeof(ObjectUniforms), 0, m_context->getQueue());

			// Cache for future frames
			m_objectBindGroupCache[cacheKey] = objectBindGroup;
		}
	}

	for (const auto &submesh : model->getSubmeshes())
	{
		RenderItem item;
		item.modelHandle = modelHandle;
		item.submesh = submesh;
		item.worldTransform = transform;
		item.renderLayer = layer;
		item.objectBindGroup = objectBindGroup;

		m_renderItems.push_back(item);
	}
}

void RenderCollector::addLight(const LightStruct &light)
{
	m_lights.push_back(light);
}

void RenderCollector::sort()
{
	std::sort(
		m_renderItems.begin(),
		m_renderItems.end(),
		[](const RenderItem &a, const RenderItem &b)
		{
			// extract to method for clarity
			return a < b;
		}
	);
}

void RenderCollector::clear()
{
	m_renderItems.clear();
	m_lights.clear();
	// NOTE: Do NOT clear m_objectBindGroupCache - it persists across frames for efficient bind group reuse
}

void RenderCollector::updateCameraData(
	const glm::mat4 &viewMatrix,
	const glm::mat4 &projMatrix,
	const glm::vec3 &cameraPosition,
	const engine::math::Frustum &frustum
)
{
	m_viewMatrix = viewMatrix;
	m_projMatrix = projMatrix;
	m_cameraPosition = cameraPosition;
	m_frustum = frustum;
}

bool RenderCollector::isAABBVisible(
	const engine::math::AABB &aabb
) const
{
	const auto &planes = m_frustum.asArray();

	const glm::vec3 center = aabb.center();
	const float radius = glm::length(aabb.extent());

	// Fast Radar test
	for (const auto &plane : planes)
	{
		const float distance =
			glm::dot(plane->normal, center) + plane->d;

		if (distance < -radius)
			return false;
	}

	// Test AABB against all planes
	for (const auto &plane : planes)
	{
		glm::vec3 positiveVertex = aabb.min;
		if (plane->normal.x >= 0)
			positiveVertex.x = aabb.max.x;
		if (plane->normal.y >= 0)
			positiveVertex.y = aabb.max.y;
		if (plane->normal.z >= 0)
			positiveVertex.z = aabb.max.z;

		// If positive vertex is outside, the box is not visible
		if (glm::dot(plane->normal, positiveVertex) + plane->d < 0)
			return false;
	}

	return true;
}

} // namespace engine::rendering
