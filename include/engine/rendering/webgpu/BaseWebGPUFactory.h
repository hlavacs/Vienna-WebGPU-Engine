#pragma once
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "engine/core/Identifiable.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @brief Templated base class for all WebGPU factories.
 * @tparam SourceT Type used to create the GPU resource.
 * @tparam ProductT GPU resource type produced by the factory.
 */
template <typename SourceT, typename ProductT>
class BaseWebGPUFactory
{
  public:
	/**
	 * @brief Construct a factory with a WebGPU context.
	 * @param context The WebGPU context used for resource creation.
	 */
	explicit BaseWebGPUFactory(WebGPUContext &context) :
		m_context(context)
	{
		// Static assert that SourceT is derived from Identifiable<SourceT>
		static_assert(std::is_base_of_v<engine::core::Identifiable<SourceT>, SourceT>, "SourceT must derive from engine::core::Identifiable<SourceT>");
	}
	virtual ~BaseWebGPUFactory()
	{
		cleanup();
	};

	/**
	 * @brief Get a GPU resource from a source handle if it exists.
	 * @param handle Handle to the source object.
	 * @return Shared pointer to the GPU resource, or nullptr if not found.
	 * @note This does not create the resource if it does not exist; it only retrieves from cache.
	 */
	std::shared_ptr<ProductT> get(const typename SourceT::Handle &handle)
	{
		auto it = m_cache.find(handle);
		if (it != m_cache.end())
		{
			return it->second;
		}
		return nullptr;
	}

	/**
	 * @brief Check if a GPU resource exists for the given source handle.
	 * @param handle Handle to the source object.
	 * @return True if the resource exists in the cache, false otherwise.
	 */
	bool has(const typename SourceT::Handle &handle)
	{
		return m_cache.find(handle) != m_cache.end();
	}

	/**
	 * @brief Get or create a GPU resource from a source object.
	 * @param source The source object to create from.
	 * @return Shared pointer to the created GPU resource.
	 * @note This automatically creates a handle from the source and calls createFromHandle.
	 * If handle creation is not possible, it throws an error.
	 * This means there cannot be a GPU resource without a valid handle.
	 */
	std::shared_ptr<ProductT> createFrom(const SourceT &source)
	{
		typename engine::core::Identifiable<SourceT>::HandleType handle{};
		try
		{
			const engine::core::Identifiable<SourceT> &identifiable = static_cast<const engine::core::Identifiable<SourceT> &>(source);
			handle = identifiable.getHandle();
		}
		catch (const std::exception &e)
		{
			throw std::runtime_error(std::string("Could not create handle: ") + e.what() + "\nA valid handle is required. Make sure the source object is registered.");
		}
		return createFromHandle(handle);
	}

	/**
	 * @brief Get or create a GPU resource from a source handle.
	 * @param handle Handle to the source object.
	 * @return Shared pointer to the GPU resource.
	 * @note This uses an internal cache to avoid duplicate creations.
	 */
	virtual std::shared_ptr<ProductT> createFromHandle(const typename SourceT::Handle &handle)
	{
		auto it = m_cache.find(handle);
		if (it != m_cache.end())
		{
			return it->second;
		}
		auto product = createFromHandleUncached(handle);
		m_cache[handle] = product;
		return product;
	}

	/**
	 * @brief Clear the internal cache of created resources.
	 * Careful: this does not delete the resources themselves if they are still referenced elsewhere.
	 * @note Override this method in derived classes if additional cleanup is needed.
	 * @warning If used it might lead to dangling pointers in existing resources!
	 */
	virtual void cleanup()
	{
		m_cache.clear();
	}

  protected:
	/**
	 * @brief Create a GPU resource from a handle to a source object.
	 * @param handle Handle to the source object.
	 * @return Shared pointer to the created GPU resource.
	 */
	virtual std::shared_ptr<ProductT> createFromHandleUncached(const typename SourceT::Handle &handle) = 0;

  protected:
	/**
	 * @brief Reference to the WebGPU context for resource creation.
	 */
	WebGPUContext &m_context;

	/**
	 * @brief Cache mapping source handles to created GPU resources.
	 */
	std::unordered_map<typename SourceT::Handle, std::shared_ptr<ProductT>> m_cache;
};

} // namespace engine::rendering::webgpu
