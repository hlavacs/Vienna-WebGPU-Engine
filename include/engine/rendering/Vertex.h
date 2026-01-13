#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <ostream>
#include "engine/core/Enum.h"

namespace engine::rendering
{

/**
 * @brief Vertex attributes as bitmask flags.
 */
enum class VertexAttribute : uint32_t
{
	None = 0,
	Position = 1 << 0,
	Normal = 1 << 1,
	Tangent = 1 << 2,
	Bitangent = 1 << 3,
	Color = 1 << 4,
	UV = 1 << 5,
};

ENUM_BIT_OPERATORS(VertexAttribute)
ENUM_BIT_FLAGS_HAS(VertexAttribute)

/**
 * @brief Predefined vertex layouts.
 * Defines common combinations of vertex attributes.
 */
enum class VertexLayout : uint8_t
{
	// No vertex buffer (procedural generation in vertex shader)
	None,
	// Production PBR / unlit
	Position,
	PositionNormal,
	PositionNormalUV,
	PositionNormalUVColor,
	PositionNormalUVTangent,
	PositionNormalUVTangentColor,
	// Debug / utility
	DebugPosition,
	DebugPositionColor
};

/**
 * @brief Vertex structure with common attributes.
 * Used for mesh data and rendering.
 */
struct Vertex
{
	glm::vec3 position{};
	glm::vec3 normal{};
	glm::vec3 tangent{};
	glm::vec3 bitangent{};
	glm::vec4 color{};
	glm::vec2 uv{};

	bool operator==(const Vertex &other) const
	{
		return position == other.position && normal == other.normal && tangent == other.tangent && bitangent == other.bitangent && color == other.color && uv == other.uv;
	}

	/**
	 * @brief Get the required vertex attributes for a given layout.
	 * @param layout The vertex layout.
	 * @return The required vertex attributes as bitmask.
	 */
	static inline constexpr VertexAttribute requiredAttributes(VertexLayout layout)
	{
		switch (layout)
		{
		case VertexLayout::Position:
			return VertexAttribute::Position;
		case VertexLayout::PositionNormal:
			return VertexAttribute::Position | VertexAttribute::Normal;
		case VertexLayout::PositionNormalUV:
			return VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::UV;
		case VertexLayout::PositionNormalUVColor:
			return VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::UV | VertexAttribute::Color;
		case VertexLayout::PositionNormalUVTangent:
			return VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::UV | VertexAttribute::Tangent | VertexAttribute::Bitangent;
		case VertexLayout::PositionNormalUVTangentColor:
			return VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::UV | VertexAttribute::Tangent | VertexAttribute::Bitangent | VertexAttribute::Color;
		case VertexLayout::DebugPosition:
			return VertexAttribute::Position;
		case VertexLayout::DebugPositionColor:
			return VertexAttribute::Position | VertexAttribute::Color;
		}
		return VertexAttribute::None;
	}

	static inline constexpr bool has(VertexAttribute mask, VertexAttribute bit)
	{
		return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit)) != 0;
	}

	/**
	 * @brief Select the best matching vertex layout based on available attributes.
	 * @param available The available vertex attributes.
	 * @return The best matching vertex layout.
	 */
	static inline VertexLayout selectBestVertexLayout(VertexAttribute available)
	{
		// Order matters: most specific first
		if (has(available, requiredAttributes(VertexLayout::PositionNormalUVTangentColor)))
			return VertexLayout::PositionNormalUVTangentColor;
		if (has(available, requiredAttributes(VertexLayout::PositionNormalUVTangent)))
			return VertexLayout::PositionNormalUVTangent;
		if (has(available, requiredAttributes(VertexLayout::PositionNormalUVColor)))
			return VertexLayout::PositionNormalUVColor;
		if (has(available, requiredAttributes(VertexLayout::PositionNormalUV)))
			return VertexLayout::PositionNormalUV;
		if (has(available, requiredAttributes(VertexLayout::PositionNormal)))
			return VertexLayout::PositionNormal;
		if (has(available, requiredAttributes(VertexLayout::Position)))
			return VertexLayout::Position;
		if (has(available, requiredAttributes(VertexLayout::DebugPositionColor)))
			return VertexLayout::DebugPositionColor;
		return VertexLayout::DebugPosition;
	}

	/**
	 * @brief Get the stride (size in bytes) for a given vertex layout.
	 * @param layout The vertex layout.
	 * @return The stride in bytes.
	 */
	static inline constexpr size_t getStride(VertexLayout layout)
	{
		switch (layout)
		{
		case VertexLayout::None:
			return 0;
		case VertexLayout::Position:
			return sizeof(glm::vec3); // 12 bytes
		case VertexLayout::PositionNormal:
			return sizeof(glm::vec3) * 2; // 24 bytes
		case VertexLayout::PositionNormalUV:
			return sizeof(glm::vec3) * 2 + sizeof(glm::vec2); // 32 bytes
		case VertexLayout::PositionNormalUVColor:
			return sizeof(glm::vec3) * 2 + sizeof(glm::vec2) + sizeof(glm::vec4); // 48 bytes
		case VertexLayout::PositionNormalUVTangent:
			return sizeof(glm::vec3) * 5 + sizeof(glm::vec2); // 68 bytes
		case VertexLayout::PositionNormalUVTangentColor:
			return sizeof(Vertex); // Full vertex: 84 bytes
		case VertexLayout::DebugPosition:
			return sizeof(glm::vec3); // 12 bytes
		case VertexLayout::DebugPositionColor:
			return sizeof(glm::vec3) + sizeof(glm::vec4); // 28 bytes
		}
		return sizeof(Vertex); // Default to full vertex
	}

	
