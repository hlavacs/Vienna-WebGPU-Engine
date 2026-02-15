#pragma once

namespace engine::rendering
{
/**
 * @brief Determines whether a bind group can be reused across shaders/objects.
 */
enum class BindGroupReuse
{
	Global,		 //< device-wide, never changes
	PerFrame,	 //< per camera or per frame
	PerMaterial, //< tied to material instance
	PerObject	 //< per render item / draw call
};

/**
 * @brief Semantic type of a bind group.
 */
enum class BindGroupType
{
	Frame,
	Light,
	Mipmap,
	Object,
	Material,
	Shadow,
	ShadowPass2D,
	ShadowPassCube,
	Debug,
	Custom,
};

/**
 * @brief Type of a single binding inside a bind group.
 */
enum class BindingType
{
	UniformBuffer,
	StorageBuffer,
	Texture,
	MaterialTexture,
	Sampler
};
} // namespace engine::rendering