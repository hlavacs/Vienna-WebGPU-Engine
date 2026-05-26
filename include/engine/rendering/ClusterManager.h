#pragma once

#include <memory>
#include <unordered_map>
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

	uint32_t m_lightCount = 0; // Track the light count to know when to rebuild bind groups

	/**
	 * @brief Construct cluster manager.
	 * @param context WebGPU context for GPU resource management.
	 */
	explicit ClusterManager(webgpu::WebGPUContext &context);

	~ClusterManager();

	/**
	 * @brief Initialize cluster buffer and compute shader.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Assign lights to clusters using compute shader.
	 * Must be called once per frame, per camera, before the composition pass.
	 * Runs two compute dispatches: clear (per-cluster zero count) then assign
	 * (per-light append into intersecting clusters).
	 * @param cameraId Camera identifier - used to look up the per-camera frame
	 *                 uniform buffer in @p frameCache so the compute shader
	 *                 sees the active camera's view / projection matrices.
	 * @param frameCache Per-frame cache (frame bind groups live here).
	 * @param lightCount Number of valid entries in the scene light buffer.
	 * @return True if both dispatches succeeded.
	 */
	bool assignLights(uint64_t cameraId, FrameCache &frameCache, uint32_t lightCount);

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

	// GPU cluster storage shared with the composition pass (different bind-group
	// views of the same buffers: read-only for the fragment shader, atomic +
	// read_write for the compute shader).
	std::shared_ptr<webgpu::WebGPUBuffer> m_clusterGridBuffer;	  // {offset, count} per cluster
	std::shared_ptr<webgpu::WebGPUBuffer> m_clusterIndicesBuffer; // Flat u32 index pool
	std::shared_ptr<webgpu::WebGPUBindGroup> m_clusterBindGroup;  // composition-side, read-only

	// Compute pipeline (clear + assign share one bind group layout).
	wgpu::ShaderModule m_computeShaderModule = nullptr;
	wgpu::BindGroupLayout m_computeBindGroupLayout = nullptr;
	wgpu::PipelineLayout m_computePipelineLayout = nullptr;
	wgpu::ComputePipeline m_clearPipeline = nullptr;
	wgpu::ComputePipeline m_assignPipeline = nullptr;

	// Per-camera compute bind-group cache. Recreated only when the underlying
	// buffer identities change (e.g. SceneLightBuffer reallocates on capacity
	// growth). Rebuilding every frame was the dominant clustering overhead.
	struct CachedComputeBindGroup
	{
		wgpu::BindGroup bindGroup = nullptr;

		WGPUBuffer frameBuffer = nullptr;
		WGPUBuffer lightBuffer = nullptr;

		uint32_t lastLightCount = 0;
	};
	std::unordered_map<uint64_t, CachedComputeBindGroup> m_computeBindGroups;

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

	/**
	 * @brief Get the cached compute bind group for this camera, rebuilding it
	 * only when the underlying frame / light buffer identities change.
	 * Returns nullptr on failure.
	 */
	wgpu::BindGroup getOrCreateComputeBindGroup(uint64_t cameraId, wgpu::Buffer frameBuffer, wgpu::Buffer lightBuffer, uint32_t lightCount);
};

} // namespace engine::rendering
