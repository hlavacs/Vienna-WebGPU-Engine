#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include "engine/core/RenderState.h"

namespace engine::core {

class RenderBufferManager {
public:
    explicit RenderBufferManager(size_t bufferCount = 2);

    RenderState& acquireWriteBuffer();
    void submitWrite();
    const RenderState& acquireReadBuffer();
    void releaseReadBuffer();

private:
    std::vector<RenderState> m_buffers;
    size_t m_bufferCount;
    std::atomic<size_t> m_writeIndex;
    std::atomic<size_t> m_readIndex;
    std::mutex m_mutex;
};

} // namespace engine::core
