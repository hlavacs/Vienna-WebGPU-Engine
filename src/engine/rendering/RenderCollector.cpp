#include "engine/rendering/RenderCollector.h"

#include <algorithm>

namespace engine::rendering
{

void RenderCollector::addModel(
	const engine::core::Handle<engine::rendering::Model> &modelHandle,
	const glm::mat4 &transform,
	uint32_t layer
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

	for (const auto &submesh : model->getSubmeshes())
	{
		m_renderItems.push_back(RenderItem{modelHandle, submesh, transform, layer});
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
