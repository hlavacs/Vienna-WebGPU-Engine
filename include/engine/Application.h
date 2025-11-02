#pragma once

#include <array>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include "engine/core/PathProvider.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/resources/ResourceManager.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/Scene.h"
#include "engine/scene/entity/Node.h"

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
	bool initSurface();
	void terminateSurface();

	bool initDepthBuffer();
	void terminateDepthBuffer();

	bool initRenderPipeline();
	void terminateRenderPipeline();
	bool reloadShader();

	bool initGeometry();
	void terminateGeometry();

	bool initUniforms();
	void terminateUniforms();

	bool initLightingUniforms();
	void terminateLightingUniforms();
	void updateLightingUniforms();

	bool initBindGroupLayout();
	bool initBindGroups();

	void updateProjectionMatrix();
	void updateViewMatrix();
	void updateDragInertia(float deltaTime);

	// Orbit camera methods
	void updateOrbitCamera();
	void resetCamera();

	bool initGui();										// called in onInit
	void terminateGui();								// called in onFinish
	void updateGui(wgpu::RenderPassEncoder renderPass); // called in onFrame
	void processSDLEvents(float deltaTime);							// called in onFrame

  public:
	SDL_Window *m_window = nullptr;

  private:
	std::shared_ptr<engine::resources::ResourceManager> m_resourceManager = nullptr;
	std::shared_ptr<engine::rendering::webgpu::WebGPUContext> m_context = nullptr;

	bool m_shouldClose = false;

	// (Just aliases to make notations lighter)
	using mat4x4 = glm::mat4x4;
	using vec4 = glm::vec4;
	using vec3 = glm::vec3;
	using vec2 = glm::vec2;

	/**
	 * Frame uniforms (camera, time) - matches shader's FrameUniforms
	 */
	struct FrameUniforms
	{
		mat4x4 viewMatrix;
		mat4x4 projectionMatrix;
		vec3 cameraWorldPosition;
		float time;
	};
	static_assert(sizeof(FrameUniforms) % 16 == 0, "FrameUniforms must match shader layout");

	/**
	 * Light struct - matches shader's Light
	 */
	struct LightStruct
	{
		glm::mat4 transform = glm::mat4(1.0f); // transformation matrix for the light (includes position and rotation)
		glm::vec3 color = {1.0f, 1.0f, 1.0f};
		float intensity = 1.0f; // light intensity

		uint32_t light_type = 0;	// 0 = ambient, 1 = directional, 2 = point, 3 = spot
		float spot_angle = 0.5f;	// for spot (in radians)
		float spot_softness = 0.2f; // for spotlight softness
		float _pad = 0.0f;			// padding to ensure 16-byte alignment
	};
	static_assert(sizeof(LightStruct) % 16 == 0, "LightStruct must match WGSL layout");

	/**
	 * Lights buffer - matches shader's LightsBuffer
	 */
	struct LightsBuffer
	{
		uint32_t count = 0;
		float pad3[3]; // padding to ensure 16-byte alignment
	};
	static_assert(sizeof(LightsBuffer) % 16 == 0, "LightsBuffer must match WGSL layout");

	/**
	 * Object uniforms (model matrix) - matches shader's ObjectUniforms
	 */
	struct ObjectUniforms
	{
		mat4x4 modelMatrix;
		mat4x4 normalMatrix; // For normal transformations
	};
	static_assert(sizeof(ObjectUniforms) % 16 == 0, "ObjectUniforms must match shader layout");

	struct DragState
	{
		// Whether a drag action is ongoing (i.e., we are between mouse press and mouse release)
		bool active = false;
		// The position of the mouse at the beginning of the drag action
		vec2 startMouse;

		// Constant settings
		float sensitivity = 1.0f;
		float scrollSensitivity = 2.5f;

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
	wgpu::Instance m_instance = nullptr;
	wgpu::Surface m_surface = nullptr;
#ifndef WEBGPU_BACKEND_WGPU
	// To this date, Dawn still needs a SwapChain
	wgpu::SwapChain m_swapChain = nullptr;
#endif // WEBGPU_BACKEND_DAWN
	wgpu::Device m_device = nullptr;
	wgpu::Queue m_queue = nullptr;
	wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
	// Keep the error callback alive
	std::unique_ptr<wgpu::ErrorCallback> m_errorCallbackHandle;

	// Depth Buffer
	wgpu::TextureFormat m_depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
	wgpu::Texture m_depthTexture = nullptr;
	wgpu::TextureView m_depthTextureView = nullptr;

	// Render Pipeline
	wgpu::ShaderModule m_shaderModule = nullptr;
	wgpu::RenderPipeline m_pipeline = nullptr;

	// Geometry
	wgpu::Buffer m_vertexBuffer = nullptr;
	wgpu::Buffer m_indexBuffer = nullptr;
	int32_t m_vertexCount = 0;
	int32_t m_indexCount = 0;

	// Uniforms
	wgpu::Buffer m_frameUniformBuffer = nullptr;
	wgpu::Buffer m_objectUniformBuffer = nullptr;
	FrameUniforms m_frameUniforms;
	ObjectUniforms m_objectUniforms;

	// Lights
	std::vector<LightStruct> m_lights;
	wgpu::Buffer m_lightsBuffer = nullptr;
	LightsBuffer m_lightsBufferHeader;
	bool m_lightsChanged = true;
	std::map<size_t, glm::vec3> m_lightDirectionsUI; // Store UI direction angles in degrees

	// Material
	std::shared_ptr<engine::rendering::webgpu::WebGPUMaterial> m_material;

	// Bind Group Layout
	static constexpr size_t kBindGroupLayoutCount = 4;
	// Enum for bind group layout indices
	std::array<wgpu::BindGroupLayout, kBindGroupLayoutCount> m_bindGroupLayouts = {nullptr, nullptr, nullptr, nullptr};

	// Bind Group
	wgpu::BindGroup m_frameBindGroup = nullptr;
	wgpu::BindGroup m_uniformBindGroup = nullptr;
	wgpu::BindGroup m_lightBindGroup = nullptr;

	// Scene Graph and Camera
	std::shared_ptr<engine::scene::entity::Node> m_rootNode;
	std::shared_ptr<engine::scene::CameraNode> m_cameraNode;
	std::shared_ptr<engine::scene::Scene> m_scene;
	wgpu::Buffer m_cameraBuffer = nullptr; // Buffer for camera data

	DragState m_drag;

	std::vector<std::shared_ptr<engine::rendering::webgpu::WebGPUModel>> m_webgpuModels;

	bool m_pendingShaderReload = false;

	// Debug
	bool m_showDebugAxes = false;
	// Debug rendering resources
	wgpu::ShaderModule m_debugShaderModule = nullptr;
	wgpu::RenderPipeline m_debugPipeline = nullptr;

	// Scene graph management
	void updateSceneGraph(float deltaTime);

	// Helper methods for light management
	void addLight();
	void removeLight(size_t index);
};
} // namespace engine