#pragma once

#include "engine/rendering/webgpu/WebGPUTextureFactory.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

struct WebGPUContext;

/**
 * @class WebGPUSurfaceManager
 * @brief Manages the WebGPU surface and its configuration, auto-reconfiguring if needed.
 *
 * This class encapsulates the logic for managing the WebGPU surface, including configuration,
 * swap-chain handling, and texture acquisition. It automatically reconfigures the surface
 * when the window size or configuration changes, and provides access to the current surface texture.
 */
class WebGPUSurfaceManager
{
  public:
	/**
	 * @struct Config
	 * @brief Configuration for the WebGPU surface.
	 *
	 * Holds all parameters required to configure the WebGPU surface, including size, format,
	 * usage, present mode, alpha mode, and optional view formats.
	 */
	struct Config
	{
		uint32_t width = 0;	 ///< Surface width in pixels
		uint32_t height = 0; ///< Surface height in pixels

		wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;	 ///< Texture format for the surface
		wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment; ///< Usage flags for the surface texture

		std::vector<wgpu::TextureFormat> viewFormats{}; ///< Optional additional view formats

		wgpu::PresentMode presentMode = wgpu::PresentMode::Fifo;			 ///< Presentation mode for swap-chain
		wgpu::CompositeAlphaMode alphaMode = wgpu::CompositeAlphaMode::Auto; ///< Alpha compositing mode

		/**
		 * @brief Convert to wgpu::SurfaceConfiguration for WebGPU API.
		 * @param device The WebGPU device.
		 * @return Configured wgpu::SurfaceConfiguration.
		 */
		[[nodiscard]] wgpu::SurfaceConfiguration asSurfaceConfiguration(const wgpu::Device &device) const
		{
			wgpu::SurfaceConfiguration cfg{};
			cfg.device = device;
			cfg.format = format;
			cfg.usage = usage;
			cfg.width = width;
			cfg.height = height;
			cfg.presentMode = presentMode;
			cfg.alphaMode = alphaMode;
			// Only include viewFormats if non-empty#
			if (!viewFormats.empty())
			{
				cfg.viewFormatCount = viewFormats.size();
				cfg.viewFormats = viewFormats.empty() ? nullptr : reinterpret_cast<const WGPUTextureFormat *>(viewFormats.data());
			}
			return cfg;
		}

		/**
		 * @brief Equality operator for Config.
		 * @param o Other config to compare.
		 * @return True if all parameters match, false otherwise.
		 */
		bool operator==(const Config &o) const
		{
			return width == o.width && height == o.height && format == o.format && usage == o.usage && presentMode == o.presentMode && alphaMode == o.alphaMode && viewFormats == o.viewFormats;
		}

		/**
		 * @brief Inequality operator for Config.
		 * @param o Other config to compare.
		 * @return True if any parameter differs, false otherwise.
		 */
		bool operator!=(const Config &o) const { return !(*this == o); }
	};

  public:
	/**
	 * @brief Construct a WebGPUSurfaceManager for the given context.
	 * @param context Reference to the WebGPU context.
	 */
	explicit WebGPUSurfaceManager(WebGPUContext &context);

	/**
	 * @brief Update the surface if width/height or config changed.
	 * @param width New width in pixels.
	 * @param height New height in pixels.
	 * @return True if the surface was reconfigured, false otherwise.
	 */
	bool updateIfNeeded(uint32_t width, uint32_t height);

	/**
	 * @brief Acquire the next swap-chain/surface texture as a WebGPUTexture.
	 * @return Shared pointer to the acquired WebGPUTexture.
	 */
	std::shared_ptr<WebGPUTexture> acquireNextTexture();

	/**
	 * @brief Reapply the current surface configuration.
	 * @param config Optional new configuration to apply. If provided, replaces the current config.
	 */
	void reconfigure(const std::optional<Config> &config = std::nullopt);

	/**
	 * @brief Get the current surface configuration.
	 * @return Reference to the current Config.
	 */
	[[nodiscard]] const Config &currentConfig() const { return m_config; }

  private:
	/**
	 * @brief Internal: apply the current config to the surface.
	 */
	void applyConfig();

  private:
	WebGPUContext &m_context; ///< Reference to the WebGPU context

	Config m_config{};			  ///< Current surface configuration
	Config m_lastAppliedConfig{}; ///< Last applied surface configuration

#ifndef WEBGPU_BACKEND_WGPU
	wgpu::SwapChain m_swapChain; ///< Swap-chain for non-WGPU backends
#endif
};

} // namespace engine::rendering::webgpu
