#include "engine/rendering/ClusterManager.h"

#include <array>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/CacheStats.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/SceneLightBuffer.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{


ClusterManager::ClusterManager(webgpu::WebGPUContext &context) :
	m_context(context)
{
}

ClusterManager::~ClusterManager()
{
	for (auto &[cameraId, cached] : m_computeBindGroups)
	{
		if (cached.frameBindGroup)
			cached.frameBindGroup.release();
		if (cached.sceneBindGroup)
			cached.sceneBindGroup.release();
	}
}

bool ClusterManager::initialize()
{
	spdlog::info("Initializing ClusterManager ({}x{}x{} = {} clusters)", CLUSTER_GRID_DIM_X, CLUSTER_GRID_DIM_Y, CLUSTER_GRID_DIM_Z, CLUSTER_GRID_TOTAL);

	if (!createClusterGridBuffer())
	{
		spdlog::error("ClusterManager: failed to create cluster grid buffer");
		return false;
	}

	if (!createComputePipeline())
	{
		spdlog::error("ClusterManager: failed to create compute pipeline");
		return false;
	}

	spdlog::info("ClusterManager initialized successfully");
	return true;
}

bool ClusterManager::createClusterGridBuffer()
{
	const size_t clusterStructBytes = 2 * sizeof(uint32_t); // offset + count
	const size_t clusterGridBytes = CLUSTER_GRID_TOTAL * clusterStructBytes;
	const size_t maxLightIndices = CLUSTER_GRID_TOTAL * MAX_LIGHTS_PER_CLUSTER;
	const size_t lightIndicesBytes = maxLightIndices * sizeof(uint32_t);

	spdlog::info(
		"Cluster buffers: grid {} KB, light-indices {} MB",
		clusterGridBytes / 1024,
		lightIndicesBytes / (1024 * 1024)
	);

	auto &bufferFactory = m_context.bufferFactory();
	auto queue = m_context.getQueue();

	// Wrapped buffers: factory owns the wgpu::Buffer lifetime via WebGPUBuffer,
	// which destroys+releases on shared_ptr expiry. No manual cleanup needed here.
	m_clusterGridBuffer = bufferFactory.createStorageBuffer(
		"ClusterGrid.OffsetCount",
		0,
		clusterGridBytes
	);
	m_clusterIndicesBuffer = bufferFactory.createStorageBuffer(
		"ClusterGrid.LightIndices",
		1,
		lightIndicesBytes
	);
	if (!m_clusterGridBuffer || !m_clusterIndicesBuffer)
	{
		spdlog::error("ClusterManager: failed to allocate storage buffers");
		return false;
	}

	// Zero-init so the composition pass's "no lights in cluster" branch reads
	// well-defined zero counts even before any compute dispatch has run.
	const std::vector<uint32_t> zeroGrid(CLUSTER_GRID_TOTAL * 2, 0);
	queue.writeBuffer(m_clusterGridBuffer->getBuffer(), 0, zeroGrid.data(), clusterGridBytes);

	// Renderer::updateSceneBindGroup reads cluster buffers directly via the
	// getClusterGridBuffer / getClusterIndicesBuffer accessors and binds them
	// into the consolidated Scene group at @binding(8/9). Nothing else to do.
	return true;
}

