#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/math/AABB.h"
#include "engine/rendering/Vertex.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <vector>

namespace engine::rendering
{

struct Topology
{
	/**
	 * @enum TopologyType
	 * @brief Mesh topology types.
	 */
	enum Type
	{
		Points,
		Lines,
		LineStrip,
		Triangles,
		TriangleStrip

	};
	constexpr static Type Default = Type::Triangles;
	constexpr static const char *toString(Type type) noexcept
	{
		switch (type)
		{
		case Points:
			return "Points";
		case Lines:
			return "Lines";
		case LineStrip:
			return "LineStrip";
		case Triangles:
			return "Triangles";
		case TriangleStrip:
			return "TriangleStrip";
		default:
			return "Unknown";
		}
	}
	static Type fromString(const std::string &str) noexcept
	{
		if (str == "Points")
			return Points;
		else if (str == "Lines")
			return Lines;
		else if (str == "LineStrip")
			return LineStrip;
		else if (str == "Triangles")
			return Triangles;
		else if (str == "TriangleStrip")
			return TriangleStrip;
		else
			return Default;
	}
};

struct Mesh : public engine::core::Identifiable<Mesh>, public engine::core::Versioned
{
  public:
	using Handle = engine::core::Handle<Mesh>;
	using Ptr = std::shared_ptr<Mesh>;

  private:
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;

  public:
	Mesh() = default;

	Mesh(
		std::vector<Vertex> vertices,
		engine::math::AABB boundingBox,
		bool triangulated = true
	) :
		m_vertices(std::move(vertices)),
		m_boundingBox(boundingBox),
		m_isIndexed(false),
		m_isTriangulated(triangulated) {}

	Mesh(
		std::vector<Vertex> vertices,
		std::vector<uint32_t> indices,
		engine::math::AABB boundingBox,
		bool triangulated = true
	) :
		m_vertices(std::move(vertices)),
		m_indices(std::move(indices)),
		m_boundingBox(boundingBox),
		m_isIndexed(true),
		m_isTriangulated(triangulated) {}

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

	/**
	 * @brief Compute the TBN matrix for a triangle given its vertices and expected normal.
	 * @param corners Array of 3 vertices forming the triangle.
	 * @param expectedN The expected normal vector.
	 * @return The TBN matrix as a vec4 (tangent.xyz, handedness).
	 */
	static glm::vec4 computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN);

	/**
	 * @brief Set the vertices of the mesh.
	 * @param vertices The new vertex list.
	 */
	void setVertices(std::vector<Vertex> vertices)
	{
		m_vertices = std::move(vertices);
		incrementVersion();
	}

	/**
	 * @brief Set the indices of the mesh.
	 * @param indices The new index list.
	 */
	void setIndices(std::vector<uint32_t> indices)
	{
		m_indices = std::move(indices);
		m_isIndexed = m_indices.size() > 0;
		incrementVersion();
	}

	/**
	 * @brief Set the topology type of the mesh.
	 * @param topology The new topology type.
	 */
	void setTopology(Topology::Type topology)
	{
		m_topology = topology;
		incrementVersion();
	}

	/**
	 * @brief Set the bounding box of the mesh.
	 * @param aabb The new AABB.
	 * @note Should be updated when vertices change.
	 */
	void setBoundingBox(const engine::math::AABB &aabb)
	{
		m_boundingBox = aabb;
		incrementVersion();
	}

	/**
	 * @brief Check if the mesh is indexed.
	 * @return True if indexed, false otherwise.
	 */
	bool isIndexed() const { return m_isIndexed; }

	/**
	 * @brief Check if the mesh is triangulated.
	 * @return True if triangulated, false otherwise.
	 */
	bool isTriangulated() const { return m_isTriangulated; }

	/**
	 * @brief Get the topology type of the mesh.
	 * @return The topology type.
	 */
	Topology::Type getTopology() const { return m_topology; }

	/**
	 * @brief Get the bounding box of the mesh.
	 * @return The AABB.
	 */
	const engine::math::AABB &getBoundingBox() const { return m_boundingBox; }

	const std::vector<Vertex> &getVertices() const { return m_vertices; }
	const std::vector<uint32_t> &getIndices() const { return m_indices; }

  private:
	bool m_isIndexed = false;
	bool m_isTriangulated = true;
	engine::math::AABB m_boundingBox;
	Topology::Type m_topology = Topology::Type::Triangles;

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
