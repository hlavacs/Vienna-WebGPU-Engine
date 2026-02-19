#pragma once
#include <cstdint>
#include <spdlog/spdlog.h>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

/**
 * @brief Configuration for WebGPU device resource and pipeline limits.
 *
 * Allows fine-tuning of the maximum resource and pipeline limits requested from a WebGPU device.
 * All values are clamped against actual hardware capabilities at device creation time.
 *
 * Usage:
 *   auto limits = DeviceLimitsConfig::standard();
 *   limits.maxTextureDimension2D = 8192; // override one field
 *   ctx.initialize(window, vsync, limits);
 */
struct DeviceLimitsConfig
{
	// -------------------------------------------------------------------------
	// Geometry
	// -------------------------------------------------------------------------

	uint32_t maxVertexAttributes = 16;
	///< Maximum number of vertex attributes (position, normal, uv, etc.) per vertex shader.

	uint32_t maxVertexBuffers = 8;
	///< Maximum number of vertex buffers that can be bound simultaneously.

	uint64_t maxBufferSize = 64ULL * 1024 * 1024;
	///< Maximum size of a single GPU buffer in bytes (vertex, index, uniform, storage).

	uint32_t maxVertexBufferArrayStride = 256;
	///< Maximum stride in bytes between consecutive elements in a vertex buffer. Must be a multiple of 4.

	// -------------------------------------------------------------------------
	// Inter-stage
	// -------------------------------------------------------------------------

	uint32_t maxInterStageShaderComponents = 60;
	///< Maximum number of scalar components that can be passed between shader stages
	///< (e.g. vertex → fragment). A vec4 counts as 4 components.

	// -------------------------------------------------------------------------
	// Bind groups
	// -------------------------------------------------------------------------

	uint32_t maxBindGroups = 8;
	///< Maximum number of bind groups usable in a pipeline.

	uint32_t maxBindingsPerBindGroup = 16;
	///< Maximum number of bindings (buffers, textures, samplers) per bind group.

	uint32_t maxUniformBuffersPerShaderStage = 8;
	///< Maximum number of uniform buffers accessible per shader stage.

	uint64_t maxUniformBufferBindingSize = 64ULL * 1024;
	///< Maximum size of a single uniform buffer binding in bytes.

	// -------------------------------------------------------------------------
	// Textures
	// -------------------------------------------------------------------------

	uint32_t maxTextureDimension1D = 4096;
	///< Maximum width of a 1D texture in pixels.

	uint32_t maxTextureDimension2D = 4096;
	///< Maximum width/height of a 2D texture in pixels.

	uint32_t maxTextureArrayLayers = 256;
	///< Maximum number of layers in a texture array or cube map array.

	uint32_t maxSampledTexturesPerShaderStage = 16;
	///< Maximum number of sampled textures accessible per shader stage.

	uint32_t maxSamplersPerShaderStage = 16;
	///< Maximum number of samplers accessible per shader stage.

	// -------------------------------------------------------------------------
	// Storage
	// -------------------------------------------------------------------------

	uint32_t maxStorageBuffersPerShaderStage = 4;
	///< Maximum number of read/write storage buffers per shader stage.

	uint64_t maxStorageBufferBindingSize = 16ULL * 1024 * 1024;
	///< Maximum size of a single storage buffer binding in bytes.

	// -------------------------------------------------------------------------
	// Presets
	// -------------------------------------------------------------------------

	/// Lowest limits for maximum compatibility with older/weaker devices.
	static DeviceLimitsConfig minimal();

	/// Balanced defaults that work on most modern desktop and mobile GPUs.
	static DeviceLimitsConfig standard();

	/// High limits for capable desktop GPUs.
	static DeviceLimitsConfig high();

