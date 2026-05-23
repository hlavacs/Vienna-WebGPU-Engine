#include "engine/rendering/ClusterManager.h"

#include <map>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"

namespace engine::rendering
{

ClusterManager::ClusterManager(webgpu::WebGPUContext &context) :
m_context(context)
{
}

bool ClusterManager::initialize()
{
spdlog::info("Initializing ClusterManager");

if (!createClusterGridBuffer())
{
spdlog::error("Failed to create cluster grid buffer");
return false;
}

if (!createComputePipeline())
{
spdlog::error("Failed to create compute pipeline");
return false;
}

spdlog::info("ClusterManager initialized successfully");
return true;
}

bool ClusterManager::createClusterGridBuffer()
{
	// New structure: 
	// - Each cluster stores offset and count (8 bytes per cluster)
	// - Separate flat buffer stores all light indices (4 bytes per index)
	// - Max: 10,752 clusters * 256 lights = 2,752,512 indices max

	const size_t clusterStructSize = 2 * sizeof(uint32_t);  // offset + count = 8 bytes
	const size_t clusterGridSize = CLUSTER_GRID_TOTAL * clusterStructSize;  // ~86 KB
	const size_t maxLightIndices = CLUSTER_GRID_TOTAL * MAX_LIGHTS_PER_CLUSTER;

	spdlog::info("Creating cluster grid buffer: {} clusters ({} KB), max light indices: {} ({} MB)",
		CLUSTER_GRID_TOTAL, clusterGridSize / 1024, maxLightIndices, maxLightIndices * sizeof(uint32_t) / (1024 * 1024));

	auto &bufferFactory = m_context.bufferFactory();
	auto device = m_context.getDevice();

	// Create cluster grid storage buffer (small buffer with offset and count per cluster)
	// Initialize with zeros (offset=0, count=0 for all clusters)
	auto gridRawBuffer = bufferFactory.createStorageBuffer(clusterGridSize);
	if (!gridRawBuffer)
	{
		spdlog::error("Failed to create GPU cluster grid buffer");
		return false;
	}
	// Zero-initialize the buffer
	std::vector<uint32_t> zeroClustersData(CLUSTER_GRID_TOTAL * 2, 0);
	device.getQueue().writeBuffer(gridRawBuffer, 0, zeroClustersData.data(), clusterGridSize);
	
	m_clusterGridBuffer = std::make_shared<webgpu::WebGPUBuffer>(
		gridRawBuffer,
		"ClusterGrid_OffsetCountBuffer",
		0,
		clusterGridSize,
		static_cast<WGPUBufferUsageFlags>(wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst)
	);

	// For now, create a dummy light indices buffer (will be populated by compute shader later)
	// Start with a minimal buffer for compatibility (will be resized by compute shader)
	const size_t minIndicesSize = 1024 * sizeof(uint32_t);
	auto indicesRawBuffer = bufferFactory.createStorageBuffer(minIndicesSize);
	if (!indicesRawBuffer)
	{
		spdlog::error("Failed to create GPU cluster light indices buffer");
		return false;
	}
	// Initialize with dummy values
	std::vector<uint32_t> dummyIndices(1024, 0xFFFFFFFF);
	device.getQueue().writeBuffer(indicesRawBuffer, 0, dummyIndices.data(), minIndicesSize);
	
	m_clusterIndicesBuffer = std::make_shared<webgpu::WebGPUBuffer>(
		indicesRawBuffer,
		"ClusterLightIndices_Buffer",
		1,
		minIndicesSize,
		static_cast<WGPUBufferUsageFlags>(wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst)
	);

	// Create bind group with both buffers
	std::vector<wgpu::BindGroupLayoutEntry> layoutEntries(2);
	layoutEntries[0].binding = 0;
	layoutEntries[0].visibility = wgpu::ShaderStage::Fragment;
	layoutEntries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

	layoutEntries[1].binding = 1;
	layoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
	layoutEntries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

	std::vector<webgpu::BindGroupBinding> bindings(2);
	bindings[0].bindingIndex = 0;
	bindings[0].name = "uClusterGrid";
	bindings[0].type = BindingType::StorageBuffer;
	bindings[0].visibility = wgpu::ShaderStage::Fragment;
	bindings[0].size = clusterGridSize;

	bindings[1].bindingIndex = 1;
	bindings[1].name = "uClusterLightIndices";
	bindings[1].type = BindingType::StorageBuffer;
	bindings[1].visibility = wgpu::ShaderStage::Fragment;
	bindings[1].size = minIndicesSize;

	auto layoutInfo = m_context.bindGroupFactory().createBindGroupLayoutInfo(
		"ClusterGrid_BindGroup",
		BindGroupType::Custom,
		BindGroupReuse::PerFrame,
		layoutEntries,
		bindings
	);

	if (!layoutInfo)
	{
		spdlog::error("Failed to create cluster bind group layout");
		return false;
	}

	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> resources;
	resources.emplace(std::make_tuple(0u, 0u), webgpu::BindGroupResource(m_clusterGridBuffer));
	resources.emplace(std::make_tuple(0u, 1u), webgpu::BindGroupResource(m_clusterIndicesBuffer));
	m_clusterBindGroup = m_context.bindGroupFactory().createBindGroup(layoutInfo, resources, nullptr, "ClusterGrid_BindGroup");
	if (!m_clusterBindGroup)
	{
		spdlog::error("Failed to create cluster bind group");
		return false;
	}

	spdlog::info("Cluster grid buffer and bind group created successfully");
	return true;
}

bool ClusterManager::createComputePipeline()
{
// For now, we're not implementing the compute shader
// This is a placeholder; clustering will be updated later
spdlog::info("ClusterManager compute pipeline: placeholder implementation");
return true;
}

bool ClusterManager::assignLights(FrameCache &frameCache, const std::shared_ptr<webgpu::WebGPUBindGroup> &sceneLightBindGroup)
{
// For now, this is a placeholder
// The actual compute shader dispatch will be implemented later
return true;
}

bool ClusterManager::clearClusters()
{
// For now, this is a placeholder
// The actual clearing logic will be implemented later
return true;
}

} // namespace engine::rendering
