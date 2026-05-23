#include "engine/rendering/SceneLightBuffer.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "engine/lighting/LightManager.h"
#include "engine/rendering/LightUniforms.h"
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
	// Get the light bind group layout from shader registry
	auto layoutInfo = m_context.bindGroupFactory().getGlobalBindGroupLayout(
		engine::rendering::bindgroup::defaults::LIGHT
	);

	if (!layoutInfo)
	{
		spdlog::error("Failed to get light bind group layout from shader registry");
		return;
	}

	// Create bind group (factory will create the storage buffer based on layout)
	m_bindGroup = m_context.bindGroupFactory().createBindGroup(layoutInfo, {}, nullptr, "SceneLightBuffer");

	if (!m_bindGroup)
	{
		spdlog::error("Failed to create scene light bind group");
		return;
	}

	// Get the buffer from the bind group for later access
	if (!m_bindGroup->getBuffers().empty())
	{
		auto buffer = m_bindGroup->getBuffer(0);
		if (buffer)
		{
			m_storageBuffer = buffer->getBuffer();
			m_bufferCapacity = buffer->getSize();
		}
	}

	spdlog::info("SceneLightBuffer initialized successfully");
}

SceneLightBuffer::~SceneLightBuffer()
{
	// The storage buffer handle cached in m_storageBuffer is owned by the
	// WebGPUBuffer instance held inside m_bindGroup. That WebGPUBuffer
	// destructor will call destroy()/release() on the underlying wgpu handle.
	// Releasing it manually here would double-free the wgpu handle.
	m_bindGroup.reset();
	m_storageBuffer = nullptr;
}

void SceneLightBuffer::updateFromManager(const LightManager &lightManager)
{
	updateFromLights(lightManager.getLightData());
}

void SceneLightBuffer::updateFromLights(const std::vector<engine::rendering::LightStruct> &lightData)
{
	if (!m_bindGroup)
	{
		spdlog::warn("Cannot update light buffer - bind group not initialized");
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

	// Write header (light count)
	engine::rendering::LightsBuffer header;
	header.count = lightCount;
	m_bindGroup->updateBuffer(
		0, // binding 0 is the storage buffer
		&header,
		sizeof(engine::rendering::LightsBuffer),
		0, // offset 0
		m_context.getQueue()
	);

	// Write light data if there are lights
	if (!lightData.empty())
	{
		m_bindGroup->updateBuffer(
			0,
			lightData.data(),
			lightCountToUpload * sizeof(engine::rendering::LightStruct),
			sizeof(engine::rendering::LightsBuffer), // offset after header
			m_context.getQueue()
		);
	}

	m_lightCount = lightCount;
	spdlog::debug("SceneLightBuffer updated with {} lights", lightCount);
}

} // namespace engine::lighting
