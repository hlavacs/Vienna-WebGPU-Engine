#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

#include "engine/debug/Loggable.h"
#include "engine/rendering/Texture.h"

namespace engine::resources::loaders
{

	class TextureLoader : public engine::debug::Loggable
	{
	public:
		explicit TextureLoader(std::filesystem::path basePath = {}, std::shared_ptr<spdlog::logger> logger = nullptr);
		~TextureLoader() = default;

		/**
		 * @brief Loads a texture from a file.
		 * @param file Path to the image file, relative or absolute.
		 * @return Optional Texture object, std::nullopt if loading failed.
		 */
		[[nodiscard]]
		virtual std::optional<engine::rendering::Texture::Ptr> load(const std::filesystem::path &file);

		// Getters / setters
		const std::filesystem::path &getBasePath() const { return m_basePath; }
		void setBasePath(const std::filesystem::path &basePath) { m_basePath = basePath; }

		const std::shared_ptr<spdlog::logger> &getLogger() const { return m_logger; }
		void setLogger(const std::shared_ptr<spdlog::logger> &logger) { m_logger = logger; }

	protected:

		std::filesystem::path m_basePath;
	};

} // namespace engine::resources::loaders
