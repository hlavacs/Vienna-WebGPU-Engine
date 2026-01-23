#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace engine::rendering
{

// Unified shadow model: single buffer for all shadow types (2D and cube)
// Lights reference shadows via shadowIndex in their uniform
// Each shadow entry can be either 2D (directional/spot) or cube (point) based on shadowType
// For CSM, multiple ShadowUniforms are used (one per cascade)
struct ShadowUniform
{
	glm::mat4 viewProj = glm::mat4(1.0f); //< 64 bytes - Used for spot, directional, CSM
	glm::vec3 lightPos = glm::vec3(0.0f); //< 12 bytes - Used for point lights
	float near = 0.1f;					  //< 4 bytes (total: 80 bytes)
	float far = 100.0f;					  //< 4 bytes
	float bias = 0.002f;				  //< 4 bytes
	float normalBias = 0.3f;			  //< 4 bytes
	float texelSize = 1.0f / 2048.0f;	  //< 4 bytes (total: 96 bytes)
	uint32_t pcfKernel = 1;				  //< 4 bytes
	uint32_t shadowType = 0;			  //< 4 bytes (0 = 2D shadow, 1 = cube shadow)
	uint32_t textureIndex = 0;			  //< 4 bytes - layer in correct texture array (total: 108 bytes)
	float cascadeSplit = 1.0f;			  //< 4 bytes - far plane distance for this cascade (CSM only) (total: 112 bytes)
};
static_assert(sizeof(ShadowUniform) % 16 == 0, "ShadowUniform must match WGSL layout");

// Shadow pass specific uniforms for 2D shadow maps (directional/spot lights)
struct ShadowPass2DUniforms
{
	glm::mat4 lightViewProjectionMatrix = glm::mat4(1.0f);
};
static_assert(sizeof(ShadowPass2DUniforms) % 16 == 0, "ShadowPassUniforms2D must match WGSL layout");

// Shadow pass specific uniforms for cube shadow maps (point lights)
struct ShadowPassCubeUniforms
{
	glm::vec3 lightPosition = glm::vec3(0.0f);
	float farPlane = 100.0f;
};
static_assert(sizeof(ShadowPassCubeUniforms) % 16 == 0, "ShadowPassUniformsCube must match WGSL layout");

} // namespace engine::rendering