static std::vector<uint8_t> repackVertices(const std::vector<Vertex> &vertices, VertexLayout layout)
{
	size_t stride = Vertex::getStride(layout);
	std::vector<uint8_t> packed(vertices.size() * stride);

	auto vertexAttributes = Vertex::requiredAttributes(layout);
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		uint8_t *dst = packed.data() + i * stride;

		if (hasFlag(vertexAttributes, VertexAttribute::Position))
			memcpy(dst, &vertices[i].position, sizeof(glm::vec3));
		dst += sizeof(glm::vec3);

		if (hasFlag(vertexAttributes, VertexAttribute::Normal))
			memcpy(dst, &vertices[i].normal, sizeof(glm::vec3));
		dst += sizeof(glm::vec3);

		if (hasFlag(vertexAttributes, VertexAttribute::Tangent))
			memcpy(dst, &vertices[i].tangent, sizeof(glm::vec3));
		dst += sizeof(glm::vec3);
		if (hasFlag(vertexAttributes, VertexAttribute::Bitangent))
			memcpy(dst, &vertices[i].bitangent, sizeof(glm::vec3));
		dst += sizeof(glm::vec3);
		if (hasFlag(vertexAttributes, VertexAttribute::UV))
			memcpy(dst, &vertices[i].uv, sizeof(glm::vec2));
		dst += sizeof(glm::vec2);

		if (hasFlag(vertexAttributes, VertexAttribute::Color))
			memcpy(dst, &vertices[i].color, sizeof(glm::vec4));
		dst += sizeof(glm::vec4);
	}

	return packed;
}
};

// Stream output
inline std::ostream &operator<<(std::ostream &os, const Vertex &v)
{
	os << "Vertex("
	   << "pos: [" << v.position.x << ", " << v.position.y << ", " << v.position.z << "], "
	   << "normal: [" << v.normal.x << ", " << v.normal.y << ", " << v.normal.z << "], "
	   << "tangent: [" << v.tangent.x << ", " << v.tangent.y << ", " << v.tangent.z << "], "
	   << "bitangent: [" << v.bitangent.x << ", " << v.bitangent.y << ", " << v.bitangent.z << "], "
	   << "color: [" << v.color.r << ", " << v.color.g << ", " << v.color.b << ", " << v.color.a << "], "
	   << "uv: [" << v.uv.x << ", " << v.uv.y << "])";
	return os;
}

} // namespace engine::rendering

/**
 * @brief Hash function for Vertex structure.
 */
namespace std
{
template <>
struct hash<engine::rendering::Vertex>
{
	std::size_t operator()(const engine::rendering::Vertex &v) const noexcept
	{
		auto hasher = std::hash<float>{};
		auto hashVec = [&](const auto &vec, std::size_t seed = 0)
		{
			for (int i = 0; i < vec.length(); ++i)
			{
				seed ^= hasher(vec[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			}
			return seed;
		};

		std::size_t h = 0;
		h = hashVec(v.position, h);
		h = hashVec(v.normal, h);
		h = hashVec(v.tangent, h);
		h = hashVec(v.bitangent, h);
		h = hashVec(v.color, h);
		h = hashVec(v.uv, h);
		return h;
	}
};
} // namespace std
