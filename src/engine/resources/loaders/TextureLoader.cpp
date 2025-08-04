#include "engine/resources/loaders/TextureLoader.h"
#include "engine/stb_image.h"

namespace engine::resources::loaders
{

TextureLoader::TextureLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger) :
	engine::debug::Loggable(std::move(logger)), m_basePath(std::move(basePath)) {}

std::optional<engine::rendering::Texture::Ptr> TextureLoader::load(const std::filesystem::path &file)
{
	auto fullPath = m_basePath / file;

	logInfo("Loading texture from '{}'", fullPath.string());

	int width = 0, height = 0, channels = 0;
	// ToDo: no longer Force 4 channels. Currently needed because or fixed RGBA8Unorm Format
	unsigned char *data = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
	channels = 4; // RGBA8Unorm forces 4 channels
	if (!data)
	{
		logError("Failed to load texture '{}': {}", fullPath.string(), stbi_failure_reason());
		return std::nullopt;
	}

	size_t dataSize = static_cast<size_t>(width) * height * channels;
	std::vector<uint8_t> pixels(data, data + dataSize);
	stbi_image_free(data);

	engine::rendering::Texture::Ptr texture = std::make_shared<engine::rendering::Texture>(
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height),
		static_cast<uint32_t>(channels),
		std::move(pixels)
	);

	texture->generateMipmaps();

	logInfo("Loaded texture '{}', size: {}x{}, channels: {}", fullPath.string(), width, height, channels);

	return texture;
}

} // namespace engine::resources::loaders