	/// Build a config that exactly matches the hardware's supported limits.
	/// Useful as a starting point when you want to allow everything the GPU can do.
	static DeviceLimitsConfig fromSupported(const wgpu::SupportedLimits &supported);

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	/**
	 * @brief Return a copy of this config with all fields clamped to hardware limits.
	 *
	 * Any field that exceeds the hardware maximum is silently reduced and a warning
	 * is emitted via spdlog so the caller knows what was adjusted.
	 *
	 * @param supported  The limits reported by the adapter (from adapter.getLimits()).
	 * @return           A new DeviceLimitsConfig safe to pass to applyTo().
	 */
	[[nodiscard]] DeviceLimitsConfig clamped(const wgpu::SupportedLimits &supported) const;

	/**
	 * @brief Write this config's fields into a wgpu::RequiredLimits struct.
	 *
	 * Does NOT handle alignment limits (minUniformBufferOffsetAlignment, etc.) —
	 * those are hardware-fixed and must be copied from SupportedLimits directly.
	 * Always call clamped() before applyTo() to avoid requesting unsupported values.
	 *
	 * @param out  The RequiredLimits struct to populate.
	 */
	void applyTo(wgpu::RequiredLimits &out) const;
};

// -----------------------------------------------------------------------------
// Preset implementations
// -----------------------------------------------------------------------------

inline DeviceLimitsConfig DeviceLimitsConfig::minimal()
{
	DeviceLimitsConfig c{};
	c.maxVertexAttributes = 8;
	c.maxVertexBuffers = 4;
	c.maxBufferSize = 16ULL * 1024 * 1024;
	c.maxVertexBufferArrayStride = 128;
	c.maxInterStageShaderComponents = 16;
	c.maxBindGroups = 2;
	c.maxBindingsPerBindGroup = 8;
	c.maxUniformBuffersPerShaderStage = 4;
	c.maxUniformBufferBindingSize = 16ULL * 1024;
	c.maxTextureDimension1D = 2048;
	c.maxTextureDimension2D = 2048;
	c.maxTextureArrayLayers = 64;
	c.maxSampledTexturesPerShaderStage = 8;
	c.maxSamplersPerShaderStage = 8;
	c.maxStorageBuffersPerShaderStage = 2;
	c.maxStorageBufferBindingSize = 8ULL * 1024 * 1024;
	return c;
}

inline DeviceLimitsConfig DeviceLimitsConfig::standard()
{
	return DeviceLimitsConfig{}; // default member values are the standard preset
}

inline DeviceLimitsConfig DeviceLimitsConfig::high()
{
	DeviceLimitsConfig c{};
	c.maxVertexAttributes = 32;
	c.maxVertexBuffers = 16;
	c.maxBufferSize = 256ULL * 1024 * 1024;
	c.maxVertexBufferArrayStride = 512;
	c.maxInterStageShaderComponents = 120;
	c.maxBindGroups = 8;
	c.maxBindingsPerBindGroup = 32;
	c.maxUniformBuffersPerShaderStage = 12;
	c.maxUniformBufferBindingSize = 256ULL * 1024;
	c.maxTextureDimension1D = 8192;
	c.maxTextureDimension2D = 8192;
	c.maxTextureArrayLayers = 2048;
	c.maxSampledTexturesPerShaderStage = 16;
	c.maxSamplersPerShaderStage = 16;
	c.maxStorageBuffersPerShaderStage = 8;
	c.maxStorageBufferBindingSize = 128ULL * 1024 * 1024;
	return c;
}

inline DeviceLimitsConfig DeviceLimitsConfig::fromSupported(const wgpu::SupportedLimits &supported)
{
	const auto &sl = supported.limits;
	DeviceLimitsConfig c{};
	c.maxVertexAttributes = sl.maxVertexAttributes;
	c.maxVertexBuffers = sl.maxVertexBuffers;
	c.maxBufferSize = sl.maxBufferSize;
	c.maxVertexBufferArrayStride = sl.maxVertexBufferArrayStride;
	c.maxInterStageShaderComponents = sl.maxInterStageShaderComponents;
	c.maxBindGroups = sl.maxBindGroups;
	c.maxBindingsPerBindGroup = sl.maxBindingsPerBindGroup;
	c.maxUniformBuffersPerShaderStage = sl.maxUniformBuffersPerShaderStage;
	c.maxUniformBufferBindingSize = sl.maxUniformBufferBindingSize;
	c.maxTextureDimension1D = sl.maxTextureDimension1D;
	c.maxTextureDimension2D = sl.maxTextureDimension2D;
	c.maxTextureArrayLayers = sl.maxTextureArrayLayers;
	c.maxSampledTexturesPerShaderStage = sl.maxSampledTexturesPerShaderStage;
	c.maxSamplersPerShaderStage = sl.maxSamplersPerShaderStage;
	c.maxStorageBuffersPerShaderStage = sl.maxStorageBuffersPerShaderStage;
	c.maxStorageBufferBindingSize = sl.maxStorageBufferBindingSize;
	return c;
}