bool ClusterManager::createComputePipeline()
{
	// Load the compute shader module via the shader factory's low-level helper.
	// We don't go through WebGPUShaderFactory::begin().build() because that path
	// is wired for render pipelines (vertex + fragment entry points, color
	// targets). Compute pipelines need their own minimal setup; once a real
	// compute path lands in WebGPUPipelineManager we can migrate.
	auto wgslPath = engine::core::PathProvider::getResource(shader::resource_paths::CLUSTER_COMPUTE);
	m_computeShaderModule = m_context.shaderFactory().loadShaderModule(wgslPath);
	if (!m_computeShaderModule)
	{
		spdlog::error("ClusterManager: failed to load light_clustering.wgsl");
		return false;
	}

	// Two compute-only bind group layouts, mirroring the canonical render
	// convention (Frame @group(0), Scene @group(1)). The Scene-side layout
	// is a 3-entry SUBSET that exposes only the compute path needs (lights
	// read-only, cluster grid + index pool read_write with atomics).
	//
	// Frame @group(0) @binding(0): FrameUniforms uniform
	std::vector<wgpu::BindGroupLayoutEntry> frameEntries(1);
	frameEntries[0] = wgpu::Default;
	frameEntries[0].binding               = 0;
	frameEntries[0].visibility            = wgpu::ShaderStage::Compute;
	frameEntries[0].buffer.type           = wgpu::BufferBindingType::Uniform;
	frameEntries[0].buffer.minBindingSize = sizeof(engine::rendering::FrameUniforms);
	m_computeFrameBindGroupLayout = m_context.bindGroupFactory().createBindGroupLayout(
		frameEntries, "ClusterCompute.FrameLayout");

	// Scene @group(1) @binding(0..2): lights / cluster grid / cluster indices
	std::vector<wgpu::BindGroupLayoutEntry> sceneEntries(3);
	for (auto &e : sceneEntries) e = wgpu::Default;

	sceneEntries[0].binding               = 0;
	sceneEntries[0].visibility            = wgpu::ShaderStage::Compute;
	sceneEntries[0].buffer.type           = wgpu::BufferBindingType::ReadOnlyStorage;
	sceneEntries[0].buffer.minBindingSize = 0; // runtime-sized array

	sceneEntries[1].binding               = 1;
	sceneEntries[1].visibility            = wgpu::ShaderStage::Compute;
	sceneEntries[1].buffer.type           = wgpu::BufferBindingType::Storage;
	sceneEntries[1].buffer.minBindingSize = 0;

	sceneEntries[2].binding               = 2;
	sceneEntries[2].visibility            = wgpu::ShaderStage::Compute;
	sceneEntries[2].buffer.type           = wgpu::BufferBindingType::Storage;
	sceneEntries[2].buffer.minBindingSize = 0;

	m_computeBindGroupLayout = m_context.bindGroupFactory().createBindGroupLayout(
		sceneEntries, "ClusterCompute.SceneLayout");

	std::array<wgpu::BindGroupLayout, 2> pipelineLayouts{
		m_computeFrameBindGroupLayout,
		m_computeBindGroupLayout,
	};
	m_computePipelineLayout = m_context.pipelineFactory().createPipelineLayout(
		pipelineLayouts.data(), static_cast<uint32_t>(pipelineLayouts.size()));

	auto makePipeline = [&](const char *label, const char *entryPoint) -> wgpu::ComputePipeline
	{
		return m_context.pipelineFactory().createComputePipeline(
			m_computePipelineLayout, m_computeShaderModule, entryPoint, label);
	};

	m_clearPipeline = makePipeline("ClusterCompute.Clear", "cs_clear");
	m_assignPipeline = makePipeline("ClusterCompute.Assign", "cs_assign");
	if (!m_clearPipeline || !m_assignPipeline)
	{
		spdlog::error("ClusterManager: failed to create compute pipelines");
		return false;
	}
	return true;
}

const ClusterManager::CachedComputeBindGroup *ClusterManager::getOrCreateComputeBindGroups(
	uint64_t cameraId,
	wgpu::Buffer frameBuffer,
	wgpu::Buffer lightBuffer,
	uint32_t lightCount)
{
	if (!m_computeFrameBindGroupLayout || !m_computeBindGroupLayout
		|| !frameBuffer || !lightBuffer
		|| !m_clusterGridBuffer || !m_clusterIndicesBuffer)
	{
		return nullptr;
	}

	auto &cached = m_computeBindGroups[cameraId];

	const WGPUBuffer frameRaw = static_cast<WGPUBuffer>(frameBuffer);
	const WGPUBuffer lightRaw = static_cast<WGPUBuffer>(lightBuffer);

	const bool valid =
		cached.frameBindGroup &&
		cached.sceneBindGroup &&
		cached.frameBuffer == frameRaw &&
		cached.lightBuffer == lightRaw &&
		cached.lastLightCount == lightCount;

	if (valid)
		return &cached;

	cached.frameBindGroup = nullptr;
	cached.sceneBindGroup = nullptr;

	// Frame @group(0) @binding(0)
	{
		wgpu::BindGroupEntry e{};
		e.binding = 0;
		e.buffer  = frameBuffer;
		e.offset  = 0;
		e.size    = sizeof(engine::rendering::FrameUniforms);

		cached.frameBindGroup = m_context.bindGroupFactory().createBindGroup(
			m_computeFrameBindGroupLayout, std::vector<wgpu::BindGroupEntry>{e});
		if (!cached.frameBindGroup)
		{
			spdlog::error("ClusterManager: failed to create frame bind group");
			return nullptr;
		}
	}

	// Scene-like @group(1): lights @0, cluster grid @1, cluster indices @2
	{
		std::vector<wgpu::BindGroupEntry> entries(3);

		entries[0].binding = 0;
		entries[0].buffer  = lightBuffer;
		entries[0].offset  = 0;
		entries[0].size    = WGPU_WHOLE_SIZE;

		entries[1].binding = 1;
		entries[1].buffer  = m_clusterGridBuffer->getBuffer();
		entries[1].offset  = 0;
		entries[1].size    = WGPU_WHOLE_SIZE;

		entries[2].binding = 2;
		entries[2].buffer  = m_clusterIndicesBuffer->getBuffer();
		entries[2].offset  = 0;
		entries[2].size    = WGPU_WHOLE_SIZE;

		cached.sceneBindGroup = m_context.bindGroupFactory().createBindGroup(
			m_computeBindGroupLayout, entries);
		if (!cached.sceneBindGroup)
		{
			spdlog::error("ClusterManager: failed to create scene bind group");
			return nullptr;
		}
	}

	cached.frameBuffer    = frameRaw;
	cached.lightBuffer    = lightRaw;
	cached.lastLightCount = lightCount;
	return &cached;
}

