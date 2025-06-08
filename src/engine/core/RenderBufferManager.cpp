#include "engine/core/RenderBufferManager.h"
#include <cassert>

namespace engine::core {

RenderBufferManager::RenderBufferManager(size_t bufferCount)
    : m_bufferCount(bufferCount), m_writeIndex(0), m_readIndex(0) {
    m_buffers.resize(bufferCount);
}

RenderState& RenderBufferManager::acquireWriteBuffer() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffers[m_writeIndex % m_bufferCount];
}

void RenderBufferManager::submitWrite() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_readIndex = m_writeIndex;
    m_writeIndex = (m_writeIndex + 1) % m_bufferCount;
}

const RenderState& RenderBufferManager::acquireReadBuffer() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffers[m_readIndex % m_bufferCount];
}

void RenderBufferManager::releaseReadBuffer() {
    // No-op for double buffering, can be extended for triple buffering
}

} // namespace engine::core
