#include "engine/resources/TextureManager.h"
#include "engine/resources/Image.h"

namespace engine::resources
{

std::optional<TextureManager::TexturePtr> TextureManager::createImageTexture(
	engine::resources::Image::Ptr image,
	std::optional<std::filesystem::path> filePath
)
{
	if (image == nullptr)
		return std::nullopt;

	std::string key;
	if (filePath.has_value())
		key = filePath->string();

	{
		std::scoped_lock lock(m_mutex);
		if (!key.empty())
		{
			auto it = m_imageCache.find(key);
			if (it != m_imageCache.end())
			{
				auto cached = get(it->second);
				if (cached && *cached)
					return *cached;
			}
		}
	}

	auto texture = std::shared_ptr<engine::rendering::Texture>(
		new engine::rendering::Texture(
			engine::rendering::Texture::Type::Image,
			std::move(image),
			filePath.value_or(std::filesystem::path{})
		)
	);

	auto handleOpt = add(texture);
	if (!handleOpt)
		return std::nullopt;

	if (!key.empty())
	{
		std::scoped_lock lock(m_mutex);
		m_imageCache[key] = *handleOpt;
	}

	return texture;
}

std::optional<TextureManager::TexturePtr> TextureManager::createDepthTexture(
	uint32_t width,
	uint32_t height
)
{
	auto texture = std::shared_ptr<engine::rendering::Texture>(
		new engine::rendering::Texture(
			engine::rendering::Texture::Type::DepthStencil,
			width,
			height,
			1, // single channel, placeholder
			std::filesystem::path{}
		)
	); // no file path

	texture->setName("DepthStencilTexture");

	auto handleOpt = add(texture);
	if (!handleOpt)
		return std::nullopt;

	return texture;
}

std::optional<TextureManager::TexturePtr> TextureManager::createSurfaceTexture(
	uint32_t width,
	uint32_t height,
	uint32_t channels
)
{
	auto texture = std::shared_ptr<engine::rendering::Texture>(
		new engine::rendering::Texture(
			engine::rendering::Texture::Type::Surface,
			width,
			height,
			channels,
			std::filesystem::path{}
		)
	); // no file path

	texture->setName("SurfaceTexture");

	auto handleOpt = add(texture);
	if (!handleOpt)
		return std::nullopt;

	return texture;
}

std::optional<TextureManager::TexturePtr> TextureManager::createTextureFromFile(
	const std::filesystem::path &filepath,
	bool forceReload
)
{
	std::filesystem::path texturePath;
	if (filepath.is_absolute())
		texturePath = filepath;
	else
		texturePath = m_loader->getBasePath() / filepath;

	std::string key = texturePath.string();

	{
		std::scoped_lock lock(m_mutex);
		if (!forceReload)
		{
			auto it = m_imageCache.find(key);
			if (it != m_imageCache.end())
			{
				// Access map directly to avoid deadlock (we already hold the mutex)
				auto resIt = m_resources.find(it->second);
				if (resIt != m_resources.end() && resIt->second)
					return resIt->second;
			}
		}
	}

	auto result = m_loader->load(texturePath);
	if (!result)
		return std::nullopt;

	auto texture = createImageTexture(result.value(), texturePath);
	return texture;
}

std::optional<TextureManager::TexturePtr> TextureManager::getTextureByPath(
	const std::filesystem::path &filepath
) const
{
	std::filesystem::path texturePath;
	if (filepath.is_absolute())
		texturePath = filepath;
	else
		texturePath = m_loader->getBasePath() / filepath;

	std::string key = texturePath.string();

	std::scoped_lock lock(m_mutex);
	auto it = m_imageCache.find(key);
	if (it == m_imageCache.end())
		return std::nullopt;

	// Access map directly to avoid deadlock (we already hold the mutex)
	auto resIt = m_resources.find(it->second);
	if (resIt != m_resources.end())
		return resIt->second;

	return std::nullopt;
}

} // namespace engine::resources
