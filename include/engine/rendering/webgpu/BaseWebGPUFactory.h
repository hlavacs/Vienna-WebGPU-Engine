#pragma once
#include <memory>
#include <type_traits>
#include <stdexcept>

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
		explicit BaseWebGPUFactory(WebGPUContext &context) : m_context(context)
		{
			// Static assert that SourceT is derived from Identifiable<SourceT>
			static_assert(std::is_base_of_v<engine::core::Identifiable<SourceT>, SourceT>,
						  "SourceT must derive from engine::core::Identifiable<SourceT>");
		}
		virtual ~BaseWebGPUFactory() = default;

		/**
		 * @brief Create a GPU resource from a source object.
		 * @param source The source object to create from.
		 * @return Shared pointer to the created GPU resource.
		 * @note This automatically creates a handle from the source and calls createFromHandle.
		 *       If handle creation is not possible, it throws an error.
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
				throw std::runtime_error(std::string("Could not create handle: ") + e.what() +
										 "\nA valid handle is required. Make sure the source object is registered.");
			}
			return createFromHandle(handle);
		}

		/**
		 * @brief Create a GPU resource from a handle to a source object.
		 * @param handle Handle to the source object.
		 * @return Shared pointer to the created GPU resource.
		 */
		virtual std::shared_ptr<ProductT> createFromHandle(const typename SourceT::Handle &handle) = 0;

	protected:
		/**
		 * @brief Reference to the WebGPU context for resource creation.
		 */
		WebGPUContext &m_context;
	};

} // namespace engine::rendering::webgpu
