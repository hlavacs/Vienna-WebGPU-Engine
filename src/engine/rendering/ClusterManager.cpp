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

namespace
{

constexpr const char *CLUSTER_BIND_GROUP_NAME = "ClusterGrid_BindGroup";

} // namespace

ClusterManager::ClusterManager(webgpu::WebGPUContext &context) :
	m_context(context)
{
}

ClusterManager::~ClusterManager()
{
	for (auto &[cameraId, cached] : m_computeBindGroups)
	{
		if (cached.bindGroup)
			cached.bindGroup.release();
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
	m_clusterGridBuffer = bufferFactory.createStorageBufferWrapped(
		"ClusterGrid.OffsetCount",
		0,
		clusterGridBytes
	);
	m_clusterIndicesBuffer = bufferFactory.createStorageBufferWrapped(
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

	// Single layout source of truth: take it from the registered composition
	// shader so the bind group and the pipeline agree on every binding's
	// minBindingSize. Building a separate layout in this manager would have
	// the same names but a different minBindingSize and the validator would
	// reject the bind group at draw time.
	auto shader = m_context.shaderRegistry().getShader(shader::defaults::COMPOSITION_DEFERRED);
	if (!shader)
	{
		spdlog::error("ClusterManager: shader '{}' is not registered", shader::defaults::COMPOSITION_DEFERRED);
		return false;
	}

	auto layoutInfo = shader->getBindGroupLayout(CLUSTER_BIND_GROUP_NAME);
	if (!layoutInfo)
	{
		spdlog::error("ClusterManager: shader does not declare '{}'", CLUSTER_BIND_GROUP_NAME);
		return false;
	}

	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> resources;
	// Group index in the key is ignored by the factory (it only looks at the
	// binding index), so passing 0 keeps the keys consistent and small.
	resources.emplace(std::make_tuple(uint32_t{0}, uint32_t{0}), webgpu::BindGroupResource(m_clusterGridBuffer));
	resources.emplace(std::make_tuple(uint32_t{0}, uint32_t{1}), webgpu::BindGroupResource(m_clusterIndicesBuffer));

	m_clusterBindGroup = m_context.bindGroupFactory().createBindGroup(
		layoutInfo,
		resources,
		nullptr,
		"ClusterGrid.BindGroup"
	);
	if (!m_clusterBindGroup)
	{
		spdlog::error("ClusterManager: failed to create bind group");
		return false;
	}

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
	auto wgslPath = engine::core::PathProvider::getResource("light_clustering.wgsl");
	m_computeShaderModule = m_context.shaderFactory().loadShaderModule(wgslPath);
	if (!m_computeShaderModule)
	{
		spdlog::error("ClusterManager: failed to load light_clustering.wgsl");
		return false;
	}

	// Bind group layout: 4 entries, all compute-only.
	//   0: FrameUniforms (uniform)
	//   1: LightsBuffer  (read-only storage)
	//   2: cluster grid  (read_write storage, written atomically)
	//   3: cluster indices pool (read_write storage)
	std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
	for (auto &e : entries)
	{
		e = wgpu::Default;
	}

	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Compute;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(engine::rendering::FrameUniforms);

	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Compute;
	entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	entries[1].buffer.minBindingSize = 0; // runtime-sized array

	entries[2].binding = 2;
	entries[2].visibility = wgpu::ShaderStage::Compute;
	entries[2].buffer.type = wgpu::BufferBindingType::Storage;
	entries[2].buffer.minBindingSize = 0;

	entries[3].binding = 3;
	entries[3].visibility = wgpu::ShaderStage::Compute;
	entries[3].buffer.type = wgpu::BufferBindingType::Storage;
	entries[3].buffer.minBindingSize = 0;

	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.label = "ClusterCompute.BindGroupLayout";
	layoutDesc.entryCount = static_cast<uint32_t>(entries.size());
	layoutDesc.entries = entries.data();
	m_computeBindGroupLayout = device.createBindGroupLayout(layoutDesc);

	wgpu::PipelineLayoutDescriptor plDesc{};
	plDesc.label = "ClusterCompute.PipelineLayout";
	plDesc.bindGroupLayoutCount = 1;
	plDesc.bindGroupLayouts = reinterpret_cast<WGPUBindGroupLayout *>(&m_computeBindGroupLayout);
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

wgpu::BindGroup ClusterManager::getOrCreateComputeBindGroup(
    uint64_t cameraId,
    wgpu::Buffer frameBuffer,
    wgpu::Buffer lightBuffer,
    uint32_t lightCount)
{
    if (!m_computeBindGroupLayout || !frameBuffer || !lightBuffer
        || !m_clusterGridBuffer || !m_clusterIndicesBuffer)
    {
        return nullptr;
    }

    auto& cached = m_computeBindGroups[cameraId];

    const WGPUBuffer frameRaw = static_cast<WGPUBuffer>(frameBuffer);
    const WGPUBuffer lightRaw = static_cast<WGPUBuffer>(lightBuffer);

    const bool valid =
        cached.bindGroup &&
        cached.frameBuffer == frameRaw &&
        cached.lightBuffer == lightRaw &&
        cached.lastLightCount == lightCount;

    if (valid)
        return cached.bindGroup;

    // IMPORTANT: DO NOT manually release bind groups
    cached.bindGroup = nullptr;

    std::array<wgpu::BindGroupEntry, 4> entries{};

    // -------------------------
    // Frame uniforms
    // -------------------------
    wgpu::BindGroupEntry e0{};
    e0.binding = 0;
    e0.buffer = frameBuffer;
    e0.offset = 0;
    e0.size = sizeof(engine::rendering::FrameUniforms);
    entries[0] = e0;

    // -------------------------
    // Light buffer
    // -------------------------
    wgpu::BindGroupEntry e1{};
    e1.binding = 1;
    e1.buffer = lightBuffer;
    e1.offset = 0;
    e1.size = WGPU_WHOLE_SIZE;
    entries[1] = e1;

    // -------------------------
    // Cluster grid
    // -------------------------
    wgpu::BindGroupEntry e2{};
    e2.binding = 2;
    e2.buffer = m_clusterGridBuffer->getBuffer();
    e2.offset = 0;
    e2.size = WGPU_WHOLE_SIZE;
    entries[2] = e2;

    // -------------------------
    // Cluster indices
    // -------------------------
    wgpu::BindGroupEntry e3{};
    e3.binding = 3;
    e3.buffer = m_clusterIndicesBuffer->getBuffer();
    e3.offset = 0;
    e3.size = WGPU_WHOLE_SIZE;
    entries[3] = e3;

    wgpu::BindGroupDescriptor desc{};
    desc.label = "ClusterCompute.BindGroup";
    desc.layout = m_computeBindGroupLayout;
    desc.entryCount = 4;
    desc.entries = entries.data();

    wgpu::BindGroup newBindGroup =
        m_context.getDevice().createBindGroup(desc);

    if (!newBindGroup)
    {
        spdlog::error("ClusterManager: failed to create bind group");
        return nullptr;
    }

    cached.bindGroup = newBindGroup;
    cached.frameBuffer = frameRaw;
    cached.lightBuffer = lightRaw;
    cached.lastLightCount = lightCount;

    return cached.bindGroup;
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

	wgpu::BindGroup bindGroup = getOrCreateComputeBindGroup(cameraId, frameBuffer, lightBuffer, lightCount);
	if (!bindGroup)
	{
		spdlog::warn("ClusterManager::assignLights: failed to build compute bind group");
		return false;
	}

	auto encoder = m_context.createCommandEncoder("ClusterCompute.Encoder");
	if (auto *prof = m_context.frameProfiler())
		prof->beginGpuScope("Pass.ClusterCompute", encoder);
	{
		wgpu::ComputePassDescriptor passDesc{};
		passDesc.label = "ClusterCompute.Pass";
		wgpu::ComputePassEncoder pass = encoder.beginComputePass(passDesc);

		// Phase 1: clear every cluster's count + seed its fixed offset slot.
		pass.setPipeline(m_clearPipeline);
		pass.setBindGroup(0, bindGroup, 0, nullptr);
		constexpr uint32_t clearWorkgroupSize = 64;
		const uint32_t clearGroups = (CLUSTER_GRID_TOTAL + clearWorkgroupSize - 1) / clearWorkgroupSize;
		pass.dispatchWorkgroups(clearGroups, 1, 1);

		// Phase 2: per-light assignment. Same bind group (only the entry point changes).
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
