#pragma once

#include <atomic>
#include <cstdint>

namespace engine::core
{
    /**
     * @class Versioned
     * @brief Abstract base class for objects that track version changes.
     * 
     * Objects inheriting from this class maintain a version number that 
     * increments when their state changes, allowing efficient change detection.
     */
    class Versioned
    {
    public:
        Versioned() = default;
        virtual ~Versioned() = default;
        
        // No copy
        Versioned(const Versioned&) = delete;
        Versioned& operator=(const Versioned&) = delete;
        
        // Allow move
        Versioned(Versioned&&) noexcept = default;
        Versioned& operator=(Versioned&&) noexcept = default;
        
        /**
         * @brief Get the current version of this object.
         * @return The version number, which increments each time a property changes.
         */
        uint64_t getVersion() const { return m_version; }
        
    protected:
        /**
         * @brief Increment the version number when properties change.
         */
        void incrementVersion() { ++m_version; }
        
    private:
        std::atomic<uint64_t> m_version{0};
    };

} // namespace engine::core