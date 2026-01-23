#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/math/AABB.h"
#include "engine/rendering/Vertex.h"

#include <tiny_gltf.h>

namespace engine::resources
{

/**
 * @struct GltfMaterialContext
 * @brief Holds references to all glTF material-related arrays.
 *
 * Provides all necessary data to construct engine Material objects
 * without passing the entire tinygltf::Model.
 */
struct GltfMaterialContext
{
	const std::vector<tinygltf::Material> &materials;
	const std::vector<tinygltf::Texture> &textures;
	const std::vector<tinygltf::Image> &images;
	const std::vector<tinygltf::Sampler> &samplers;
};

/**
 * @struct GltfGeometryData
 * @brief Holds parsed geometry and material data from a glTF/glb file.
 *
 * Supports multiple primitives per mesh, optional skinning, and morph targets.
 */
struct GltfGeometryData
{
	std::string filePath;
	std::string name;

	// Flattened vertex buffer containing all vertices of all primitives
	std::vector<engine::rendering::Vertex> vertices;
	std::vector<uint32_t> indices; // Global index buffer for all primitives

	engine::math::AABB boundingBox;

	/**
	 * @struct PrimitiveRange
	 * @brief Describes a single glTF primitive within a mesh
	 */
	struct PrimitiveRange
	{
		int materialId = -1;	   ///< Index into the glTF materials array
		uint32_t indexOffset = 0;  ///< Start offset in the global indices array
		uint32_t indexCount = 0;   ///< Number of indices for this primitive
		uint32_t vertexOffset = 0; ///< Start vertex in the vertices array
		uint32_t vertexCount = 0;  ///< Number of vertices for this primitive
		uint32_t flags = 0;		   ///< Optional flags: skinned, morph targets
	};

	std::vector<PrimitiveRange> primitives; ///< One per glTF primitive

	// Material context instead of full Model
	std::shared_ptr<GltfMaterialContext> materialContext;

	// Optional skinning
	struct SkinData
	{
		std::string name;
		std::vector<int> jointNodeIndices; ///< Indices of joint nodes in glTF scene
		std::vector<glm::mat4> inverseBindMatrices;
	};
	std::vector<SkinData> skins;

	// Optional animations
	struct AnimationData
	{
		std::string name;
		tinygltf::Animation gltfAnimation; ///< Keep original tinygltf animation for later use
	};
	std::vector<AnimationData> animations;

	/**
	 * @brief Clear all loaded data
	 */
	void clear()
	{
		vertices.clear();
		indices.clear();
		primitives.clear();
		materialContext.reset();
		skins.clear();
		animations.clear();
		boundingBox = engine::math::AABB();
	}

	/**
	 * @brief Returns the total number of vertices
	 */
	[[nodiscard]] size_t vertexCount() const { return vertices.size(); }

	/**
	 * @brief Returns the total number of indices
	 */
	[[nodiscard]] size_t indexCount() const { return indices.size(); }

	/**
	 * @brief Returns the total number of primitives
	 */
	[[nodiscard]] size_t primitiveCount() const { return primitives.size(); }

	/**
	 * @brief Check if any geometry was loaded
	 */
	[[nodiscard]] bool isEmpty() const { return vertices.empty(); }
};

} // namespace engine::resources
