#include "engine/rendering/ClusterManager.h"

#include <array>
#include <map>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/BindGroupEnums.h"
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
	auto device = m_context.getDevice();

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
	std::array<wgpu::BindGroupLayoutEntry, 1> frameEntries{};
	frameEntries[0] = wgpu::Default;
	frameEntries[0].binding             = 0;
	frameEntries[0].visibility          = wgpu::ShaderStage::Compute;
	frameEntries[0].buffer.type         = wgpu::BufferBindingType::Uniform;
	frameEntries[0].buffer.minBindingSize = sizeof(engine::rendering::FrameUniforms);

	wgpu::BindGroupLayoutDescriptor frameLayoutDesc{};
	frameLayoutDesc.label      = "ClusterCompute.FrameLayout";
	frameLayoutDesc.entryCount = static_cast<uint32_t>(frameEntries.size());
	frameLayoutDesc.entries    = frameEntries.data();
	m_computeFrameBindGroupLayout = device.createBindGroupLayout(frameLayoutDesc);

	// Scene @group(1) @binding(0..2): lights / cluster grid / cluster indices
	std::array<wgpu::BindGroupLayoutEntry, 3> sceneEntries{};
	for (auto &e : sceneEntries) e = wgpu::Default;

	sceneEntries[0].binding             = 0;
	sceneEntries[0].visibility          = wgpu::ShaderStage::Compute;
	sceneEntries[0].buffer.type         = wgpu::BufferBindingType::ReadOnlyStorage;
	sceneEntries[0].buffer.minBindingSize = 0; // runtime-sized array

	sceneEntries[1].binding             = 1;
	sceneEntries[1].visibility          = wgpu::ShaderStage::Compute;
	sceneEntries[1].buffer.type         = wgpu::BufferBindingType::Storage;
	sceneEntries[1].buffer.minBindingSize = 0;

	sceneEntries[2].binding             = 2;
	sceneEntries[2].visibility          = wgpu::ShaderStage::Compute;
	sceneEntries[2].buffer.type         = wgpu::BufferBindingType::Storage;
	sceneEntries[2].buffer.minBindingSize = 0;

	wgpu::BindGroupLayoutDescriptor sceneLayoutDesc{};
	sceneLayoutDesc.label      = "ClusterCompute.SceneLayout";
	sceneLayoutDesc.entryCount = static_cast<uint32_t>(sceneEntries.size());
	sceneLayoutDesc.entries    = sceneEntries.data();
	m_computeBindGroupLayout = device.createBindGroupLayout(sceneLayoutDesc);

	std::array<WGPUBindGroupLayout, 2> pipelineLayouts{
		static_cast<WGPUBindGroupLayout>(m_computeFrameBindGroupLayout),
		static_cast<WGPUBindGroupLayout>(m_computeBindGroupLayout),
	};
	wgpu::PipelineLayoutDescriptor plDesc{};
	plDesc.label                = "ClusterCompute.PipelineLayout";
	plDesc.bindGroupLayoutCount = static_cast<uint32_t>(pipelineLayouts.size());
	plDesc.bindGroupLayouts     = pipelineLayouts.data();
	m_computePipelineLayout = device.createPipelineLayout(plDesc);

	auto makePipeline = [&](const char *label, const char *entryPoint) -> wgpu::ComputePipeline
	{
		wgpu::ComputePipelineDescriptor desc{};
		desc.label = label;
		desc.layout = m_computePipelineLayout;
		desc.compute.module = m_computeShaderModule;
		desc.compute.entryPoint = entryPoint;
		desc.compute.constantCount = 0;
		desc.compute.constants = nullptr;
		return device.createComputePipeline(desc);
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

		wgpu::BindGroupDescriptor desc{};
		desc.label      = "ClusterCompute.Frame.BindGroup";
		desc.layout     = m_computeFrameBindGroupLayout;
		desc.entryCount = 1;
		desc.entries    = &e;
		cached.frameBindGroup = m_context.getDevice().createBindGroup(desc);
		if (!cached.frameBindGroup)
		{
			spdlog::error("ClusterManager: failed to create frame bind group");
			return nullptr;
		}
	}

	// Scene-like @group(1): lights @0, cluster grid @1, cluster indices @2
	{
		std::array<wgpu::BindGroupEntry, 3> entries{};

		entries[0]         = {};
		entries[0].binding = 0;
		entries[0].buffer  = lightBuffer;
		entries[0].offset  = 0;
		entries[0].size    = WGPU_WHOLE_SIZE;

		entries[1]         = {};
		entries[1].binding = 1;
		entries[1].buffer  = m_clusterGridBuffer->getBuffer();
		entries[1].offset  = 0;
		entries[1].size    = WGPU_WHOLE_SIZE;

		entries[2]         = {};
		entries[2].binding = 2;
		entries[2].buffer  = m_clusterIndicesBuffer->getBuffer();
		entries[2].offset  = 0;
		entries[2].size    = WGPU_WHOLE_SIZE;

		wgpu::BindGroupDescriptor desc{};
		desc.label      = "ClusterCompute.Scene.BindGroup";
		desc.layout     = m_computeBindGroupLayout;
		desc.entryCount = static_cast<uint32_t>(entries.size());
		desc.entries    = entries.data();
		cached.sceneBindGroup = m_context.getDevice().createBindGroup(desc);
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

bool ClusterManager::assignLights(uint64_t cameraId, FrameCache &frameCache, uint32_t lightCount)
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

	// bindGroup.release();
	return true;
}

bool ClusterManager::clearClusters()
{
	// Clear is folded into assignLights' first dispatch - keep the entry for
	// API compatibility but it's no longer the caller's responsibility.
	return true;
}

} // namespace engine::rendering
