#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "engine/resources/loaders/LoaderBase.h"
#include "engine/resources/Image.h"

namespace engine::resources::loaders
{

class ImageLoader : public LoaderBase<engine::resources::Image::Ptr>
{
  public:
	explicit ImageLoader(std::filesystem::path basePath = {}) : LoaderBase(std::move(basePath)) {}
	~ImageLoader() = default;

	/**
	 * @brief Loads an image from file.
	 *        Supports LDR (png, jpg, bmp) and HDR (exr, hdr) automatically.
	 * @param file Relative or absolute path to the image file.
	 * @return Optional Image object, std::nullopt if loading failed.
	 */
	[[nodiscard]]
	std::optional<Loaded> load(const std::filesystem::path &file) override;

	

  private:
	std::filesystem::path m_basePath;
};

} // namespace engine::resources::loaders
