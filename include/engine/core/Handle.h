#pragma once

#include <cstdint>
#include <functional>

namespace engine::core
{
	template <typename T>
	class Handle	
	{
	public:
		// ToDo: Allow different id_types by template
		using id_type = uint64_t;

		constexpr Handle() = default;
		explicit constexpr Handle(id_type id) : m_id(id) {}

		constexpr id_type id() const { return m_id; }
		constexpr bool valid() const { return m_id != 0; }

		constexpr bool operator==(const Handle<T> &other) const { return m_id == other.m_id; }
		constexpr bool operator!=(const Handle<T> &other) const { return m_id != other.m_id; }
		constexpr bool operator<(const Handle<T> &other) const { return m_id < other.m_id; }

	private:
		id_type m_id = 0; // 0 means invalid/null
	};

} // namespace engine::core

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
}
