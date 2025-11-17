#pragma once
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/scene/CameraNode.h"

namespace engine::rendering
{

class Renderer
{
  public:
	virtual ~Renderer() = default;

	// Initialize GPU resources or backend
	virtual void initialize() = 0;

	// Start a new frame, update frame uniforms
	virtual void beginFrame(const scene::CameraNode &camera) = 0;

	// Render collected scene data
	virtual void renderScene(const RenderCollector &collector) = 0;

	// Submit frame to GPU
	virtual void submitFrame() = 0;

	// Shutdown renderer and release GPU resources
	virtual void shutdown() = 0;

  protected:
	// Cached frame data (view, projection, camera position, time)
	FrameUniforms m_frameUniforms;
};

} // namespace engine::rendering
