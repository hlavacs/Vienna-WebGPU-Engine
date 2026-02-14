#pragma once
#include "engine/math/Frustum.h"
#include "engine/math/Rect.h"
#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/Texture.h"
#include "engine/scene/Transform.h"
#include "engine/scene/nodes/Node.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/SpatialNode.h"
#include "engine/scene/nodes/UpdateNode.h"
#include <glm/glm.hpp>
#include <memory>
#include <optional>

namespace engine::scene::nodes
{

/**
 * @brief Node representing a camera in the scene.
 *
 * This node contains all camera-related information:
 * - Transform and orientation
 * - Projection parameters (FOV, aspect, near/far, orthographic size)
 * - Render settings (viewport, clear flags, background color, MSAA, HDR)
 * - Optional render target (texture or surface)
 *
 * Inherits from SpatialNode (for position/rotation/scale),
 * UpdateNode (for per-frame updates), and RenderNode (for pre-render hooks).
 */
class CameraNode : public nodes::UpdateNode, public nodes::RenderNode, public nodes::SpatialNode
{
  public:
	using Ptr = std::shared_ptr<CameraNode>;

	/**
	 * @brief Constructs a new CameraNode with default parameters.
	 */
	CameraNode();
	~CameraNode() override = default;

	/**
	 * @brief Rotate the camera to look at a target point in world space.
	 * @param target Target point to look at.
	 * @param up Up direction (default is (0,1,0)).
	 */
	void lookAt(const glm::vec3 &target, const glm::vec3 &up = glm::vec3(0, 1, 0));

	/**
	 * @brief Pan the camera horizontally and vertically in local space.
	 * @param deltaX Horizontal pan amount.
	 * @param deltaY Vertical pan amount.
	 */
	void pan(float deltaX, float deltaY);

	/**
	 * @brief Tilt the camera up/down in local space.
	 * @param deltaX Horizontal tilt (yaw).
	 * @param deltaY Vertical tilt (pitch).
	 */
	void tilt(float deltaX, float deltaY);

	/**
	 * @brief Move the camera forward/backward along its view direction.
	 * @param delta Positive moves forward, negative moves backward.
	 */
	void dolly(float delta);

	// =========================================================
	// Projection parameters
	// =========================================================

	/**
	 * @brief Set the camera's field of view in degrees.
	 * @param fovDegrees Field of view angle.
	 */
	void setFov(float fovDegrees);

	/**
	 * @brief Set near and far clipping planes.
	 * @param near Near plane distance.
	 * @param far Far plane distance.
	 */
	void setNearFar(float near, float far);

	/**
	 * @brief Set whether the camera uses perspective or orthographic projection.
	 * @param perspective True = perspective, false = orthographic.
	 */
	void setPerspective(bool perspective);

	/**
	 * @brief Set orthographic size (height of view volume) when in orthographic mode.
	 * @param size Orthographic height.
	 */
	void setOrthographicSize(float size);

	/**
	 * @brief Get the camera's field of view in degrees.
	 * @return Field of view angle.
	 */
	float getFov() const { return m_fov; };
	/**
	 * @brief Get the camera's aspect ratio (width / height) based on viewport.
	 * @return Aspect ratio value.
	 */
	float getAspect() const { return m_aspect; };
	/**
	 * @brief Get the near clipping plane distance.
	 * @return Near plane distance.
	 */
	float getNear() const { return m_near; };
	/**
	 * @brief Get the far clipping plane distance.
	 * @return Far plane distance.
	 */
	float getFar() const { return m_far; };
	/**
	 * @brief Check if the camera is in perspective mode.
	 * @return True if perspective, false if orthographic.
	 */
	bool isPerspective() const { return m_isPerspective; };
	/**
	 * @brief Get the orthographic size (height) when in orthographic mode.
	 * @return Orthographic height.
	 */
	float getOrthographicSize() const { return m_orthographicSize; };

