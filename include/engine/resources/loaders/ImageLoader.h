#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "engine/resources/loaders/LoaderBase.h"
#include "engine/resources/Image.h"

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
    explicit ImageLoader(std::filesystem::path basePath = {})
        : LoaderBase(std::move(basePath))
    {}

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
    std::optional<Loaded> load(const std::filesystem::path& file) override;

private:
    [[nodiscard]]
    static bool isHDRImage(const std::filesystem::path& path);

    [[nodiscard]]
    std::optional<Loaded> loadHDR(const std::filesystem::path& fullPath);

    [[nodiscard]]
    std::optional<Loaded> loadLDR(const std::filesystem::path& fullPath);
};

} // namespace engine::resources::loaders
