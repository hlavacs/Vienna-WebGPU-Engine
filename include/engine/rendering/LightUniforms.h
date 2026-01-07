#pragma once
#include <glm/glm.hpp>

namespace engine::rendering
{

struct LightStruct
{
	glm::mat4 transform = glm::mat4(1.0f); //< 64 bytes
	glm::vec3 color = {1.0f, 1.0f, 1.0f};  //< 12 bytes
	float intensity = 1.0f;				   //< 4 bytes (total: 80 bytes)
	uint32_t light_type = 0;			   //< 4 bytes (0 = ambient, 1 = directional, 2 = point, 3 = spot)
	float spot_angle = 0.5f;			   //< 4 bytes
	float spot_softness = 0.2f;			   //< 4 bytes
	float range = 10.0f;				   //< 4 bytes (total: 96 bytes)
	int32_t shadowIndex = -1;			   //< 4 bytes (< 0 = no shadows, >= 0 = index in shadow map array)
	float _pad1 = 0.0f;					   //< 4 bytes
	float _pad2 = 0.0f;					   //< 4 bytes
	float _pad3 = 0.0f;					   //< 4 bytes (total: 112 bytes, aligned to 16)
};
static_assert(sizeof(LightStruct) % 16 == 0, "LightStruct must match WGSL layout");

struct LightsBuffer
{
	uint32_t count = 0;
	float _pad1 = 0.0f;
	float _pad2 = 0.0f;
	float _pad3 = 0.0f;
};
static_assert(sizeof(LightsBuffer) % 16 == 0, "LightsBuffer must match WGSL layout");

// Shadow data for directional and spot lights (2D shadow maps)
struct Shadow2D
{
	glm::mat4 lightViewProjection = glm::mat4(1.0f);
	float bias = 0.005f;
	float normalBias = 0.01f;
	float texelSize = 1.0f / 2048.0f; // Assuming 2048x2048 shadow maps
	uint32_t pcfKernel = 1;			  // PCF kernel size (1 = 3x3, 2 = 5x5, etc.)
};
static_assert(sizeof(Shadow2D) % 16 == 0, "Shadow2D must match WGSL layout");

// Shadow data for point lights (cube shadow maps)
struct ShadowCube
{
	glm::vec3 lightPosition = glm::vec3(0.0f);
	float bias = 0.005f;
	float texelSize = 1.0f / 1024.0f; // Assuming 1024x1024 cube faces
	uint32_t pcfKernel = 1;			  // PCF kernel size
	glm::vec2 _pad = glm::vec2(0.0f); // Padding to maintain 16-byte alignment
};
static_assert(sizeof(ShadowCube) % 16 == 0, "ShadowCube must match WGSL layout");

} // namespace engine::rendering
