#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/Vertex.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <vector>

namespace engine::rendering
{

struct Mesh : public engine::core::Identifiable<Mesh>, public engine::core::Versioned
{
  public:
	using Handle = engine::core::Handle<Mesh>;
	using Ptr = std::shared_ptr<Mesh>;

	// Instead of exposing these directly, we'll provide accessor methods
  private:
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;

  public:
	// Accessors for vertices
	const std::vector<Vertex> &getVertices() const { return m_vertices; }
	const std::vector<uint32_t> &getIndices() const { return m_indices; }

	// Modify these properties with version tracking
	void setVertices(std::vector<Vertex> verts)
	{
		m_vertices = std::move(verts);
		incrementVersion();
	}

	void setIndices(std::vector<uint32_t> inds)
	{
		m_indices = std::move(inds);
		incrementVersion();
	}

	Mesh() = default;

	Mesh(std::vector<Vertex> verts, bool triangulated = true) :
		m_vertices(std::move(verts)), m_isIndexed(false), m_isTriangulated(triangulated) {}

	Mesh(std::vector<Vertex> verts, std::vector<uint32_t> inds, bool triangulated = true) :
		m_vertices(std::move(verts)), m_indices(std::move(inds)), m_isIndexed(true), m_isTriangulated(triangulated) {}

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
	   << "Vertices: " << mesh.getVertices().size() << ", "
	   << "Indices: " << mesh.getIndices().size()
	   << ")";
	return os;
}

} // namespace engine::rendering
