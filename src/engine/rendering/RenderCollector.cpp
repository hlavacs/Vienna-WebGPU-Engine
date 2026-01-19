#include "engine/rendering/RenderCollector.h"

#include <algorithm>

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

	// Calculate world-space AABB for later culling
	engine::math::AABB worldBounds = mesh->getBoundingBox().transformed(transform);

	// NO culling here - just collect unconditionally
	// Culling happens on-demand via extractVisible() / extractForLight()

	// Create CPU-only render items for each submesh
	for (const auto &submesh : model->getSubmeshes())
	{
		RenderItemCPU item;
		item.modelHandle = modelHandle;
		item.submesh = submesh;
		item.worldTransform = transform;
		item.worldBounds = worldBounds;
		item.renderLayer = layer;
		item.objectID = objectID;

		m_renderItems.push_back(item);
	}
}

void RenderCollector::addLight(const Light &light)
{
	m_lights.push_back(light);
}

void RenderCollector::sort()
{
	std::sort(
		m_renderItems.begin(),
		m_renderItems.end(),
		[](const RenderItemCPU &a, const RenderItemCPU &b)
		{
			return a < b;
		}
	);
}

void RenderCollector::clear()
{
	m_renderItems.clear();
	m_lights.clear();
}

std::vector<size_t> RenderCollector::extractVisible(const engine::math::Frustum &frustum) const
{
	std::vector<size_t> visibleIndices;
	visibleIndices.reserve(m_renderItems.size());

	for (size_t i = 0; i < m_renderItems.size(); ++i)
	{
		if (isAABBVisibleInFrustum(m_renderItems[i].worldBounds, frustum))
		{
			visibleIndices.push_back(i);
		}
	}

	return visibleIndices;
}

std::vector<size_t> RenderCollector::extractForLightFrustum(const engine::math::Frustum &lightFrustum) const
{
	std::vector<size_t> visibleIndices;
	visibleIndices.reserve(m_renderItems.size());

	for (size_t i = 0; i < m_renderItems.size(); ++i)
	{
		if (isAABBVisibleInFrustum(m_renderItems[i].worldBounds, lightFrustum))
		{
			visibleIndices.push_back(i);
		}
	}

	return visibleIndices;
}

std::vector<size_t> RenderCollector::extractForPointLight(const glm::vec3 &lightPosition, float lightRange) const
{
	std::vector<size_t> visibleIndices;
	visibleIndices.reserve(m_renderItems.size());

	for (size_t i = 0; i < m_renderItems.size(); ++i)
	{
		if (isAABBInSphere(m_renderItems[i].worldBounds, lightPosition, lightRange))
		{
			visibleIndices.push_back(i);
		}
	}

	return visibleIndices;
}

std::vector<LightStruct> RenderCollector::extractLightUniformsWithShadows(uint32_t maxShadow2D, uint32_t maxShadowCube) const
{
	std::vector<LightStruct> uniforms;
	uniforms.reserve(m_lights.size());

	uint32_t shadow2DIndex = 0;
	uint32_t shadowCubeIndex = 0;

	for (auto &light : m_lights)
	{ // Assign shadow indices only if the light casts shadows
		auto lightUniforms = light.toUniforms();
		if (light.canCastShadows())
		{
			switch (light.getLightType())
			{
			case engine::rendering::Light::Type::Directional: // directional
			case engine::rendering::Light::Type::Spot: // spot
				lightUniforms.shadowIndex = shadow2DIndex < maxShadow2D ? shadow2DIndex : -1;
				shadow2DIndex++;
				break;
			case engine::rendering::Light::Type::Point: // point
				lightUniforms.shadowIndex = shadowCubeIndex < maxShadowCube ? shadowCubeIndex : -1;
				shadowCubeIndex++;
				break;
			}
		}
		uniforms.push_back(lightUniforms);
	}

	return uniforms;
}

bool RenderCollector::isAABBVisibleInFrustum(
	const engine::math::AABB &aabb,
	const engine::math::Frustum &frustum
)
{
	const auto &planes = frustum.asArray();

	const glm::vec3 center = aabb.center();
	const float radius = glm::length(aabb.extent());

	// Fast sphere test
	for (const auto &plane : planes)
	{
		const float distance = glm::dot(plane->normal, center) + plane->d;
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

bool RenderCollector::isAABBInSphere(
	const engine::math::AABB &aabb,
	const glm::vec3 &center,
	float radius
)
{
	// Find the closest point on the AABB to the sphere center
	glm::vec3 closestPoint = glm::clamp(center, aabb.min, aabb.max);

	// Check if the closest point is within the sphere
	glm::vec3 diff = closestPoint - center;
	float distanceSquared = glm::dot(diff, diff);
	return distanceSquared <= (radius * radius);
}

} // namespace engine::rendering
