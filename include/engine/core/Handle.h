#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace engine::resources
{
/**
 * @brief Forward declaration for friend access
 */
template <typename T>
class ResourceManagerBase;
} // namespace engine::resources

namespace engine::core
{

/**
 * @brief Handle to a managed resource with optional resolver support.
 */
template <typename T>
class Handle
{
  public:
	// TODO: Make id_type a template parameter for custom handle types
	using id_type = uint64_t;

	constexpr Handle() = default;
	explicit constexpr Handle(id_type id) :
		m_id(id) {}

	[[nodiscard]] constexpr id_type id() const { return m_id; }
	[[nodiscard]] constexpr bool valid() const { return m_id != 0; }

	constexpr bool operator==(const Handle<T> &other) const { return m_id == other.m_id; }
	constexpr bool operator!=(const Handle<T> &other) const { return m_id != other.m_id; }
	constexpr bool operator<(const Handle<T> &other) const { return m_id < other.m_id; }

	// Attempts to resolve the handle using the resolver set by the manager
	[[nodiscard]] std::optional<std::shared_ptr<T>> get() const
	{
		if (!valid() || !s_resolver)
			return std::nullopt;
		return s_resolver(*this);
	}

  private:
	friend class engine::resources::ResourceManagerBase<T>;

	// Used by manager to install resolver
	static void setResolver(std::function<std::optional<std::shared_ptr<T>>(Handle<T>)> resolver)
	{
		s_resolver = std::move(resolver);
	}

	// Used by manager to invalidate a handle
	void invalidate()
	{
		m_id = 0;
	}

	id_type m_id = 0; // 0 means invalid/null
	static inline std::function<std::optional<std::shared_ptr<T>>(Handle<T>)> s_resolver;
};

/**
 * @brief Converts an optional shared_ptr<T> to its Handle<T> or a default/null handle.
 *
 * Intended to be used with Identifiable-derived types, where getHandle() returns Handle<T>.
 */
template <typename T>
Handle<T> unwrapOrHandle(const std::optional<std::shared_ptr<T>> &opt);

} // namespace engine::core

template <typename T>
engine::core::Handle<T> engine::core::unwrapOrHandle(const std::optional<std::shared_ptr<T>> &opt)
{
	if (opt)
		return opt.value()->getHandle();
	return Handle<T>{};
}

// Hash support for unordered_map
namespace std
{
template <typename T>
struct hash<engine::core::Handle<T>>
{
	std::size_t operator()(const engine::core::Handle<T> &handle) const noexcept
	{
		return std::hash<typename engine::core::Handle<T>::id_type>()(handle.id());
	}
};
} // namespace std
