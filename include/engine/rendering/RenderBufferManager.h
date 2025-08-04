#pragma once
#include "engine/rendering/RenderState.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace engine::rendering
{

class RenderBufferManager
{
  public:
	explicit RenderBufferManager(size_t bufferCount = 2);

	RenderState &acquireWriteBuffer();
	void submitWrite();
	const RenderState &acquireReadBuffer();
	void releaseReadBuffer();

  private:
	std::vector<RenderState> m_buffers;
	size_t m_bufferCount;
	std::atomic<size_t> m_writeIndex;
	std::atomic<size_t> m_readIndex;
	std::mutex m_mutex;
};

} // namespace engine::rendering
