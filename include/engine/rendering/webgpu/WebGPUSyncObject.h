#pragma once
/**
 * @file WebGPUSyncObject.h
 * @brief Base class for all GPU-side objects in the WebGPU backend that need automatic syncing.
 */
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
// Forward declaration to avoid circular dependency
class WebGPUContext;

/**
 * @class WebGPUSyncObject
 * @brief Base class for all GPU-side objects (mesh, material, etc.) in the WebGPU backend that need automatic syncing.
 *        Provides context, name, type, dirty flag, versioning, and timestamps.
 * @tparam CPUObjectT The CPU-side object type this GPU object represents
 */
template <typename CPUObjectT>
class WebGPUSyncObject
{
  public:
	/**
	 * @brief Construct a WebGPUSyncObject.
	 * @param context The WebGPU context.
	 * @param cpuHandle Handle to the CPU-side object.
	 */
	explicit WebGPUSyncObject(
		WebGPUContext &context,
		const typename CPUObjectT::Handle &cpuHandle
	) :
		m_context(context),
		m_cpuHandle(cpuHandle),
		m_dirty(true),
		m_creationTime(std::chrono::steady_clock::now()),
		m_lastUpdateTime(m_creationTime),
		m_lastSyncedVersion(0)
	{
		static_assert(std::is_base_of_v<engine::core::Identifiable<CPUObjectT>, CPUObjectT>, "CPUObjectT must derive from engine::core::Identifiable<CPUObjectT>");
		static_assert(std::is_same_v<typename CPUObjectT::Handle, engine::core::Handle<CPUObjectT>>, "CPUObjectT::Handle must be engine::core::Handle<CPUObjectT>");
		static_assert(std::is_base_of_v<engine::core::Versioned, CPUObjectT>, "CPUObjectT must derive from engine::core::Versioned");
	}

	virtual ~WebGPUSyncObject() = default;

	const typename CPUObjectT::Handle &getCPUHandle() const { return m_cpuHandle; }

	CPUObjectT &getCPUObject() const
	{
		auto obj = m_cpuHandle.get();
		if (!obj || !obj.value())
		{
			throw std::runtime_error("Invalid CPU object handle");
		}
		return *obj.value();
	}

	/**
	 * @brief Sync GPU resources from CPU if needed.
	 * Checks if the CPU object version has changed and calls syncFromCPU if necessary.
	 */
	void syncIfNeeded()
	{
		auto obj = m_cpuHandle.get();
		if (!obj || !obj.value()) return;

		const auto &cpuObj = *obj.value();
		if (needsSync(cpuObj))
		{
			syncFromCPU(cpuObj);
			m_lastSyncedVersion = cpuObj.getVersion();
		}
	}

  protected:
	virtual bool needsSync(const CPUObjectT &cpuObj) const
	{
		return cpuObj.getVersion() > m_lastSyncedVersion;
	}

	virtual void syncFromCPU(const CPUObjectT &cpuObj) = 0;

	WebGPUContext &m_context;
	typename CPUObjectT::Handle m_cpuHandle;
	bool m_dirty;
	std::chrono::steady_clock::time_point m_creationTime;
	std::chrono::steady_clock::time_point m_lastUpdateTime;
	uint64_t m_lastSyncedVersion = 0;
};

} // namespace engine::rendering::webgpu
