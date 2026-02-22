#pragma once

#include "engine/core/Enum.h"
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUBuffer;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

/**
 * @brief GPU-compatible debug primitive structure.
 *
 * Layout matches the shader's DebugPrimitive struct.
 * Uses a union to minimize memory usage while supporting multiple primitive types.
 */
struct alignas(16) DebugPrimitive
{
	enum class Type : uint32_t
	{
		Line = 0,
		Disk = 1,
		AABB = 2,
		Arrow = 3
	};

	float padding1[3]; // Align to 16 bytes
	uint32_t type;	   // DebugPrimitiveKind
	glm::vec4 color;   // RGBA color

	// Union data - 48 bytes (3 vec4s)
	// Interpreted differently based on type
	union Data
	{
		// Line: from (vec3), to (vec3)
		struct Line
		{
			glm::vec3 from;
			float _pad0;
			glm::vec3 to;
			float _pad1;
			glm::vec4 _unused;
		} line{};

		// Disk: center (vec3), radii (vec3) - elliptical disk
		struct Disk
		{
			glm::vec3 center;
			float _pad0;
			glm::vec3 radii;
			float _pad1;
			glm::vec4 _unused;
		} disk;

		// AABB: min (vec3), max (vec3)
		struct AABB
		{
			glm::vec3 min;
			float _pad0;
			glm::vec3 max;
			float _pad1;
			glm::vec4 _unused;
		} aabb;

		// Arrow: from (vec3), to (vec3), headSize (vec3)
		struct Arrow
		{
			glm::vec3 from;
			float _pad0;
			glm::vec3 to;
			float _pad1;
			glm::vec3 headSize;
			float _pad2;
		} arrow;

		Data() { memset(this, 0, sizeof(Data)); }
	} data;

	// Factory methods for creating primitives
	static DebugPrimitive createLine(const glm::vec3 &from, const glm::vec3 &to, const glm::vec4 &color = glm::vec4(1.0f));
	static std::vector<DebugPrimitive> createFrustum(const glm::mat4 &viewProjection, const glm::vec4 &color = glm::vec4(1.0f));
	static DebugPrimitive createDisk(const glm::vec3 &center, const glm::vec3 &radii, const glm::vec4 &color = glm::vec4(1.0f));
	static DebugPrimitive createAABB(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color = glm::vec4(1.0f));
	static DebugPrimitive createArrow(const glm::vec3 &from, const glm::vec3 &to, float headSize, const glm::vec4 &color = glm::vec4(1.0f));

	// Helper for creating a sphere from 3 orthogonal disks
	static std::vector<DebugPrimitive> createSphere(const glm::vec3 &center, float radius, const glm::vec4 &color = glm::vec4(1.0f));

	// Helper for transform axes visualization
	static std::vector<DebugPrimitive> createTransformAxes(const glm::mat4 &transform, float scale = 1.0f);
};

// Ensure correct size and alignment for GPU
static_assert(sizeof(DebugPrimitive) == 80, "DebugPrimitive must be 80 bytes for GPU alignment");

/**
 * @brief Collects debug primitives from the scene graph for rendering.
 *
 * During the debug render stage, nodes with debug enabled will add primitives
 * to this collector. The collector then generates a GPU buffer for efficient rendering.
 */
class DebugRenderCollector
{
  public:
	DebugRenderCollector() = default;
	~DebugRenderCollector() = default;

	/**
	 * @brief Add a debug primitive to the collection.
	 */
	void addPrimitive(const DebugPrimitive &primitive);

	/**
	 * @brief Add multiple primitives at once.
	 */
	void addPrimitives(const std::vector<DebugPrimitive> &primitives);

	/**
	 * @brief Add transform axes (X=red, Y=green, Z=blue) for visualization.
	 * @param transform The transform matrix to visualize.
	 * @param scale Scale factor for the axis lines (default = 1.0f).
	 */
	void addTransformAxes(const glm::mat4 &transform, float scale = 1.0f);

	/**
	 * @brief Add a line primitive.
	 */
	void addLine(const glm::vec3 &from, const glm::vec3 &to, const glm::vec4 &color = glm::vec4(1.0f));

	/**
	 * @brief Add a frustum primitive.
	 */
	void addFrustum(const glm::mat4 &viewProjection, const glm::vec4 &color = glm::vec4(1.0f));

	/**
	 * @brief Add a disk primitive.
	 */
	void addDisk(const glm::vec3 &center, const glm::vec3 &radii, const glm::vec4 &color = glm::vec4(1.0f));

	/**
	 * @brief Add a sphere primitive (created from 3 orthogonal disks).
	 */
	void addSphere(const glm::vec3 &center, float radius, const glm::vec4 &color = glm::vec4(1.0f));

	/**
	 * @brief Add an AABB primitive.
	 */
	void addAABB(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color = glm::vec4(1.0f));

	/**
	 * @brief Add an arrow primitive.
	 */
	void addArrow(const glm::vec3 &from, const glm::vec3 &to, float headSize, const glm::vec4 &color = glm::vec4(1.0f));

	/**
	 * @brief Clear all collected primitives.
	 */
	void clear();

	/**
	 * @brief Get the current list of primitives.
	 */
	[[nodiscard]] const std::vector<DebugPrimitive> &getPrimitives() const { return m_primitives; }

	/**
	 * @brief Get number of primitives.
	 */
	[[nodiscard]] size_t getPrimitiveCount() const { return m_primitives.size(); }

	/**
	 * @brief Check if there are any primitives to render.
	 */
	[[nodiscard]] bool isEmpty() const { return m_primitives.empty(); }

	/**
	 * @brief Check if the collection is at capacity.
	 */
	[[nodiscard]] bool isFull() const { return m_primitives.size() >= MAX_DEBUG_PRIMITIVES; }

	/**
	 * @brief Get the maximum number of primitives that can be stored.
	 */
	static constexpr size_t getMaxPrimitives() { return MAX_DEBUG_PRIMITIVES; }

  private:
	static constexpr size_t MAX_DEBUG_PRIMITIVES = 1024;
	std::vector<DebugPrimitive> m_primitives;
};

} // namespace engine::rendering
