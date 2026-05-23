#pragma once

#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUBindGroup;
class WebGPUBuffer;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{
class FrameCache;

/**
 * @class ClusterManager
 * @brief Manages light clustering for deferred rendering.
 *
 * Implements a 3D cluster grid (24×14×32 = 10,752 clusters) that assigns lights
 * to clusters based on their world position and radius. Used during the composition
 * pass to efficiently select which lights affect each pixel.
 *
 * **Grid Dimensions:**
 * - X: 24 clusters (screen width)
 * - Y: 14 clusters (screen height)
 * - Z: 32 clusters (depth, logarithmic distribution)
 * - Total: 10,752 clusters, each capable of holding 256 lights
 *
 * **Workflow:**
 * 1. LightManager provides CPU light list
 * 2. SceneLightBuffer uploads to GPU storage buffer
 * 3. ClusterManager::assignLights() executes compute shader:
 *    - Assigns each light to intersecting clusters (atomics for thread safety)
 * 4. CompositionPass accesses cluster data to light individual pixels
 */
class ClusterManager
{
  public:
	// Grid dimensions
	static constexpr uint32_t CLUSTER_GRID_DIM_X = 24;
	static constexpr uint32_t CLUSTER_GRID_DIM_Y = 14;
	static constexpr uint32_t CLUSTER_GRID_DIM_Z = 32;
	static constexpr uint32_t CLUSTER_GRID_TOTAL = CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y * CLUSTER_GRID_DIM_Z;
	static constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 256;

	/**
	 * @brief Construct cluster manager.
	 * @param context WebGPU context for GPU resource management.
	 */
	explicit ClusterManager(webgpu::WebGPUContext &context);

	~ClusterManager() = default;

	/**
	 * @brief Initialize cluster buffer and compute shader.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Assign lights to clusters using compute shader.
	 * Must be called once per frame before composition pass.
	 * @param frameCache Frame data including lights and camera uniforms.
	 * @param sceneLightBindGroup Bind group containing light data (from SceneLightBuffer).
	 * @return True if assignment succeeded.
	 */
	bool assignLights(FrameCache &frameCache, const std::shared_ptr<webgpu::WebGPUBindGroup> &sceneLightBindGroup);

	/**
	 * @brief Get the cluster grid storage buffer.
	 * Used by composition pass to look up lights affecting each pixel.
	 * @return Shared pointer to cluster grid buffer.
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUBuffer> getClusterGridBuffer() const { return m_clusterGridBuffer; }

	/**
	 * @brief Get the cluster bind group for the composition pass.
	 * @return Shared pointer to cluster bind group.
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUBindGroup> getClusterBindGroup() const { return m_clusterBindGroup; }

	/**
	 * @brief Clear cluster assignments (resets count to 0 for all clusters).
	 * Called before each frame's light assignment.
	 * @return True if clear succeeded.
	 */
	bool clearClusters();

  private:
	webgpu::WebGPUContext &m_context;

	// GPU cluster storage
	std::shared_ptr<webgpu::WebGPUBuffer> m_clusterGridBuffer;      // Cluster offset + count buffer
	std::shared_ptr<webgpu::WebGPUBuffer> m_clusterIndicesBuffer;   // Flat light indices buffer
	std::shared_ptr<webgpu::WebGPUBindGroup> m_clusterBindGroup;

	// Compute shader
	wgpu::ComputePipeline m_computePipeline = nullptr;
	wgpu::PipelineLayout m_pipelineLayout = nullptr;

	/**
	 * @brief Create the cluster grid storage buffer.
	 * @return True if creation succeeded.
	 */
	bool createClusterGridBuffer();

	/**
	 * @brief Create the compute pipeline for clustering.
	 * @return True if creation succeeded.
	 */
	bool createComputePipeline();
};

} // namespace engine::rendering
