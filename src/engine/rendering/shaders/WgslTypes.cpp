#include "engine/rendering/shaders/WgslTypes.h"

namespace engine::rendering::shaders
{

std::string_view wgslTypeName(WgslType t)
{
	switch (t)
	{
		case WgslType::F32:  return "f32";
		case WgslType::I32:  return "i32";
		case WgslType::U32:  return "u32";
		case WgslType::Vec2: return "vec2<f32>";
		case WgslType::Vec3: return "vec3<f32>";
		case WgslType::Vec4: return "vec4<f32>";
		case WgslType::Mat4: return "mat4x4<f32>";
	}
	return "?";
}

uint32_t wgslTypeSize(WgslType t)
{
	switch (t)
	{
		case WgslType::F32:
		case WgslType::I32:
		case WgslType::U32:  return 4;
		case WgslType::Vec2: return 8;
		case WgslType::Vec3: return 12; // Element size, NOT alignment-padded. UBO use rounds via padding.
		case WgslType::Vec4: return 16;
		case WgslType::Mat4: return 64;
	}
	return 0;
}

uint32_t wgslTypeAlign(WgslType t)
{
	switch (t)
	{
		case WgslType::F32:
		case WgslType::I32:
		case WgslType::U32:  return 4;
		case WgslType::Vec2: return 8;
		case WgslType::Vec3: return 16; // std140: a bare vec3 still aligns to 16, leaving 4 trailing bytes
		case WgslType::Vec4: return 16;
		case WgslType::Mat4: return 16;
	}
	return 1;
}

} // namespace engine::rendering::shaders
