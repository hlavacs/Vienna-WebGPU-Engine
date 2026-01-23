namespace engine::rendering
{
/**
 * @enum ShaderType
 * @brief Shader types defining pipeline behavior.
 */
enum class ShaderType
{
	Lit,
	Unlit,
	Debug,
	PostProcess,
	Custom
};
} // namespace engine::rendering