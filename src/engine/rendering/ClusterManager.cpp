#include "engine/rendering/ClusterManager.h"

#include <map>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
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

bool ClusterManager::initialize()
{
	spdlog::info("Initializing ClusterManager ({}x{}x{} = {} clusters)",
		CLUSTER_GRID_DIM_X, CLUSTER_GRID_DIM_Y, CLUSTER_GRID_DIM_Z, CLUSTER_GRID_TOTAL);

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
	// Placeholder: clustering compute shader is not wired up yet. The
	// composition shader falls back to scanning all scene lights per pixel
	// when every cluster's count is zero, which is what the zero-initialised
	// grid buffer produces.
	return true;
}

bool ClusterManager::assignLights(FrameCache & /*frameCache*/, const std::shared_ptr<webgpu::WebGPUBindGroup> & /*sceneLightBindGroup*/)
{
	// Placeholder - the compute pass isn't implemented. The composition pass
	// will use its fallback "all lights" branch in the meantime.
	return true;
}

bool ClusterManager::clearClusters()
{
	// Placeholder - no clustering means nothing to clear.
	return true;
}

} // namespace engine::rendering
