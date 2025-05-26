#include "engine/resources/TextureManager.h"

namespace engine::resources
{
	std::optional<TextureManager::TextureResult> TextureManager::createTexture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixels)
	{
		auto texture = std::make_shared<engine::rendering::Texture>(width, height, channels, std::move(pixels));

		TextureHandle handle = texture->getHandle();
		m_textures.emplace(handle, texture);
		return TextureResult{handle, texture};
	}

	std::optional<TextureManager::TextureResult> TextureManager::createTextureFromFile(const path &filepath, bool forceReload)
	{
		std::string key = (m_loader->getBasePath() / filepath).string();
		if (!forceReload)
		{
			// Check if texture already loaded
			auto it = m_pathToTexture.find(key);
			if (it != m_pathToTexture.end())
			{
				TextureHandle existingHandle = it->second;
				auto texIt = m_textures.find(existingHandle);
				if (texIt != m_textures.end())
				{
					return TextureResult{existingHandle, texIt->second};
				}
			}
		}

		auto result = m_loader->load(filepath);
		if (!result)
			return std::nullopt;

		auto texture = result.value();
		texture->setName(filepath.filename().string());
		texture->setFilePath(key);

		auto handle = texture->getHandle();

		m_textures[handle] = texture;
		m_pathToTexture[key] = handle;

		return TextureResult{handle, texture};
	}

	std::optional<TextureManager::TexturePtr> TextureManager::getTextureByHandle(TextureHandle handle) const
	{
		auto it = m_textures.find(handle);
		if (it != m_textures.end())
			return it->second;
		return std::nullopt;
	}

	std::optional<TextureManager::TexturePtr> TextureManager::getTextureByID(uint64_t id) const
	{
		TextureHandle handle(id);
		return getTextureByHandle(handle);
	}

	std::optional<TextureManager::TexturePtr> TextureManager::getTextureByName(const std::string &name) const
	{
		for (const auto &[handle, texture] : m_textures)
		{
			if (texture && texture->getName().has_value() && texture->getName().value() == name)
			{
				return texture;
			}
		}
		return std::nullopt;
	}

	std::optional<TextureManager::TexturePtr> TextureManager::getTextureByPath(const path &filepath) const
	{
		auto it = m_pathToTexture.find(filepath.string());
		if (it == m_pathToTexture.end())
		{
			return std::nullopt;
		}
		return getTextureByHandle(it->second);
	}

} // namespace engine::resources