bool ClusterManager::assignLights(
	uint64_t          cameraId,
	const glm::mat4  &viewProjection,
	FrameCache       &frameCache,
	uint32_t          lightCount
)
{
	if (!m_clearPipeline || !m_assignPipeline)
	{
		spdlog::warn("ClusterManager::assignLights: compute pipelines not initialised");
		return false;
	}

	// Frame uniforms live in a per-camera bind group built by Renderer; pull
	// the underlying buffer back out so the compute shader can read view /
	// projection matrices for THIS camera.
	auto frameBgIt = frameCache.frameBindGroupCache.find(cameraId);
	if (frameBgIt == frameCache.frameBindGroupCache.end() || !frameBgIt->second
		|| frameBgIt->second->getBufferCount() == 0)
	{
		spdlog::warn("ClusterManager::assignLights: no frame bind group for camera {}", cameraId);
		return false;
	}
	wgpu::Buffer frameBuffer = frameBgIt->second->getBuffer(0)->getBuffer();

	auto sceneLightBuffer = m_context.sceneLightBuffer();
	if (!sceneLightBuffer)
	{
		spdlog::warn("ClusterManager::assignLights: scene light buffer missing");
		return false;
	}
	wgpu::Buffer lightBuffer = sceneLightBuffer->getStorageBuffer();
	if (!lightBuffer)
	{
		spdlog::warn("ClusterManager::assignLights: light storage buffer is null");
		return false;
	}

	const auto *cached = getOrCreateComputeBindGroups(cameraId, frameBuffer, lightBuffer, lightCount);
	if (!cached)
	{
		spdlog::warn("ClusterManager::assignLights: failed to build compute bind groups");
		return false;
	}

	// Dispatch-skip fingerprint. Cluster grid output is a pure function of
	// (viewProjection, light hash, light count); when all three match the
	// previous invocation for this camera, the GPU buffer already holds the
	// correct result and we elide both dispatches. Light hash comes from
	// SceneLightBuffer's upload-time fingerprint — same hash means the GPU
	// light buffer's contents are byte-identical to last frame's.
	auto       &fingerprint = m_dispatchFingerprints[cameraId];
	const auto  currentHash = sceneLightBuffer->getLastUploadHash();
	if (fingerprint.valid
		&& fingerprint.lightCount    == lightCount
		&& fingerprint.lightHash     == currentHash
		&& fingerprint.viewProjection == viewProjection)
	{
		CacheStats::clusterDispatchesSkipped.fetch_add(1, std::memory_order_relaxed);
		return true;
	}
	fingerprint.viewProjection = viewProjection;
	fingerprint.lightHash      = currentHash;
	fingerprint.lightCount     = lightCount;
	fingerprint.valid          = true;
	CacheStats::clusterDispatchesExecuted.fetch_add(1, std::memory_order_relaxed);

	auto encoder = m_context.createCommandEncoder("ClusterCompute.Encoder");
	if (auto *prof = m_context.frameProfiler())
		prof->beginGpuScope("Pass.ClusterCompute", encoder);
	{
		wgpu::ComputePassDescriptor passDesc{};
		passDesc.label = "ClusterCompute.Pass";
		wgpu::ComputePassEncoder pass = encoder.beginComputePass(passDesc);

		// Frame @group(0) + Scene-style compute view @group(1).
		pass.setBindGroup(0, cached->frameBindGroup, 0, nullptr);
		pass.setBindGroup(1, cached->sceneBindGroup, 0, nullptr);

		// Phase 1: clear every cluster's count + seed its fixed offset slot.
		pass.setPipeline(m_clearPipeline);
		constexpr uint32_t clearWorkgroupSize = 64;
		const uint32_t clearGroups = (CLUSTER_GRID_TOTAL + clearWorkgroupSize - 1) / clearWorkgroupSize;
		pass.dispatchWorkgroups(clearGroups, 1, 1);

		// Phase 2: per-light assignment. Same bind groups (only entry point changes).
		if (lightCount > 0)
		{
			pass.setPipeline(m_assignPipeline);
			constexpr uint32_t assignWorkgroupSize = 64;
			const uint32_t assignGroups = (lightCount + assignWorkgroupSize - 1) / assignWorkgroupSize;
			pass.dispatchWorkgroups(assignGroups, 1, 1);
		}

		pass.end();
	}
	if (auto *prof = m_context.frameProfiler())
		prof->endGpuScope("Pass.ClusterCompute", encoder);
	m_context.submitCommandEncoder(encoder, "ClusterCompute.Commands");

	return true;
}

} // namespace engine::rendering
