#pragma once

namespace engine::rendering
{
enum class ColorSpace
{
	sRGB, // Standard RGB color space with gamma correction
	Linear // Linear color space without gamma correction
};
} // namespace engine::rendering