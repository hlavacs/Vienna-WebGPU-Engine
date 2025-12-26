namespace engine::rendering
{
enum class ShaderFeature : uint32_t
{
	None = 0,
	UsesNormalMap = 1 << 0,
	AlphaTest = 1 << 1,
	Skinned = 1 << 2,
	Instanced = 1 << 3,
};

inline constexpr ShaderFeature operator|(ShaderFeature a, ShaderFeature b) noexcept
{
	return static_cast<ShaderFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr ShaderFeature operator&(ShaderFeature a, ShaderFeature b) noexcept
{
	return static_cast<ShaderFeature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr ShaderFeature operator~(ShaderFeature a) noexcept
{
	return static_cast<ShaderFeature>(~static_cast<uint32_t>(a));
}

}