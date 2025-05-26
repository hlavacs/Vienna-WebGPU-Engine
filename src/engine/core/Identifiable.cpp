#include "engine/core/Identifiable.h"

namespace engine::core {

// Atomic global counter for all IDs
static std::atomic<uint64_t> s_counter{1};

uint64_t IDGenerator::nextID() {
    return s_counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace engine::core
