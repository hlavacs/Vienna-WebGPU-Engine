#pragma once
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
	/**
	 * @class WebGPUDepthStencilStateFactory
	 * @brief Factory for creating WebGPU DepthStencilState objects with default or custom settings.
	 */
	class WebGPUDepthStencilStateFactory
	{
	public:
		/**
		 * @brief Constructs a new WebGPUDepthStencilStateFactory.
		 */
		explicit WebGPUDepthStencilStateFactory();

		/**
		 * @brief Returns a default DepthStencilState for a given format.
		 *
		 * @param format The texture format for the depth buffer.
		 * @param enableDepth Whether depth writing is enabled (default: true).
		 * @return A default-configured wgpu::DepthStencilState.
		 */
		wgpu::DepthStencilState createDefault(wgpu::TextureFormat format, bool enableDepth = true);

		/**
		 * @brief Returns a custom DepthStencilState with full control over all parameters.
		 *
		 * @param format The texture format for the depth buffer.
		 * @param depthWriteEnabled Enable or disable depth writing.
		 * @param depthCompare Depth comparison function.
		 * @param stencilReadMask Stencil read mask (default: 0).
		 * @param stencilWriteMask Stencil write mask (default: 0).
		 * @param stencilFront Stencil operations for front faces (default: all Keep).
		 * @param stencilBack Stencil operations for back faces (default: all Keep).
		 * @return A fully-configured wgpu::DepthStencilState.
		 */
		wgpu::DepthStencilState create(
			wgpu::TextureFormat format,
			bool depthWriteEnabled,
			wgpu::CompareFunction depthCompare,
			uint32_t stencilReadMask = 0,
			uint32_t stencilWriteMask = 0,
			wgpu::StencilFaceState stencilFront = {},
			wgpu::StencilFaceState stencilBack = {});
	};

} // namespace engine::rendering::webgpu
