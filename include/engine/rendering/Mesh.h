#pragma once

#include <vector>
#include <filesystem>
#include <glm/glm.hpp>
#include "engine/rendering/Vertex.h"

namespace engine::rendering
{

	struct Mesh
	{
	public:
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		Mesh() = default;

		Mesh(std::vector<Vertex> verts, bool triangulated = true)
			: vertices(std::move(verts)), m_isIndexed(false), m_isTriangulated(triangulated) {}

		Mesh(std::vector<Vertex> verts, std::vector<uint32_t> inds, bool triangulated = true)
			: vertices(std::move(verts)), indices(std::move(inds)), m_isIndexed(true), m_isTriangulated(triangulated) {}

		void computeTangents()
		{
			if (m_isIndexed)
			{
				computeTangentsIndexed();
			}
			else
			{
				computeTangentsNonIndexed();
			}
		}

		static glm::mat3x3 computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN);

		// Getter
		bool isIndexed() const
		{
			return m_isIndexed;
		}

		bool isTriangulated() const
		{
			return m_isTriangulated;
		}

	private:
		bool m_isIndexed = false;
		bool m_isTriangulated = true;

		void computeTangentsIndexed();

		void computeTangentsNonIndexed();
	};

	inline std::ostream &operator<<(std::ostream &os, const Mesh &mesh)
	{
		os << "Mesh("
		   << "Triangulated: " << (mesh.isTriangulated() ? "true" : "false") << ", "
		   << "Indexed: " << (mesh.isIndexed() ? "true" : "false") << ", "
		   << "Vertices: " << mesh.vertices.size() << ", "
		   << "Indices: " << mesh.indices.size()
		   << ")";
		return os;
	}

} // namespace engine::rendering
