#include "engine/rendering/SceneLightBuffer.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "engine/lighting/LightManager.h"
#include "engine/rendering/CacheStats.h"
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

namespace
{
/// 64-bit FNV-1a over 8-byte blocks of @p data plus the leading @p count.
/// Cheap "did anything change?" fingerprint — false positives (rare hash
/// collisions) cost an extra writeBuffer, false negatives (skipped writes
/// when data did change) would be visible as stale lights, which the
/// 64-bit hash space makes astronomically unlikely.
uint64_t fingerprintLights(uint32_t count, const engine::rendering::LightStruct *data, size_t byteCount)
{
	constexpr uint64_t kOffset = 0xcbf29ce484222325ull;
	constexpr uint64_t kPrime  = 0x100000001b3ull;

	uint64_t h = kOffset;
	h ^= static_cast<uint64_t>(count);
	h *= kPrime;

	if (count == 0 || byteCount == 0) return h;

	const auto *blocks = reinterpret_cast<const uint64_t *>(data);
	const size_t blockCount = byteCount / sizeof(uint64_t);
	for (size_t i = 0; i < blockCount; ++i)
	{
		h ^= blocks[i];
		h *= kPrime;
	}
	// Trailing bytes (LightStruct is 16-byte-aligned in practice, but be
	// defensive in case the layout changes).
	const auto *tail = reinterpret_cast<const uint8_t *>(data) + (blockCount * sizeof(uint64_t));
	const size_t tailCount = byteCount - (blockCount * sizeof(uint64_t));
	for (size_t i = 0; i < tailCount; ++i)
	{
		h ^= tail[i];
		h *= kPrime;
	}
	return h;
}
} // namespace

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

	// Fingerprint check: skip the upload when the payload is byte-identical
	// to the previous frame's. Common case for static scenes; helps even on
	// animated scenes where only some lights move (the cost is the hash
	// itself — ~10 µs for 1000 lights, much less than the ~64 KB queue
	// submission it replaces).
	const size_t lightBytes = lightCountToUpload * sizeof(engine::rendering::LightStruct);
	const uint64_t hash = fingerprintLights(lightCount, lightData.data(), lightBytes);
	if (m_lastUploadValid && m_lastUploadHash == hash && m_lightCount == lightCount)
	{
		engine::rendering::CacheStats::sceneLightUploadsSkipped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	engine::rendering::CacheStats::sceneLightUploadsExecuted.fetch_add(1, std::memory_order_relaxed);

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
			lightBytes
		);
	}

	m_lightCount      = lightCount;
	m_lastUploadHash  = hash;
	m_lastUploadValid = true;
	spdlog::debug("SceneLightBuffer updated with {} lights", lightCount);
}

} // namespace engine::lighting
