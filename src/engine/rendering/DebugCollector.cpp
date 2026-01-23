#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <cstring>

namespace engine::rendering
{

// Factory methods for DebugPrimitive
DebugPrimitive DebugPrimitive::createLine(const glm::vec3 &from, const glm::vec3 &to, const glm::vec4 &color)
{
	DebugPrimitive p;
	p.type = static_cast<uint32_t>(DebugPrimitive::Type::Line);
	p.color = color;
	p.data.line.from = from;
	p.data.line.to = to;
	return p;
}

DebugPrimitive DebugPrimitive::createDisk(const glm::vec3 &center, const glm::vec3 &radii, const glm::vec4 &color)
{
	DebugPrimitive p;
	p.type = static_cast<uint32_t>(DebugPrimitive::Type::Disk);
	p.color = color;
	p.data.disk.center = center;
	p.data.disk.radii = radii;
	return p;
}

std::vector<DebugPrimitive> DebugPrimitive::createSphere(const glm::vec3 &center, float radius, const glm::vec4 &color)
{
	std::vector<DebugPrimitive> disks;
	disks.reserve(3);

	// Create 3 orthogonal disks to form a sphere wireframe
	// XY plane (disk in XZ, standing upright)
	disks.push_back(createDisk(center, glm::vec3(radius, 0.0f, radius), color));

	// YZ plane (disk rotated 90 degrees - will need rotation support in shader)
	// For now, just create 3 identical XZ disks
	disks.push_back(createDisk(center, glm::vec3(radius, 0.0f, radius), color));
	disks.push_back(createDisk(center, glm::vec3(radius, 0.0f, radius), color));

	return disks;
}

DebugPrimitive DebugPrimitive::createAABB(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color)
{
	DebugPrimitive p;
	p.type = static_cast<uint32_t>(DebugPrimitive::Type::AABB);
	p.color = color;
	p.data.aabb.min = min;
	p.data.aabb.max = max;
	return p;
}

DebugPrimitive DebugPrimitive::createArrow(const glm::vec3 &from, const glm::vec3 &to, float headSize, const glm::vec4 &color)
{
	DebugPrimitive p;
	p.type = static_cast<uint32_t>(DebugPrimitive::Type::Arrow);
	p.color = color;
	p.data.arrow.from = from;
	p.data.arrow.to = to;
	p.data.arrow.headSize = glm::vec3(headSize, 0.0f, 0.0f);
	return p;
}

std::vector<DebugPrimitive> DebugPrimitive::createTransformAxes(const glm::mat4 &transform, float scale)
{
	std::vector<DebugPrimitive> axes;
	axes.reserve(3);

	auto origin = glm::vec3(transform[3]);
	glm::vec3 xAxis = glm::vec3(transform[0]) * scale;
	glm::vec3 yAxis = glm::vec3(transform[1]) * scale;
	glm::vec3 zAxis = glm::vec3(transform[2]) * scale;

	// X-axis (red)
	axes.push_back(createLine(origin, origin + xAxis, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));

	// Y-axis (green)
	axes.push_back(createLine(origin, origin + yAxis, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)));

	// Z-axis (blue)
	axes.push_back(createLine(origin, origin + zAxis, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));

	return axes;
}

// DebugRenderCollector implementation
void DebugRenderCollector::addPrimitive(const DebugPrimitive &primitive)
{
	if (m_primitives.size() < MAX_DEBUG_PRIMITIVES)
	{
		m_primitives.push_back(primitive);
	}
}

void DebugRenderCollector::addPrimitives(const std::vector<DebugPrimitive> &primitives)
{
	for (const auto &primitive : primitives)
	{
		if (m_primitives.size() >= MAX_DEBUG_PRIMITIVES)
		{
			break; // Stop adding once we reach the limit
		}
		m_primitives.push_back(primitive);
	}
}

void DebugRenderCollector::addTransformAxes(const glm::mat4 &transform, float scale)
{
	auto axes = DebugPrimitive::createTransformAxes(transform, scale);
	addPrimitives(axes);
}

void DebugRenderCollector::addLine(const glm::vec3 &from, const glm::vec3 &to, const glm::vec4 &color)
{
	addPrimitive(DebugPrimitive::createLine(from, to, color));
}

void DebugRenderCollector::addDisk(const glm::vec3 &center, const glm::vec3 &radii, const glm::vec4 &color)
{
	addPrimitive(DebugPrimitive::createDisk(center, radii, color));
}

void DebugRenderCollector::addSphere(const glm::vec3 &center, float radius, const glm::vec4 &color)
{
	auto disks = DebugPrimitive::createSphere(center, radius, color);
	addPrimitives(disks);
}

void DebugRenderCollector::addAABB(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color)
{
	addPrimitive(DebugPrimitive::createAABB(min, max, color));
}

void DebugRenderCollector::addArrow(const glm::vec3 &from, const glm::vec3 &to, float headSize, const glm::vec4 &color)
{
	addPrimitive(DebugPrimitive::createArrow(from, to, headSize, color));
}

void DebugRenderCollector::clear()
{
	m_primitives.clear();
}

} // namespace engine::rendering
