#include "engine/resources/TextureManager.h"

namespace engine::resources
{
std::optional<TextureManager::TexturePtr> TextureManager::createTexture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixels)
{
	auto texture = std::make_shared<engine::rendering::Texture>(width, height, channels, std::move(pixels));
	auto handleOpt = add(texture);
	if (!handleOpt)
		return std::nullopt;
	return texture;
}

std::optional<TextureManager::TexturePtr> TextureManager::createTextureFromFile(const path &filepath, bool forceReload)
{
	std::string key = (m_loader->getBasePath() / filepath).string();
	if (!forceReload)
	{
		// Check if texture already loaded
		auto it = m_pathToTexture.find(key);
		if (it != m_pathToTexture.end())
		{
			auto texOpt = get(it->second);
			if (texOpt && *texOpt)
			{
				return *texOpt;
			}
		}
	}

	auto result = m_loader->load(filepath);
	if (!result)
		return std::nullopt;

	auto texture = result.value();
	texture->setName(filepath.filename().string());
	texture->setFilePath(key);

	auto handleOpt = add(texture);
	if (!handleOpt)
		return std::nullopt;

	m_pathToTexture[key] = *handleOpt;

	return texture;
}

std::optional<TextureManager::TexturePtr> TextureManager::getTextureByPath(const path &filepath) const
{
	std::string key = (m_loader->getBasePath() / filepath).string();
	auto it = m_pathToTexture.find(key);
	if (it == m_pathToTexture.end())
		return std::nullopt;
	return get(it->second);
}

} // namespace engine::resources
