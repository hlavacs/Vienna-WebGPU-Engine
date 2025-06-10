#include "engine/rendering/RenderBufferManager.h"
#include <cassert>

namespace engine::rendering
{

	RenderBufferManager::RenderBufferManager(size_t bufferCount)
		: m_bufferCount(bufferCount), m_writeIndex(0), m_readIndex(0)
	{
		m_buffers.resize(bufferCount);
	}

	RenderState &RenderBufferManager::acquireWriteBuffer()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_buffers[m_writeIndex % m_bufferCount];
	}

	void RenderBufferManager::submitWrite()
	{
		size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
		size_t nextWrite = (currentWrite + 1) % m_bufferCount;

		m_readIndex.store(currentWrite, std::memory_order_release);
		m_writeIndex.store(nextWrite, std::memory_order_release);
	}

	const RenderState &RenderBufferManager::acquireReadBuffer()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_buffers[m_readIndex % m_bufferCount];
	}

	void RenderBufferManager::releaseReadBuffer()
	{
		// No-op for double buffering, can be extended for triple buffering
	}

} // namespace engine::rendering
