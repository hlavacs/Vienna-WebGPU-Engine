#pragma once
/**
 * @file WebGPURenderObject.h
 * @brief Base class for all GPU-side render objects in the WebGPU backend.
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
 * @class WebGPURenderObject
 * @brief Base class for all GPU-side render objects (camera, mesh, material, etc.) in the WebGPU backend.
 *        Provides context, name, type, dirty flag, versioning, and timestamps.
 * @tparam CPUObjectT The CPU-side object type this GPU object represents
 */
template <typename CPUObjectT>
class WebGPURenderObject : public engine::core::Identifiable<WebGPURenderObject<CPUObjectT>>
{
  public:
	/**
	 * @brief Type of the render object for RTTI and debugging.
	 */
	enum class Type
	{
		Unknown,  ///< Unknown type
		Camera,	  ///< Camera object
		Mesh,	  ///< Mesh object
		Material, ///< Material object
		Model,	  ///< Model object
		Texture	  ///< Texture object
	};

	/**
	 * @brief Construct a WebGPURenderObject.
	 * @param context The WebGPU context.
	 * @param cpuHandle Handle to the CPU-side object.
	 * @param type The type of the render object.
	 * @param name Optional name for debugging.
	 */
	explicit WebGPURenderObject(
		WebGPUContext &context,
		const typename CPUObjectT::Handle &cpuHandle,
		Type type = Type::Unknown,
		std::optional<std::string> name = std::nullopt
	) :
		engine::core::Identifiable<WebGPURenderObject<CPUObjectT>>(std::move(name)),
		m_context(context),
		m_cpuHandle(cpuHandle),
		m_dirty(true),
		m_type(type),
		m_creationTime(std::chrono::steady_clock::now()),
		m_lastUpdateTime(m_creationTime),
		m_lastSyncedVersion(0)
	{
		// Static assert that CPUObjectT is derived from Identifiable<CPUObjectT>
		static_assert(std::is_base_of_v<engine::core::Identifiable<CPUObjectT>, CPUObjectT>, "CPUObjectT must derive from engine::core::Identifiable<CPUObjectT>");

		// Static assert that CPUObjectT::Handle is the same as Handle<CPUObjectT>
		static_assert(std::is_same_v<typename CPUObjectT::Handle, engine::core::Handle<CPUObjectT>>, "CPUObjectT::Handle must be engine::core::Handle<CPUObjectT>");

		// Static assert that CPUObjectT implements Versioned
		static_assert(std::is_base_of_v<engine::core::Versioned, CPUObjectT>, "CPUObjectT must derive from engine::core::Versioned");
	}

	virtual ~WebGPURenderObject() = default;

	/**
	 * @brief Get the handle to the CPU-side object.
	 * @return Handle to the CPU-side object.
	 */
	const typename CPUObjectT::Handle &getCPUHandle() const { return m_cpuHandle; }

	/**
	 * @brief Get the CPU-side object.
	 * @return Reference to the CPU-side object.
	 * @throws std::runtime_error if the handle is invalid.
	 */
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
	 * @brief Update the GPU-side resources if needed.
	 * @note This method is called before rendering to sync CPU changes to GPU.
	 *       Will call `updateGPUResources()` if the CPU object is dirty or has a new version.
	 */
	virtual void update()
	{
		auto obj = m_cpuHandle.get();
		if (obj && obj.value())
		{
			const auto &cpuObj = *obj.value();
			if (cpuObj.getVersion() > m_lastSyncedVersion || isDirty())
			{
				updateGPUResources();
				m_lastSyncedVersion = cpuObj.getVersion();
				setDirty(false);
			}
		}

		m_lastUpdateTime = std::chrono::steady_clock::now();
	}

	/**
	 * @brief Bind the render object for rendering.
	 * @param renderPass The render pass encoder.
	 */
    virtual void bind(wgpu::RenderPassEncoder &renderPass) const = 0;

	/** @brief Get the WebGPU context. */
	WebGPUContext &getContext() { return m_context; }

	/** @brief Check if the object is dirty (needs update). */
	bool isDirty() const { return m_dirty; }

	/** @brief Set the dirty flag. */
	void setDirty(bool dirty) { m_dirty = dirty; }

	/** @brief Get the type of the object. */
	Type getType() const { return m_type; }

	/** @brief Set the type of the object. */
	void setType(Type type) { m_type = type; }

	/** @brief Get the creation time of the object. */
	std::chrono::steady_clock::time_point getCreationTime() const { return m_creationTime; }

	/** @brief Get the last update time of the object. */
	std::chrono::steady_clock::time_point getLastUpdateTime() const { return m_lastUpdateTime; }

  protected:
	/**
	 * @brief Update GPU resources from CPU data.
	 * @note Derived classes must implement this to sync CPU changes to GPU.
	 */
	virtual void updateGPUResources() = 0;

	WebGPUContext &m_context;
	typename CPUObjectT::Handle m_cpuHandle;
	bool m_dirty;
	Type m_type;
	std::chrono::steady_clock::time_point m_creationTime;
	std::chrono::steady_clock::time_point m_lastUpdateTime;
	uint64_t m_lastSyncedVersion = 0;
};

} // namespace engine::rendering::webgpu
