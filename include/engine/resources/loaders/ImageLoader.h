#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "engine/resources/Image.h"
#include "engine/resources/loaders/LoaderBase.h"

namespace engine::resources::loaders
{

/**
 * @brief Loads 2D image files into CPU-side Image resources.
 *
 * Responsibilities:
 *  - Disk I/O
 *  - Pixel format conversion (RGB → RGBA)
 *  - HDR vs LDR detection
 *
 * Non-responsibilities:
 *  - GPU upload
 *  - Sampler configuration
 *  - Material semantics
 */
class ImageLoader final : public LoaderBase<engine::resources::Image::Ptr>
{
  public:
	explicit ImageLoader(std::filesystem::path basePath = {}) : LoaderBase(std::move(basePath))
	{
	}

	~ImageLoader() override = default;

	/**
	 * @brief Loads an image from disk.
	 *
	 * Supported formats:
	 *  - LDR: png, jpg, jpeg, bmp, tga
	 *  - HDR: hdr (Radiance)
	 *
	 * Notes:
	 *  - RGB images are expanded to RGBA for WebGPU compatibility.
	 *  - EXR is NOT supported by this loader.
	 *
	 * @param file Relative or absolute file path.
	 * @return Loaded Image resource, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<Loaded> load(const std::filesystem::path &file) override;

	/**
	 * @brief Creates an empty image with specified dimensions and format. Useful for render targets
	 * or procedural textures.
	 * @param width Image width in pixels.
	 * @param height Image height in pixels.
	 * @param format Optional image format; defaults to LDR RGBA8 if not provided
	 */
	[[nodiscard]]
	std::optional<Loaded> createEmpty(
		std::uint32_t width,
		std::uint32_t height,
		std::optional<ImageFormat::Type> format
	);

	/**
	 * @brief Saves an Image resource to disk as a PNG file.
	 * @param image The Image resource to save.
	 * @param filePath The file path to save the image to.
	 * @return True on success, false on failure.
	 * @note Only supports saving in PNG format, regardless of the original image format.
	 */
	bool saveAsPNG(const Image &image, const std::filesystem::path &filePath) const;

  private:
	[[nodiscard]]
	static bool isHDRImage(const std::filesystem::path &path);

	[[nodiscard]]
	std::optional<Loaded> loadHDR(const std::filesystem::path &fullPath);

	[[nodiscard]]
	std::optional<Loaded> loadLDR(const std::filesystem::path &fullPath);
};

} // namespace engine::resources::loaders