	// =========================================================
	// Rendering parameters
	// =========================================================

	/**
	 * @brief Set the viewport rectangle for this camera.
	 * @param viewport Viewport defined by minimum and maximum normalized corners.
	 * @note (0,0) is the top-left corner of the render target, (1,1) is the bottom-right.
	 * min: top-left corner of the viewport (normalized)
	 * max: bottom-right corner of the viewport (normalized)
	 * Example: min = (0.25, 0.25), max = (0.75, 0.75) sets a centered viewport covering half the width and height.
	 */
	void setViewport(const glm::vec4 &viewport) { m_viewport = math::Rect(viewport); }

	/**
	 * @brief Set the viewport rectangle for this camera.
	 * @param viewport Viewport defined by minimum and maximum normalized corners.
	 * @note (0,0) is the top-left corner of the render target, (1,1) is the bottom-right.
	 * min: top-left corner of the viewport (normalized)
	 * max: bottom-right corner of the viewport (normalized)
	 * Example: min = (0.25, 0.25), max = (0.75, 0.75) sets a centered viewport covering half the width and height.
	 */
	void setViewport(const math::Rect &viewport) { m_viewport = viewport; }

	/**
	 * @brief Get the camera viewport rectangle.
	 */
	const math::Rect getViewport() const { return m_viewport; }

	/**
	 * @brief Set the clear color for this camera.
	 * @param color RGBA color.
	 */
	void setBackgroundColor(const glm::vec4 &color) { m_clearColor = color; }

	/**
	 * @brief Get the clear color.
	 */
	const glm::vec4 &getBackgroundColor() const { return m_clearColor; }

	/**
	 * @brief Set camera clear flags.
	 * @param flags ClearFlags enum value.
	 */
	void setClearFlags(engine::rendering::ClearFlags flags) { m_clearFlags = flags; }

	/**
	 * @brief Get camera clear flags.
	 */
	engine::rendering::ClearFlags getClearFlags() const { return m_clearFlags; }

	/**
	 * @brief Set a render target (texture or surface) for this camera.
	 * @param targetTexture Optional handle to a texture.
	 * @note If no render target is set, the camera will set up an offscreen render target automatically.
	 */
	void setRenderTarget(std::optional<engine::rendering::Texture::Handle> targetTexture)
	{
		m_renderTexture = targetTexture;
	}

	/**
	 * @brief Get the current render target.
	 * @return Handle to the texture render target.
	 * @note May be an invalid handle if no target is set (offscreen target will be used then).
	 */
	std::optional<engine::rendering::Texture::Handle> getRenderTarget() const { return m_renderTexture; }

	/**
	 * @brief Set the rendering depth/order for this camera.
	 * @param depth Depth value. Lower values render first, higher values render on top.
	 * @note Default is 0. Use negative values for background cameras, positive for overlay cameras.
	 */
	void setDepth(int depth) { m_depth = depth; }

	/**
	 * @brief Get the rendering depth/order for this camera.
	 * @return Depth value.
	 */
	int getDepth() const { return m_depth; }

	/**
	 * @brief Enable or disable MSAA for this camera.
	 */
	void setMSAAEnabled(bool enabled) { m_msaa = enabled; }

	/**
	 * @brief Check whether MSAA is enabled.
	 */
	bool isMSAAEnabled() const { return m_msaa; }

	/**
	 * @brief Enable or disable HDR rendering for this camera.
	 */
	void setHDREnabled(bool enabled) { m_hdr = enabled; }

	/**
	 * @brief Check whether HDR is enabled.
	 */
	bool isHDREnabled() const { return m_hdr; }

	// =========================================================
	// Frustum
	// =========================================================

	/**
	 * @brief Get the camera frustum for culling.
	 * @return Frustum object.
	 */
	const engine::math::Frustum &getFrustum() const;

	// =========================================================
	// Matrices
	// =========================================================

