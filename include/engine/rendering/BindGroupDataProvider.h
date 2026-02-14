#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering
{

/**
 * @brief Data provider for custom user-defined bind groups.
 *
 * Scene nodes create providers in preRender() to supply custom uniform data.
 * Rendering system automatically creates, caches, and binds the bind groups.
 *
 * Usage Example:
 * @code
 *   struct MyUniforms {
 *       float time;
 *       glm::vec3 position;
 *   };
 *
 *   MyUniforms uniforms{currentTime, position};
 *
 *   outProviders.push_back(BindGroupDataProvider::create(
 *       "MyShader",
 *       "MyCustomData",
 *       uniforms,
 *       BindGroupReuse::PerFrame
 *   ));
 *
 *   uint64_t objectId = reinterpret_cast<uint64_t>(this);
 *   outProviders.push_back(BindGroupDataProvider::create(
 *       "MyShader",
 *       "PerObjectData",
 *       uniforms,
 *       BindGroupReuse::PerObject,
 *       objectId
 *   ));
 * @endcode
 */
struct BindGroupDataProvider
{
	std::string shaderName;							   ///< Name of shader this bind group belongs to
	std::string bindGroupName;						   ///< Name of bind group in shader (e.g., "MyCustomUniforms")
	std::vector<uint8_t> data;						   ///< Raw uniform data to upload
	size_t dataSize = 0;							   ///< Size of data in bytes
	webgpu::BindGroupReuse reuse = webgpu::BindGroupReuse::PerFrame; ///< Reuse policy from shader layout
	std::optional<uint64_t> instanceId = std::nullopt; ///< Instance ID: nullopt=shared (Global/PerFrame), value=per-instance (PerObject/PerMaterial)

	/**
	 * @brief Creates a bind group data provider.
	 * @param shader Name of the shader
	 * @param bindGroup Name of the bind group in the shader
	 * @param dataPtr Pointer to uniform data
	 * @param size Size of data in bytes
	 * @param reuse Reuse policy (should match shader's bind group layout)
	 * @param instanceId Instance ID: std::nullopt=shared (Global/PerFrame), value=per-instance (PerObject/PerMaterial)
	 */
	static BindGroupDataProvider create(
		const std::string &shader,
		const std::string &bindGroup,
		const void *dataPtr,
		size_t size,
		webgpu::BindGroupReuse reuse = webgpu::BindGroupReuse::PerFrame,
		std::optional<uint64_t> instanceId = std::nullopt
	)
	{
		BindGroupDataProvider provider;
		provider.shaderName = shader;
		provider.bindGroupName = bindGroup;
		provider.dataSize = size;
		provider.reuse = reuse;
		provider.instanceId = instanceId;
		provider.data.resize(size);
		std::memcpy(provider.data.data(), dataPtr, size);
		return provider;
	}

	/**
	 * @brief Creates a bind group data provider from a struct.
	 * @tparam T Uniform struct type
	 * @param shader Name of the shader
	 * @param bindGroup Name of the bind group in the shader
	 * @param uniforms Reference to uniform data
	 * @param reuse Reuse policy (should match shader's bind group layout)
	 * @param instanceId Instance ID: std::nullopt=shared (Global/PerFrame), value=per-instance (PerObject/PerMaterial)
	 */
	template <typename T>
	static BindGroupDataProvider create(
		const std::string &shader,
		const std::string &bindGroup,
		const T &uniforms,
		webgpu::BindGroupReuse reuse = webgpu::BindGroupReuse::PerFrame,
		std::optional<uint64_t> instanceId = std::nullopt
	)
	{
		return create(shader, bindGroup, &uniforms, sizeof(T), reuse, instanceId);
	}
};

} // namespace engine::rendering
