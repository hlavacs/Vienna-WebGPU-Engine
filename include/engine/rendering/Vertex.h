#pragma once

#include <filesystem>
#include <glm/glm.hpp>

namespace engine::rendering
{
struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 tangent;	 // for normal mapping
	glm::vec3 bitangent; // for normal mapping
	glm::vec4 color;	 // optional but good for AO tinting etc.
	glm::vec2 uv;

	bool operator==(const Vertex &other) const
	{
		return position == other.position && normal == other.normal && tangent == other.tangent && bitangent == other.bitangent && color == other.color && uv == other.uv;
	}
};

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

namespace std
{
template <>
struct hash<engine::rendering::Vertex>
{
	std::size_t operator()(const engine::rendering::Vertex &v) const noexcept
	{
		auto hasher = std::hash<float>{};

		// Helper für Vec-Hashes
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