inline DeviceLimitsConfig DeviceLimitsConfig::clamped(const wgpu::SupportedLimits &supported) const
{
	const auto &sl = supported.limits;
	DeviceLimitsConfig c = *this;

#define CLAMP_FIELD(field)                                                         \
	if (c.field > static_cast<decltype(c.field)>(sl.field))                        \
	{                                                                              \
		spdlog::warn("[WebGPU] Limit '{}': requested {} exceeds hardware max {}, " \
					 "clamping to {}.",                                            \
					 #field,                                                       \
					 c.field,                                                      \
					 sl.field,                                                     \
					 sl.field);                                                    \
		c.field = static_cast<decltype(c.field)>(sl.field);                        \
	}

	CLAMP_FIELD(maxVertexAttributes)
	CLAMP_FIELD(maxVertexBuffers)
	CLAMP_FIELD(maxBufferSize)
	CLAMP_FIELD(maxVertexBufferArrayStride)
	CLAMP_FIELD(maxInterStageShaderComponents)
	CLAMP_FIELD(maxBindGroups)
	CLAMP_FIELD(maxBindingsPerBindGroup)
	CLAMP_FIELD(maxUniformBuffersPerShaderStage)
	CLAMP_FIELD(maxUniformBufferBindingSize)
	CLAMP_FIELD(maxTextureDimension1D)
	CLAMP_FIELD(maxTextureDimension2D)
	CLAMP_FIELD(maxTextureArrayLayers)
	CLAMP_FIELD(maxSampledTexturesPerShaderStage)
	CLAMP_FIELD(maxSamplersPerShaderStage)
	CLAMP_FIELD(maxStorageBuffersPerShaderStage)
	CLAMP_FIELD(maxStorageBufferBindingSize)
#undef CLAMP_FIELD

	return c;
}

inline void DeviceLimitsConfig::applyTo(wgpu::RequiredLimits &out) const
{
	auto &rl = out.limits;
	rl.maxVertexAttributes = maxVertexAttributes;
	rl.maxVertexBuffers = maxVertexBuffers;
	rl.maxBufferSize = maxBufferSize;
	rl.maxVertexBufferArrayStride = maxVertexBufferArrayStride;
	rl.maxInterStageShaderComponents = maxInterStageShaderComponents;
	rl.maxBindGroups = maxBindGroups;
	rl.maxBindingsPerBindGroup = maxBindingsPerBindGroup;
	rl.maxUniformBuffersPerShaderStage = maxUniformBuffersPerShaderStage;
	rl.maxUniformBufferBindingSize = maxUniformBufferBindingSize;
	rl.maxTextureDimension1D = maxTextureDimension1D;
	rl.maxTextureDimension2D = maxTextureDimension2D;
	rl.maxTextureArrayLayers = maxTextureArrayLayers;
	rl.maxSampledTexturesPerShaderStage = maxSampledTexturesPerShaderStage;
	rl.maxSamplersPerShaderStage = maxSamplersPerShaderStage;
	rl.maxStorageBuffersPerShaderStage = maxStorageBuffersPerShaderStage;
	rl.maxStorageBufferBindingSize = maxStorageBufferBindingSize;
	// NOTE: minUniformBufferOffsetAlignment and minStorageBufferOffsetAlignment
	// are hardware-fixed and must be set from SupportedLimits directly in initDevice().
}

} // namespace engine::rendering::webgpu