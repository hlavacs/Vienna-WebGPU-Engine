#pragma once

#include <array>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/resources/ResourceManager.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/Scene.h"
#include "engine/scene/entity/LightNode.h"
#include "engine/scene/entity/Node.h"
#include "engine/scene/entity/RenderNode.h"

// Forward declare
struct SDL_Window;
union SDL_Event;

namespace engine
{
class Application
{
  public:
	explicit Application();

	// A function called only once at the beginning. Returns false is init failed.
	bool onInit();

	// A function called at each frame, guaranteed never to be called before `onInit`.
	void onFrame();

	// A function called only once at the very end.
	void onFinish();

	// A function that tells if the application is still running.
	bool isRunning();

	// A function called when the window is resized.
	void onResize();

	// Mouse events
	void onMouseMove(double xpos, double ypos, float deltaTime);
	void onMouseButton(int button, bool pressed, int x, int y);
	void onScroll(double xoffset, double yoffset, float deltaTime);

  private:
	bool initRenderer();
	void terminateRenderer();

	bool initGeometry();
	void terminateGeometry();

	bool initUniforms();
	void terminateUniforms();

	void updateProjectionMatrix();
	void updateDragInertia(float deltaTime);

	// Orbit camera methods
	void updateOrbitCamera();
	void resetCamera();

	bool initGui();										// called in onInit
	void terminateGui();								// called in onFinish
	void updateGui(wgpu::RenderPassEncoder renderPass); // called in onFrame
	void processSDLEvents(float deltaTime);				// called in onFrame

  public:
	SDL_Window *m_window = nullptr;

  private:
	std::shared_ptr<engine::resources::ResourceManager> m_resourceManager = nullptr;
	std::shared_ptr<engine::rendering::webgpu::WebGPUContext> m_context = nullptr;
	std::unique_ptr<engine::rendering::Renderer> m_renderer = nullptr;

	bool m_shouldClose = false;

	// (Just aliases to make notations lighter)
	using mat4x4 = glm::mat4x4;
	using vec4 = glm::vec4;
	using vec3 = glm::vec3;
	using vec2 = glm::vec2;

	struct DragState
	{
		// Whether a drag action is ongoing (i.e., we are between mouse press and mouse release)
		bool active = false;
		// The position of the mouse at the beginning of the drag action
		vec2 startMouse;

		// Constant settings
		float sensitivity = 1.0f;
		float scrollSensitivity = 25.0f;

		// Inertia
		vec2 velocity = {0.0, 0.0};
		vec2 previousDelta;
		float inertiaDecay = 0.9f;

		// Orbit camera parameters
		vec3 targetPoint = vec3(0.0f); // Point to orbit around (origin by default)
		float azimuth = 0.0f;		   // Horizontal angle (around Y axis)
		float elevation = 0.0f;		   // Vertical angle (0 is equator, pi/2 is north pole)
		float distance = 5.0f;		   // Distance from target
	};

	// Window and Device
	int m_currentWidth = 0, m_currentHeight = 0;

	// Depth Buffer (kept for backwards compatibility with old code)
	wgpu::TextureFormat m_depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
	std::shared_ptr<engine::rendering::webgpu::WebGPUDepthTexture> m_depthBuffer;
	wgpu::Texture m_depthTexture = nullptr;
	wgpu::TextureView m_depthTextureView = nullptr;

	// Uniforms (kept for Application use - Renderer has its own frame uniforms)
	engine::rendering::FrameUniforms m_frameUniforms;

	// Lights - managed as LightNodes in the scene graph
	std::vector<std::shared_ptr<engine::scene::entity::LightNode>> m_lightNodes;
	std::map<size_t, glm::vec3> m_lightDirectionsUI; // Store UI direction angles in degrees

	// Material (kept for backwards compatibility)
	std::shared_ptr<engine::rendering::webgpu::WebGPUMaterial> m_material;

	// Scene Graph
	std::shared_ptr<engine::scene::Scene> m_scene;

	DragState m_drag;

	// Old models (kept for backwards compatibility - TODO: migrate to scene graph)
	std::vector<std::shared_ptr<engine::rendering::webgpu::WebGPUModel>> m_webgpuModels;

	// Helper methods for light management
	void addLight();
	void removeLight(size_t index);
};
} // namespace engine