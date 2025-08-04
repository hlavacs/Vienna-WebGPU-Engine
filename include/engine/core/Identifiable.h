#pragma once

#include "engine/core/Handle.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <string>

namespace engine::core
{

/**
 * IDGenerator provides thread-safe generation of unique runtime IDs.
 * Used internally by Identifiable and other systems needing globally unique IDs.
 */
class IDGenerator
{
  public:
	static uint64_t nextID();
};

// --------------------
/**
 * Identifiable is a base class providing a unique runtime ID
 * and an optional human-readable name for engine objects.
 *
 * NOTE: This ID system is only for **runtime object management**.
 * It is **not** suitable for persistent asset references or serialization.
 * Use UUIDs or asset GUIDs for saving/loading scenes and assets.
 */
template <typename T>
class Identifiable
{
  public:
	using HandleType = Handle<T>;

	explicit Identifiable(std::optional<std::string> name = std::nullopt);

	// Disable copy
	Identifiable(const Identifiable &) = delete;
	Identifiable &operator=(const Identifiable &) = delete;

	// Allow move, mark noexcept
	Identifiable(Identifiable &&) noexcept = default;
	Identifiable &operator=(Identifiable &&) noexcept = default;

	HandleType getHandle() const;

	std::optional<std::string> getName() const;
	void setName(std::string newName);

  private:
	const uint64_t m_id;
	mutable std::mutex m_nameMutex;
	std::optional<std::string> m_name;
};

} // namespace engine::core

#include "engine/core/Identifiable.inl"