	/**
	 * @brief Get the view matrix of the camera.
	 * @return View matrix.
	 */
	const glm::mat4 &getViewMatrix() const
	{
		updateMatrices();
		return m_viewMatrix;
	}
	/**
	 * @brief Get the projection matrix of the camera.
	 * @return Projection matrix.
	 */
	const glm::mat4 &getProjectionMatrix() const
	{
		updateMatrices();
		return m_projectionMatrix;
	}
	/**
	 * @brief Get the combined view-projection matrix of the camera.
	 * @return View-projection matrix.
	 */
	const glm::mat4 &getViewProjectionMatrix() const
	{
		updateMatrices();
		return m_viewProjectionMatrix;
	}

	/**
	 * @brief Get the world position of the camera.
	 */
	glm::vec3 getPosition() const;

	// =========================================================
	// Update / RenderNode overrides
	// =========================================================

	void update(float deltaTime) override;
	void lateUpdate(float deltaTime) override;
	void preRender(std::vector<engine::rendering::BindGroupDataProvider> &outProviders) override;

	/**
	 * @brief Camera nodes don't add themselves to the render collector.
	 * They are tracked separately by the scene as active cameras.
	 */
	void onRenderCollect(engine::rendering::RenderCollector &collector) override
	{
		// Cameras don't add themselves to the collector - they're managed by the scene
	}

	void onRenderAreaChanged(uint32_t width, uint32_t height)
	{
		if (height == 0)
			return;

		float newAspect = static_cast<float>(width) / static_cast<float>(height);
		if (m_aspect != newAspect)
		{
			m_aspect = newAspect;
			m_dirtyProjection = true;
		}
	}

	void onResize(uint32_t windowWidth, uint32_t windowHeight)
	{
		if (windowWidth == 0 || windowHeight == 0)
			return;

		auto pixelWidth = static_cast<uint32_t>(windowWidth * m_viewport.width());
		auto pixelHeight = static_cast<uint32_t>(windowHeight * m_viewport.height());

		onRenderAreaChanged(pixelWidth, pixelHeight);
	}

  private:
	// Recalculate view/projection matrices based on transform & projection
	void updateMatrices() const;

	// Recalculate frustum planes from view-projection matrix
	void updateFrustumPlanes() const;

  protected:
	mutable bool m_dirtyView = true;
	mutable bool m_dirtyProjection = true;
	mutable bool m_dirtyFrustum = true;

	mutable glm::mat4 m_viewMatrix = glm::mat4(1.0f);
	mutable glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
	mutable glm::mat4 m_viewProjectionMatrix = glm::mat4(1.0f);

	mutable engine::math::Frustum m_frustum{};

	// =========================================================
	// Projection parameters
	// =========================================================
	float m_fov = 45.0f;			 ///< Field of view in degrees
	float m_aspect = 16.0f / 9.0f;	 ///< Aspect ratio
	float m_near = 0.1f;			 ///< Near plane
	float m_far = 100.0f;			 ///< Far plane
	bool m_isPerspective = true;	 ///< Perspective or orthographic
	float m_orthographicSize = 5.0f; ///< Orthographic height

	// =========================================================
	// Rendering parameters
	// =========================================================
	math::Rect m_viewport = glm::vec4(0, 0, 1, 1);	///< Normalized viewport
	glm::vec4 m_clearColor = glm::vec4(0, 0, 0, 1); ///< Background clear color
	engine::rendering::ClearFlags m_clearFlags =
		engine::rendering::ClearFlags::SolidColor | engine::rendering::ClearFlags::Depth; ///< Clear flags
	std::optional<engine::rendering::Texture::Handle> m_renderTexture;					  ///< Target texture/surface
	int m_depth = 0;																	  ///< Rendering depth/order
	bool m_msaa = true;																	  ///< MSAA enabled
	bool m_hdr = false;																	  ///< HDR enabled
};

} // namespace engine::scene::nodes
