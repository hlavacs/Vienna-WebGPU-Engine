#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/debug/Loggable.h"

namespace engine::resources
{
/**
 * @class ResourceManagerBase
 * @brief Base class for resource managers of identifiable engine resources.
 *
 * Provides generic logic for adding, removing, retrieving, and managing resources
 * that inherit from Identifiable<T>. Resources are managed via handles and shared pointers.
 * This class is move-only and not copyable.
 *
 * @tparam T The resource type, must inherit from Identifiable<T>.
 */
template <typename T>
class ResourceManagerBase : public engine::debug::Loggable
{
  public:
	using HandleType = engine::core::Handle<T>;
	using Ptr = std::shared_ptr<T>;
	using IdentifiableType = engine::core::Identifiable<T>;

	/**
	 * @brief Constructs a ResourceManagerBase and sets up handle resolution.
	 */
	ResourceManagerBase()
	{
		static_assert(std::is_base_of_v<IdentifiableType, T>, "T must inherit from Identifiable<T>");
		engine::core::Handle<T>::setResolver([this](engine::core::Handle<T> h)
											 { return get(h); });
	}

	/**
	 * @brief Virtual destructor.
	 */
	virtual ~ResourceManagerBase() = default;

	// Delete copy constructor and copy assignment to enforce move-only semantics.
	ResourceManagerBase(const ResourceManagerBase &) = delete;
	ResourceManagerBase &operator=(const ResourceManagerBase &) = delete;

	// Move constructor - must update resolver to point to new instance
	ResourceManagerBase(ResourceManagerBase &&other) noexcept
		: m_resources(std::move(other.m_resources))
	{
		// Update the resolver to point to the new instance
		engine::core::Handle<T>::setResolver([this](engine::core::Handle<T> h)
											 { return get(h); });
	}

	// Move assignment - must update resolver to point to new instance
	ResourceManagerBase &operator=(ResourceManagerBase &&other) noexcept
	{
		if (this != &other)
		{
			m_resources = std::move(other.m_resources);
			// Update the resolver to point to the new instance
			engine::core::Handle<T>::setResolver([this](engine::core::Handle<T> h)
												 { return get(h); });
		}
		return *this;
	}

	/**
	 * @brief Adds a resource to the manager.
	 * @param resource Shared pointer to the resource.
	 * @return Optional handle to the resource, or std::nullopt if resource is null.
	 */
	std::optional<HandleType> add(const Ptr &resource)
	{
		if (!resource)
			return std::nullopt;

		const auto handle = resource->getHandle();
		std::scoped_lock lock(m_mutex);
		m_resources[handle] = resource;
		return handle;
	}

	/**
	 * @brief Removes a resource by its handle.
	 * @param handle The handle of the resource to remove.
	 * @return True if the resource was removed, false if not found.
	 */
	bool remove(const HandleType &handle)
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_resources.find(handle);
		if (it != m_resources.end())
		{
			// const_cast needed because map key is const, but we need to invalidate
			const_cast<HandleType &>(it->first).invalidate();
			m_resources.erase(it);
			return true;
		}
		return false;
	}

	/**
	 * @brief Removes a resource by its shared pointer.
	 * @param resource Shared pointer to the resource.
	 * @return True if the resource was removed, false otherwise.
	 */
	bool remove(const Ptr &resource)
	{
		if (!resource)
			return false;
		return remove(resource->getHandle());
	}

	/**
	 * @brief Retrieves a resource by its handle.
	 * @param handle The handle of the resource.
	 * @return Optional shared pointer to the resource, or std::nullopt if not found.
	 */
	std::optional<Ptr> get(const HandleType &handle) const
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_resources.find(handle);
		if (it != m_resources.end())
			return it->second;
		return std::nullopt;
	}

	/**
	 * @brief Retrieves a resource by its runtime ID.
	 * @param id The runtime ID of the resource.
	 * @return Optional shared pointer to the resource, or std::nullopt if not found.
	 */
	std::optional<Ptr> getByID(typename HandleType::id_type id) const
	{
		std::scoped_lock lock(m_mutex);
		for (const auto &[handle, ptr] : m_resources)
		{
			if (handle.id() == id)
				return ptr;
		}
		return std::nullopt;
	}

	/**
	 * @brief Retrieves a resource by name.
	 * @param name The name of the resource.
	 * @return Optional shared pointer to the resource, or std::nullopt if not found.
	 */
	std::optional<Ptr> getByName(const std::string &name) const
	{
		std::scoped_lock lock(m_mutex);
		for (const auto &[handle, ptr] : m_resources)
		{
			if (ptr && ptr->getName().has_value() && ptr->getName().value() == name)
				return ptr;
		}
		return std::nullopt;
	}

	/**
	 * @brief Retrieves all resources with the given name.
	 * @param name The name of the resource(s).
	 * @return Vector of shared pointers to all resources with the given name.
	 */
	std::vector<Ptr> getAllWithName(const std::string &name) const
	{
		std::scoped_lock lock(m_mutex);
		std::vector<Ptr> matches;
		matches.reserve(2);
		for (const auto &[handle, ptr] : m_resources)
		{
			if (ptr && ptr->getName().has_value() && ptr->getName().value() == name)
				matches.push_back(ptr);
		}
		return matches;
	}

	/**
	 * @brief Removes all resources from the manager and invalidates their handles.
	 */
	void clear()
	{
		std::scoped_lock lock(m_mutex);
		for (auto &[handle, _] : m_resources)
		{
			// const_cast needed because map key is const, but we need to invalidate
			const_cast<HandleType &>(handle).invalidate();
		}
		m_resources.clear();
	}

	/**
	 * @brief Retrieves all resource handles managed by this manager.
	 * @return Vector of all handles.
	 */
	std::vector<HandleType> getAllHandles() const
	{
		std::scoped_lock lock(m_mutex);
		std::vector<HandleType> out;
		out.reserve(m_resources.size());
		for (const auto &[handle, _] : m_resources)
			out.push_back(handle);
		return out;
	}

	/**
	 * @brief Retrieves all resources managed by this manager.
	 * @return Vector of shared pointers to all resources.
	 */
	std::vector<Ptr> getAll() const
	{
		std::scoped_lock lock(m_mutex);
		std::vector<Ptr> out;
		out.reserve(m_resources.size());
		for (const auto &[_, ptr] : m_resources)
			out.push_back(ptr);
		return out;
	}

	/**
	 * @brief Gets the total number of resources managed.
	 * @return The resource count.
	 */
	size_t getResourceCount() const
	{
		std::scoped_lock lock(m_mutex);
		return m_resources.size();
	}

  protected:
	mutable std::mutex m_mutex;								 ///< Mutex for thread-safe access.
	mutable std::unordered_map<HandleType, Ptr> m_resources; ///< Map of handles to resources.
};

} // namespace engine::resources