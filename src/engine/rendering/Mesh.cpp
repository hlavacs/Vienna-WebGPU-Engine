#include "engine/rendering/Mesh.h"
#include <iostream>

namespace engine::rendering
{
void Mesh::computeTangentsIndexed()
{
	for (auto &v : m_vertices)
	{
		v.tangent = glm::vec4(0.0f);
	}

	incrementVersion();

	// Process triangles (3 indices per triangle)
	for (size_t i = 0; i + 2 < m_indices.size(); i += 3)
	{
		uint32_t i0 = m_indices[i];
		uint32_t i1 = m_indices[i + 1];
		uint32_t i2 = m_indices[i + 2];

		Vertex v[3] = {m_vertices[i0], m_vertices[i1], m_vertices[i2]};

		glm::vec3 faceNormal = glm::normalize(glm::cross(v[1].position - v[0].position, v[2].position - v[0].position));

		glm::vec4 faceTangent = Mesh::computeTBN(v, faceNormal);

		m_vertices[i0].tangent += faceTangent;
		m_vertices[i1].tangent += faceTangent;
		m_vertices[i2].tangent += faceTangent;
	}

	for (auto &v : m_vertices)
	{
		auto T = glm::vec3(v.tangent);
		glm::vec3 N = v.normal;

		// Orthonormalize per-vertex
		T = glm::normalize(T - N * glm::dot(N, T));

		float w = (v.tangent.w >= 0.0f) ? 1.0f : -1.0f;
		v.tangent = glm::vec4(T, w);
	}
}

void Mesh::computeTangentsNonIndexed()
{
	for (auto &v : m_vertices)
	{
		v.tangent = glm::vec4(0.0f);
	}

	incrementVersion();

	// Process triangles (3 indices per triangle)
	for (size_t i = 0; i + 2 < m_vertices.size(); i += 3)
	{
		engine::rendering::Vertex *v = &m_vertices[i];

		glm::vec3 faceNormal = glm::normalize(glm::cross(v[1].position - v[0].position, v[2].position - v[0].position));

		glm::vec4 tbn = Mesh::computeTBN(v, faceNormal);
		glm::vec4 faceTangent = Mesh::computeTBN(v, faceNormal);

		m_vertices[0].tangent += faceTangent;
		m_vertices[1].tangent += faceTangent;
		m_vertices[2].tangent += faceTangent;
	}

	for (auto &v : m_vertices)
	{
		auto T = glm::vec3(v.tangent);
		glm::vec3 N = v.normal;

		// Orthonormalize per-vertex
		T = glm::normalize(T - N * glm::dot(N, T));

		float w = (v.tangent.w >= 0.0f) ? 1.0f : -1.0f;
		v.tangent = glm::vec4(T, w);
	}
}

glm::vec4 Mesh::computeTBN(const Vertex corners[3], const glm::vec3 &expectedN)
{
	// Edge vectors in position space
	glm::vec3 edge1 = corners[1].position - corners[0].position;
	glm::vec3 edge2 = corners[2].position - corners[0].position;

	// Edge vectors in UV space
	glm::vec2 deltaUV1 = corners[1].uv - corners[0].uv;
	glm::vec2 deltaUV2 = corners[2].uv - corners[0].uv;

	const float EPSILON = 1e-6f;
	float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;

	glm::vec3 tangent(0.0f);
	glm::vec3 bitangent(0.0f);

	if (fabs(det) > EPSILON)
	{
		float invDet = 1.0f / det;
		tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invDet;
		bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * invDet;
	}
	else
	{
		// Degenerate UV, fallback: create tangent perpendicular to normal
		tangent = glm::normalize(abs(expectedN.x) > 0.99f ? glm::cross(expectedN, glm::vec3(0, 1, 0)) : glm::cross(expectedN, glm::vec3(1, 0, 0)));
		bitangent = glm::cross(expectedN, tangent);
	}

	// Orthonormalize tangent
	tangent = glm::normalize(tangent - glm::dot(tangent, expectedN) * expectedN);

	// Compute handedness
	float w = (glm::dot(glm::cross(expectedN, tangent), bitangent) < 0.0f) ? -1.0f : 1.0f;

	return glm::vec4(tangent, w);
}

} // namespace engine::rendering