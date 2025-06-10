#pragma once
#include <memory>

namespace engine::rendering::webgpu
{

	class WebGPUContext;

	// Templated base class for all WebGPU factories
	// SourceT: type used to create the GPU resource
	// ProductT: GPU resource type produced

	template <typename SourceT, typename ProductT>
	class BaseWebGPUFactory
	{
	public:
		explicit BaseWebGPUFactory(WebGPUContext &context) : m_context(context) {}
		virtual ~BaseWebGPUFactory() = default;

		// Factory method to be implemented by derived classes
		virtual std::shared_ptr<ProductT> createFrom(const SourceT &source) = 0;

	protected:
		WebGPUContext &m_context;
	};

} // namespace engine::rendering::webgpu
