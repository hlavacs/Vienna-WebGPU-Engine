#pragma once

#include "engine/core/Enum.h"

#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <ostream>

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
	glm::vec2 uv{};
	glm::vec4 tangent{};
	glm::vec3 color{};
	float _pad;

	bool operator==(const Vertex &other) const
	{
		return position == other.position && normal == other.normal && tangent == other.tangent && color == other.color && uv == other.uv;
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
			return VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::UV | VertexAttribute::Tangent;
		case VertexLayout::PositionNormalUVTangentColor:
			return VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::UV | VertexAttribute::Tangent | VertexAttribute::Color;
		case VertexLayout::DebugPosition:
			return VertexAttribute::Position;
		case VertexLayout::DebugPositionColor:
			return VertexAttribute::Position | VertexAttribute::Color;
		case VertexLayout::None:
			return VertexAttribute::None;
		default:
			return VertexAttribute::None;
		}
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

	static const size_t PositionSize = sizeof(Vertex::position);																													 // 12 bytes
	static const size_t PositionNormalSize = sizeof(Vertex::position) + sizeof(Vertex::normal);																						 // 24 bytes
	static const size_t PositionNormalUVSize = sizeof(Vertex::position) + sizeof(Vertex::normal) + sizeof(Vertex::uv);																 // 28 bytes
	static const size_t PositionNormalUVColorSize = sizeof(Vertex::position) + sizeof(Vertex::normal) + sizeof(Vertex::uv) + sizeof(Vertex::color);									 // 40 bytes
	static const size_t PositionNormalUVTangentSize = sizeof(Vertex::position) + sizeof(Vertex::normal) + sizeof(Vertex::uv) + sizeof(Vertex::tangent);								 // 48 bytes
	static const size_t PositionNormalUVTangentColorSize = sizeof(Vertex::position) + sizeof(Vertex::normal) + sizeof(Vertex::uv) + sizeof(Vertex::tangent) + sizeof(Vertex::color); // 60 bytes
	static const size_t DebugPositionColorSize = sizeof(Vertex::position) + sizeof(Vertex::color);																					 // 24 bytes

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
			return PositionSize; // 12 bytes
		case VertexLayout::PositionNormal:
			return PositionNormalSize; // 24 bytes
		case VertexLayout::PositionNormalUV:
			return PositionNormalUVSize; // 28 bytes
		case VertexLayout::PositionNormalUVColor:
			return PositionNormalUVColorSize; // 40 bytes
		case VertexLayout::PositionNormalUVTangent:
			return PositionNormalUVTangentSize; // 48 bytes
		case VertexLayout::PositionNormalUVTangentColor:
			return PositionNormalUVTangentColorSize; // 64 bytes
		case VertexLayout::DebugPosition:
			return PositionSize; // 12 bytes
		case VertexLayout::DebugPositionColor:
			return DebugPositionColorSize; // 24 bytes
		default:
			return sizeof(Vertex);
		}
	}

	static std::vector<uint8_t> repackVertices(const std::vector<Vertex> &vertices, VertexLayout layout)
	{
		size_t stride = Vertex::getStride(layout);
		std::vector<uint8_t> packed(vertices.size() * stride, 0); // Initialize to zero for proper padding

		auto vertexAttributes = Vertex::requiredAttributes(layout);
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			uint8_t *dst = packed.data() + i * stride;

			if (hasFlag(vertexAttributes, VertexAttribute::Position))
			{
				memcpy(dst, &vertices[i].position, sizeof(Vertex::position));
				dst += sizeof(Vertex::position);
			}

			if (hasFlag(vertexAttributes, VertexAttribute::Normal))
			{
				memcpy(dst, &vertices[i].normal, sizeof(Vertex::normal));
				dst += sizeof(Vertex::normal);
			}

			if (hasFlag(vertexAttributes, VertexAttribute::UV))
			{
				memcpy(dst, &vertices[i].uv, sizeof(Vertex::uv));
				dst += sizeof(Vertex::uv);
			}

			if (hasFlag(vertexAttributes, VertexAttribute::Tangent))
			{
				memcpy(dst, &vertices[i].tangent, sizeof(Vertex::tangent));
				dst += sizeof(Vertex::tangent);
			}

			if (hasFlag(vertexAttributes, VertexAttribute::Color))
			{
				memcpy(dst, &vertices[i].color, sizeof(Vertex::color));
				dst += sizeof(Vertex::color);
			}
		}

		return packed;
	}
};
static_assert(sizeof(Vertex) % 16 == 0, "Vertex size must be a multiple of 16 bytes.");

// Stream output
inline std::ostream &operator<<(std::ostream &os, const Vertex &v)
{
	os << "Vertex("
	   << "pos: [" << v.position.x << ", " << v.position.y << ", " << v.position.z << "], "
	   << "normal: [" << v.normal.x << ", " << v.normal.y << ", " << v.normal.z << "], "
	   << "tangent: [" << v.tangent.x << ", " << v.tangent.y << ", " << v.tangent.z << ", " << v.tangent.w << "], "
	   << "color: [" << v.color.r << ", " << v.color.g << ", " << v.color.b << "], "
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
		h = hashVec(v.color, h);
		h = hashVec(v.uv, h);
		return h;
	}
};
} // namespace std
