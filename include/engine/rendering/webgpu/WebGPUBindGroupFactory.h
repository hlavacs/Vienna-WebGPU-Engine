#pragma once

#include <map>
#include <memory>
#include <variant>
#include <vector>

#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUSampler.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;
class WebGPUBindGroup;
class WebGPUBuffer;
class WebGPUTexture;
class WebGPUMaterial;

/**
 * @brief Resource variant for bind group entries.
 * Can hold a texture, sampler, or buffer. The sampler is held via
 * `std::shared_ptr<WebGPUSampler>` (not raw `wgpu::Sampler`) so the bind
 * group's reference to the sampler outlives the factory's cache —
 * preventing the historical "Sampler[Id] does not exist" crash on
 * Clear All when a pass had captured a value-typed `wgpu::Sampler` copy.
 */
struct BindGroupResource
{
	std::variant<
		std::shared_ptr<WebGPUTexture>, // Texture resource
		std::shared_ptr<WebGPUSampler>, // Sampler resource (RAII)
		std::shared_ptr<WebGPUBuffer>   // Buffer resource
		>
		resource;

	// Convenience constructors
	BindGroupResource(const std::shared_ptr<WebGPUTexture> &tex) : resource(tex) {}
	BindGroupResource(const std::shared_ptr<WebGPUSampler> &sampler) : resource(sampler) {}
	BindGroupResource(const std::shared_ptr<WebGPUBuffer> &buffer) : resource(buffer) {}
};

/**
 * @brief Key for identifying a bind group binding: (groupIndex, bindingIndex).
 * - groupIndex: The bind group index (e.g., 0, 1, 2, 3).
 * - bindingIndex: The binding index within that group (e.g., 0, 1, 2).
 */
using BindGroupBindingKey = std::tuple<uint32_t, uint32_t>;

/**
 * @brief Factory for creating and managing WebGPU bind groups and layouts.
 *
 * This class provides utility functions for creating bind group layouts, bind groups,
 * and their descriptors, as well as helpers for common layout entry patterns.
 * It also manages the lifetime of created bind groups and layouts.
 */
class WebGPUBindGroupFactory
{
  public:
	/**
	 * @brief Construct a new WebGPUBindGroupFactory.
	 * @param context Reference to the WebGPUContext.
	 */
	explicit WebGPUBindGroupFactory(WebGPUContext &context);

	/**
	 * @brief Destructor. Cleans up created bind groups and layouts.
	 */
	~WebGPUBindGroupFactory();

	/**
	 * @brief Helper to create a vector of BindGroupLayoutEntries with auto binding assignment.
	 * @param name Optional name for the layout.
	 * @param type Semantic type of the bind group.
	 * @param reuse Reuse policy for the bind group.
	 * @param entries BindGroupLayoutEntry objects (with binding=-1 for auto).
	 * @param bindings Typed bindings contained in this layout.
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createBindGroupLayoutInfo(
		std::string name,
		BindGroupType type,
		BindGroupReuse reuse,
		std::vector<wgpu::BindGroupLayoutEntry> entries,
		std::vector<BindGroupBinding> bindings = {}
	);

	/**
	 * @brief Helper to create a vector of BindGroupLayoutEntries with auto binding assignment.
	 * @tparam Entries Variadic BindGroupLayoutEntry arguments.
	 * @param rawEntries BindGroupLayoutEntry objects (with binding=-1 for auto).
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	template <typename... Entries>
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createBindGroupLayoutInfo(std::string name, Entries &&...rawEntries)
	{
		static_assert((std::is_convertible_v<Entries, wgpu::BindGroupLayoutEntry> && ...), "All arguments must be convertible to wgpu::BindGroupLayoutEntry");

		std::vector<wgpu::BindGroupLayoutEntry> entries = {std::forward<Entries>(rawEntries)...};
		return createBindGroupLayoutInfo(name, entries);
	}

	/**
	 * @brief Create a raw bind group layout from explicit entries.
	 *
	 * For low-level / compute consumers that need a `wgpu::BindGroupLayout`
	 * directly (to feed a pipeline layout) rather than the richer
	 * `WebGPUBindGroupLayoutInfo`. Routes the device call through the factory
	 * so no caller pokes `device.createBindGroupLayout` itself.
	 */
	wgpu::BindGroupLayout createBindGroupLayout(
		const std::vector<wgpu::BindGroupLayoutEntry> &entries,
		const char *label = nullptr
	);

	/**
	 * @brief Generic bind group creation from entries.
	 */
	wgpu::BindGroup createBindGroup(
		const wgpu::BindGroupLayout &layout,
		const std::vector<wgpu::BindGroupEntry> &entries
	);

