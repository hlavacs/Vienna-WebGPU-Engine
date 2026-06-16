#pragma once

#include <memory>
#include <unordered_map>

#include <glm/glm.hpp>
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

	~ClusterManager();

	/**
	 * @brief Initialize cluster buffer and compute shader.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Assign lights to clusters using compute shader.
	 *
	 * Must be called once per frame, per camera, before the composition pass.
	 * Runs two compute dispatches: clear (per-cluster zero count) then assign
	 * (per-light append into intersecting clusters).
	 *
	 * **Skip on unchanged inputs.** The cluster grid is a pure function of
	 * (camera view-projection, light data, light count). When all three
	 * match the previous invocation for this camera the dispatches are
	 * elided — the GPU grid buffer is still populated from the previous
	 * frame's work. `viewProjection` is the cheapest fingerprint for the
	 * camera (mat4 equality, 16 floats); `SceneLightBuffer::
	 * getLastUploadHash()` already gives a fingerprint of the light
	 * payload that's computed once at upload time.
	 *
	 * @param cameraId Camera identifier - used to look up the per-camera frame
	 *                 uniform buffer in @p frameCache so the compute shader
	 *                 sees the active camera's view / projection matrices,
	 *                 AND to key the per-camera dispatch-skip cache.
	 * @param viewProjection This camera's current view-projection matrix.
	 *                       Used purely for the dispatch-skip fingerprint;
	 *                       the actual matrix the compute shader reads
	 *                       comes from the GPU frame uniform buffer.
	 * @param frameCache Per-frame cache (frame bind groups live here).
	 * @param lightCount Number of valid entries in the scene light buffer.
	 * @return True if dispatches succeeded OR were skipped as redundant.
	 */
	bool assignLights(
		uint64_t          cameraId,
		const glm::mat4  &viewProjection,
		FrameCache       &frameCache,
		uint32_t          lightCount
	);

	/**
	 * @brief Get the cluster grid storage buffer.
	 * Used by composition pass to look up lights affecting each pixel.
	 * @return Shared pointer to cluster grid buffer.
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUBuffer> getClusterGridBuffer() const { return m_clusterGridBuffer; }

	/// Flat u32 index pool storing the per-cluster light lists.
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUBuffer> getClusterIndicesBuffer() const { return m_clusterIndicesBuffer; }

  private:
	webgpu::WebGPUContext &m_context;

	// GPU cluster storage shared with the composition pass (different bind-group
	// views of the same buffers: read-only for the fragment shader, atomic +
	// read_write for the compute shader).
	std::shared_ptr<webgpu::WebGPUBuffer> m_clusterGridBuffer;	  // {offset, count} per cluster
	std::shared_ptr<webgpu::WebGPUBuffer> m_clusterIndicesBuffer; // Flat u32 index pool

	// Compute pipeline (clear + assign share one pipeline layout). Canonical
	// split: Frame @group(0), Scene-like @group(1) for lights + cluster grid
	// + cluster indices.
	wgpu::ShaderModule    m_computeShaderModule         = nullptr;
	wgpu::BindGroupLayout m_computeFrameBindGroupLayout = nullptr;
	wgpu::BindGroupLayout m_computeBindGroupLayout      = nullptr; ///< Scene-like layout (@group(1)).
	wgpu::PipelineLayout  m_computePipelineLayout       = nullptr;
	wgpu::ComputePipeline m_clearPipeline               = nullptr;
	wgpu::ComputePipeline m_assignPipeline              = nullptr;

	// Per-camera compute bind-group cache. Two bind groups per camera (Frame +
	// Scene-like). Recreated only when the underlying buffer identities
	// change (e.g. SceneLightBuffer reallocates on capacity growth).
	struct CachedComputeBindGroup
	{
		wgpu::BindGroup frameBindGroup = nullptr;
		wgpu::BindGroup sceneBindGroup = nullptr;

		WGPUBuffer frameBuffer = nullptr;
		WGPUBuffer lightBuffer = nullptr;

		uint32_t lastLightCount = 0;
	};
	std::unordered_map<uint64_t, CachedComputeBindGroup> m_computeBindGroups;

	// Per-camera dispatch-skip fingerprint. Skip the cs_clear + cs_assign
	// dispatches when (camera matrix, light data, light count) all match
	// the previous invocation — the cluster grid is a pure function of
	// those inputs, so the previous frame's GPU buffer contents are still
	// the correct result. Common cases this hits: static-camera frames,
	// idle / paused state, multi-camera setups with one scripted view that
	// doesn't move every frame.
	struct DispatchFingerprint
	{
		glm::mat4 viewProjection{};
		uint64_t  lightHash     = 0;
		uint32_t  lightCount    = 0;
		bool      valid         = false;
	};
	std::unordered_map<uint64_t, DispatchFingerprint> m_dispatchFingerprints;

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
	 * @brief Get the cached compute bind groups (Frame + Scene-like) for this
	 *        camera, rebuilding them only when the underlying frame / light
	 *        buffer identities change. Returns pointer to the cache entry on
	 *        success, nullptr on failure.
	 */
	const CachedComputeBindGroup *getOrCreateComputeBindGroups(
		uint64_t cameraId,
		wgpu::Buffer frameBuffer,
		wgpu::Buffer lightBuffer,
		uint32_t lightCount);
};

} // namespace engine::rendering
