#include <iostream>
#include "engine/rendering/Mesh.h"

namespace engine::rendering
{
	void Mesh::computeTangentsIndexed()
	{
		for (auto &v : m_vertices)
		{
			v.tangent = glm::vec3(0.0f);
			v.bitangent = glm::vec3(0.0f);
		}

		incrementVersion();

		// Process triangles (3 indices per triangle)
		for (size_t i = 0; i + 2 < m_indices.size(); i += 3)
		{
			uint32_t i0 = m_indices[i];
			uint32_t i1 = m_indices[i + 1];
			uint32_t i2 = m_indices[i + 2];

			Vertex &v0 = m_vertices[i0];
			Vertex &v1 = m_vertices[i1];
			Vertex &v2 = m_vertices[i2];

			Vertex v[3] = {v0, v1, v2};

			glm::vec3 faceNormal = glm::normalize(glm::cross(
				v[1].position - v[0].position,
				v[2].position - v[0].position));

			glm::mat3 tbn = Mesh::computeTBN(v, faceNormal);

			m_vertices[i0].tangent += tbn[0];
			m_vertices[i1].tangent += tbn[0];
			m_vertices[i2].tangent += tbn[0];

			m_vertices[i0].bitangent += tbn[1];
			m_vertices[i1].bitangent += tbn[1];
			m_vertices[i2].bitangent += tbn[1];
		}

		for (auto &v : m_vertices)
		{
			v.tangent = glm::normalize(v.tangent - glm::dot(v.tangent, v.normal) * v.normal);
			v.bitangent = glm::cross(v.normal, v.tangent);
		}
	}

	void Mesh::computeTangentsNonIndexed()
	{
		for (auto &v : m_vertices)
		{
			v.tangent = glm::vec3(0.0f);
			v.bitangent = glm::vec3(0.0f);
		}

		incrementVersion();

		// Process triangles (3 indices per triangle)
		for (size_t i = 0; i + 2 < m_vertices.size(); i += 3)
		{
			engine::rendering::Vertex *v = &m_vertices[i];

			glm::vec3 faceNormal = glm::normalize(glm::cross(
				v[1].position - v[0].position,
				v[2].position - v[0].position));

			glm::mat3 tbn = Mesh::computeTBN(v, faceNormal);

			v[0].tangent += tbn[0];
			v[1].tangent += tbn[0];
			v[2].tangent += tbn[0];

			v[0].bitangent += tbn[1];
			v[1].bitangent += tbn[1];
			v[2].bitangent += tbn[1];
		}

		for (auto &v : m_vertices)
		{
			v.tangent = glm::normalize(v.tangent - glm::dot(v.tangent, v.normal) * v.normal);
			v.bitangent = glm::cross(v.normal, v.tangent);
		}
	}

	glm::mat3x3 Mesh::computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN)
	{
		// Edge vectors in position space
		glm::vec3 edge1 = corners[1].position - corners[0].position;
		glm::vec3 edge2 = corners[2].position - corners[0].position;

		// Edge vectors in UV space
		glm::vec2 deltaUV1 = corners[1].uv - corners[0].uv;
		glm::vec2 deltaUV2 = corners[2].uv - corners[0].uv;

		// Calculate the denominator of the tangent/bitangent formula
		float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
		const float EPSILON = 1e-6f;

		glm::vec3 tangent(0.0f);
		glm::vec3 bitangent(0.0f);

		if (fabs(det) > EPSILON)
		{
			float invDet = 1.0f / det;
			tangent = glm::normalize((edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invDet);
			bitangent = glm::normalize((edge2 * deltaUV1.x - edge1 * deltaUV2.x) * invDet);
		}
		else
		{
			// Degenerate UV mapping, fallback: create arbitrary orthonormal basis from normal
			tangent = glm::normalize(abs(expectedN.x) > 0.99f ? glm::cross(expectedN, glm::vec3(0, 1, 0)) : glm::cross(expectedN, glm::vec3(1, 0, 0)));
			bitangent = glm::cross(expectedN, tangent);
		}

		// Fix orientation to match expected normal
		glm::vec3 N = glm::cross(tangent, bitangent);
		if (glm::dot(N, expectedN) < 0.0f)
		{
			tangent = -tangent;
			bitangent = -bitangent;
			N = -N;
		}

		// Orthonormalize tangent with respect to normal
		tangent = glm::normalize(tangent - dot(tangent, expectedN) * expectedN);
		bitangent = glm::cross(expectedN, tangent);
		N = expectedN;

		return glm::mat3x3(tangent, bitangent, N);
	}
}