	/**
	 * @brief Create a bind group from a resource map with explicit binding assignments.
	 *
	 * This method is useful for passes like shadow or composite that need to bind specific
	 * textures, samplers, and buffers at specific binding points within a specific group.
	 *
	 * @param layoutInfo The bind group layout info.
	 * @param resourceOverrides Map of (groupIndex, bindingIndex) tuple to resource (texture, sampler, or buffer) that should be used instead of defaults.
	 * @param material Optional material for per-material resources. (TextureBindings taken from material if not overridden)
	 * @param label Optional label for the bind group (for debugging).
	 * @return Created bind group.
	 *
	 * @example
	 * ```cpp
	 * // For group 4
	 * std::map<BindGroupBindingKey, BindGroupResource> resources;
	 * resources[{4, 0}] = shadowMapTexture;  // Group 4, Binding 0: Texture
	 * resources[{4, 1}] = sampler;           // Group 4, Binding 1: Sampler
	 * resources[{4, 2}] = uniformBuffer;     // Group 4, Binding 2: Buffer
	 * auto bindGroup = factory.createBindGroup(layout, layoutInfo, resources, nullptr, "ShadowBindGroup");
	 * ```
	 */
	std::shared_ptr<WebGPUBindGroup> createBindGroup(
		const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo,
		const std::map<BindGroupBindingKey, BindGroupResource> &resourceOverrides = {},
		const std::shared_ptr<WebGPUMaterial> &material = nullptr,
		const char *label = nullptr
	);

	/**
	 * @brief Create a wrapped bind group from a layout info + explicit wgpu
	 *        entries the caller has already assembled.
	 *
	 * For passes that build their own @c wgpu::BindGroupEntry list (custom
	 * resource layouts) yet already hold the @ref WebGPUBindGroupLayoutInfo.
	 * Returns the @ref WebGPUBindGroup wrapper directly so callers no longer
	 * hand-construct it. @p buffers are kept alive by the returned wrapper.
	 */
	std::shared_ptr<WebGPUBindGroup> createBindGroup(
		const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo,
		const std::vector<wgpu::BindGroupEntry> &entries,
		std::vector<std::shared_ptr<WebGPUBuffer>> buffers = {}
	);

	/**
	 * @brief Get global bind group by key.
	 * @param key Unique key for the bind group.
	 * @return Shared pointer to the WebGPUBindGroup, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUBindGroup> getGlobalBindGroup(const std::string &key);

	/**
	 * @brief Get global bind group layout by key.
	 * @param key Unique key for the bind group layout.
	 * @return Shared pointer to the WebGPUBindGroupLayoutInfo, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> getGlobalBindGroupLayout(const std::string &key);

	/**
	 * @brief Store a global bind group with a unique key.
	 * @param key Unique key for the bind group.
	 * @param bindGroup Shared pointer to the WebGPUBindGroup.
	 * @return True if stored successfully, false if key already exists.
	 */
	bool storeGlobalBindGroup(
		const std::string &key,
		const std::shared_ptr<WebGPUBindGroup> &bindGroup
	);

	/**
	 * @brief Store a global bind group layout with a unique key.
	 * @param key Unique key for the bind group layout.
	 * @param layoutInfo Shared pointer to the WebGPUBindGroupLayoutInfo.
	 * @return True if stored successfully, false if key already exists.
	 */
	bool storeGlobalBindGroupLayout(
		const std::string &key,
		const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo
	);

	/**
	 * @brief Release all created bind groups and layouts.
	 */
	void cleanup()
	{
		m_globalBindGroups.clear();
		m_globalBindGroupLayouts.clear();
	}

  private:
	/**
	 * @brief Create a BindGroupLayoutDescriptor from entries.
	 */
	wgpu::BindGroupLayoutDescriptor createBindGroupLayoutDescriptor(const std::vector<wgpu::BindGroupLayoutEntry> &entries);

	/**
	 * @brief Create a BindGroupDescriptor from layout and entries.
	 */
	wgpu::BindGroupDescriptor createBindGroupDescriptor(const wgpu::BindGroupLayout &layout, const std::vector<wgpu::BindGroupEntry> &entries);

	WebGPUContext &m_context;

	std::unordered_map<std::string, std::shared_ptr<WebGPUBindGroup>> m_globalBindGroups;
	std::unordered_map<std::string, std::shared_ptr<WebGPUBindGroupLayoutInfo>> m_globalBindGroupLayouts;
};

} // namespace engine::rendering::webgpu
