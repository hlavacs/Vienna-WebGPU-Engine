#include "engine/rendering/SceneLightBuffer.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "engine/lighting/LightManager.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::lighting
{

SceneLightBuffer::SceneLightBuffer(engine::rendering::webgpu::WebGPUContext &context) :
	m_context(context),
	m_lightCount(0)
{
	// Owns the lights storage buffer directly. The Scene bind group (built by
	// Renderer::updateSceneBindGroup) references this buffer as its
	// @binding(0); SceneLightBuffer itself no longer maintains a bind group
	// since the per-shader bindings are owned by Scene's consolidated layout.
	const size_t headerSize     = sizeof(engine::rendering::LightsBuffer);
	const size_t lightArraySize = engine::rendering::constants::MAX_LIGHTS * sizeof(engine::rendering::LightStruct);
	const size_t totalSize      = headerSize + lightArraySize;

	m_bufferWrapped = m_context.bufferFactory().createStorageBuffer(
		"SceneLights",
		0,
		totalSize
	);
	if (!m_bufferWrapped)
	{
		spdlog::error("Failed to create SceneLightBuffer storage buffer");
		return;
	}
	m_storageBuffer  = m_bufferWrapped->getBuffer();
	m_bufferCapacity = totalSize;

	spdlog::info("SceneLightBuffer initialized successfully");
}

SceneLightBuffer::~SceneLightBuffer()
{
	// The storage buffer handle cached in m_storageBuffer is owned by the
	// WebGPUBuffer instance held in m_bufferWrapped. Its destructor calls
	// destroy()/release() on the underlying wgpu handle — manual release
	// here would double-free.
	m_bufferWrapped.reset();
	m_storageBuffer = nullptr;
}

void SceneLightBuffer::updateFromManager(const LightManager &lightManager)
{
	updateFromLights(lightManager.getLightData());
}

void SceneLightBuffer::updateFromLights(const std::vector<engine::rendering::LightStruct> &lightData)
{
	if (!m_bufferWrapped)
	{
		spdlog::warn("Cannot update light buffer - storage buffer not initialized");
		return;
	}

	const size_t headerSize = sizeof(engine::rendering::LightsBuffer);
	const size_t maxLightBytes = m_bufferCapacity > headerSize ? m_bufferCapacity - headerSize : 0;
	const size_t maxLightCount = maxLightBytes / sizeof(engine::rendering::LightStruct);
	const size_t lightCountToUpload = std::min(lightData.size(), maxLightCount);
	if (lightData.size() > maxLightCount)
	{
		spdlog::warn(
			"SceneLightBuffer truncating {} lights to {} due to GPU buffer capacity",
			lightData.size(),
			maxLightCount
		);
	}

	uint32_t lightCount = static_cast<uint32_t>(lightCountToUpload);

	auto &queue = m_context.getQueue();
	engine::rendering::LightsBuffer header;
	header.count = lightCount;
	queue.writeBuffer(m_storageBuffer, 0, &header, sizeof(header));

	if (!lightData.empty())
	{
		queue.writeBuffer(
			m_storageBuffer,
			sizeof(header),
			lightData.data(),
			lightCountToUpload * sizeof(engine::rendering::LightStruct)
		);
	}

	m_lightCount = lightCount;
	spdlog::debug("SceneLightBuffer updated with {} lights", lightCount);
}

} // namespace engine::lighting
