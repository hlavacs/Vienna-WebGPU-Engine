#pragma once

#include <array>
#include <memory>

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
// WebGPURenderPassContext is declared as a struct - matching declaration here
// keeps MSVC name mangling consistent (class vs struct mangles differently).
struct WebGPURenderPassContext;
class WebGPUTexture;

/**
 * @class GBuffer
 * @brief Container for deferred-shading geometry-pass render targets.
 *
 * Owns the four color attachments and the depth attachment used by the geometry
 * pass of a deferred renderer. All GPU resources are created through
 * @ref WebGPUTextureFactory so cleanup follows the engine's standard
 * @ref WebGPUTexture lifetime rules.
 *
 * **Layout** (matches @c deferred_composition.wgsl ):
 * | Slot | Attachment | Format            | Channels                                   |
 * |------|------------|-------------------|--------------------------------------------|
 * | 0    | Position   | RGBA16Float       | xyz = world position, w = view-space depth |
 * | 1    | Normal     | RGBA16Float       | xyz = world normal,   w = view-space depth |
 * | 2    | Albedo     | RGBA8UnormSrgb    | rgb = base color,     a = coverage         |
 * | 3    | Material   | RGBA8Unorm        | r = roughness, g = metallic, b = ao        |
 * | -    | Depth      | Depth32Float      | shared with composition / forward passes   |
 *
 * The container is sized in pixels and can be resized cheaply: a no-op when
 * the requested size matches the current size, otherwise all attachments are
 * recreated through the factory.
 *
 * The class also exposes a fully populated @ref WebGPURenderPassContext for the
 * geometry pass, so callers do not have to build attachments by hand.
 */
class GBuffer
{
public:
	/// Index constants for the color attachment array.
	static constexpr size_t SLOT_POSITION = 0;
	static constexpr size_t SLOT_NORMAL = 1;
	static constexpr size_t SLOT_ALBEDO = 2;
	static constexpr size_t SLOT_MATERIAL = 3;
	static constexpr size_t COLOR_ATTACHMENT_COUNT = 4;

	// Pixel formats used by each slot (kept here so other systems can match them).
	// wgpu::TextureFormat is a thin C++ wrapper struct around the C enum and
	// its constructor isn't constexpr, hence `inline const` rather than constexpr.
	static inline const wgpu::TextureFormat FORMAT_POSITION = wgpu::TextureFormat::RGBA16Float;
	static inline const wgpu::TextureFormat FORMAT_NORMAL = wgpu::TextureFormat::RGBA16Float;
	static inline const wgpu::TextureFormat FORMAT_ALBEDO = wgpu::TextureFormat::RGBA8UnormSrgb;
	static inline const wgpu::TextureFormat FORMAT_MATERIAL = wgpu::TextureFormat::RGBA8Unorm;
	static inline const wgpu::TextureFormat FORMAT_DEPTH = wgpu::TextureFormat::Depth32Float;

	/**
	 * @brief Constructs a G-buffer of the given pixel size.
	 *
	 * @param context  WebGPU context that owns the texture factory.
	 * @param width    Width in pixels (must be > 0).
	 * @param height   Height in pixels (must be > 0).
	 */
	GBuffer(WebGPUContext &context, uint32_t width, uint32_t height);

	~GBuffer() = default;

	GBuffer(const GBuffer &) = delete;
	GBuffer &operator=(const GBuffer &) = delete;
	GBuffer(GBuffer &&) = delete;
	GBuffer &operator=(GBuffer &&) = delete;

	/**
	 * @brief Resizes all attachments to the given pixel size.
	 *
	 * Does nothing if the size already matches the current size. The cached
	 * render pass context (if any) is invalidated so the next call to
	 * @ref getRenderPassContext rebuilds it.
	 *
	 * @param width  New width in pixels (must be > 0).
	 * @param height New height in pixels (must be > 0).
	 */
	void resize(uint32_t width, uint32_t height);

	/// @return Width in pixels.
	[[nodiscard]] uint32_t getWidth() const { return m_width; }

	/// @return Height in pixels.
	[[nodiscard]] uint32_t getHeight() const { return m_height; }

	/// @return The four color attachment textures in slot order.
	[[nodiscard]] const std::array<std::shared_ptr<WebGPUTexture>, COLOR_ATTACHMENT_COUNT> &
	getColorTextures() const { return m_colorTextures; }

	/// @return The depth attachment texture.
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getDepthTexture() const { return m_depthTexture; }

	/// Convenience accessors for individual slots.
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getPositionTexture() const { return m_colorTextures[SLOT_POSITION]; }
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getNormalTexture()   const { return m_colorTextures[SLOT_NORMAL]; }
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getAlbedoTexture()   const { return m_colorTextures[SLOT_ALBEDO]; }
	[[nodiscard]] std::shared_ptr<WebGPUTexture> getMaterialTexture() const { return m_colorTextures[SLOT_MATERIAL]; }

	/**
	 * @brief Returns a render pass context that targets this G-buffer.
	 *
	 * The context is built lazily on first call and cached until the G-buffer
	 * is resized. All four color attachments are cleared to
	 * @p colorClear and the depth attachment is cleared to 1.0. Store-op is
	 * Store on every attachment so the composition pass can sample them.
	 *
	 * @param colorClear Clear color used for every color attachment.
	 * @return Shared render pass context ready to use with a command encoder.
	 */
	std::shared_ptr<WebGPURenderPassContext> getRenderPassContext(
		const glm::vec4 &colorClear = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
	);

private:
	/// Allocates / re-allocates every attachment via the texture factory.
	void createTextures();

	WebGPUContext &m_context;
	uint32_t m_width;
	uint32_t m_height;

	std::array<std::shared_ptr<WebGPUTexture>, COLOR_ATTACHMENT_COUNT> m_colorTextures{};
	std::shared_ptr<WebGPUTexture> m_depthTexture;

	std::shared_ptr<WebGPURenderPassContext> m_cachedPassContext;
	glm::vec4 m_cachedClearColor{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace engine::rendering::webgpu
