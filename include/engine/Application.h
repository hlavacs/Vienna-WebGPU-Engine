#pragma once

#include <array>
#include <memory>
#include <vector>

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>

#include "engine/core/PathProvider.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/resources/ResourceManager.h"

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
		void onMouseMove(double xpos, double ypos);
		void onMouseButton(int button, bool pressed, int x, int y);
		void onScroll(double xoffset, double yoffset);

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

		bool initUniformBindGroup();
		bool initLightBindGroup();

		void updateProjectionMatrix();
		void updateViewMatrix();
		void updateDragInertia();

		bool initGui();										// called in onInit
		void terminateGui();								// called in onFinish
		void updateGui(wgpu::RenderPassEncoder renderPass); // called in onFrame
		void processSDLEvents();							// called in onFrame

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
		 * The same structure as in the shader, replicated in C++
		 */
		struct MyUniforms
		{
			// We add transform matrices
			mat4x4 projectionMatrix;
			mat4x4 viewMatrix;
			mat4x4 modelMatrix;
			vec4 color;
			vec3 cameraWorldPosition;
			float time;
		};
		// Have the compiler check byte alignment
		static_assert(sizeof(MyUniforms) % 16 == 0);

		// Match WGSL Light struct
		struct LightStruct
		{
			glm::mat4 transform = glm::mat4(1.0f); // transformation matrix for the light (includes position and rotation)
			glm::vec3 color = {1.0f, 1.0f, 1.0f};
			float intensity = 1.0f; // light intensity

			uint32_t light_type = 0;	// 0 = directional, 1 = point, 2 = spot
			float spot_angle = 0.5f;	// for spot (in radians)
			float spot_softness = 0.2f; // padding to ensure 16-byte alignment
			float pad2 = 0.0f;			// padding to ensure 16-byte alignment
		};
		static_assert(sizeof(LightStruct) % 16 == 0, "LightStruct must match WGSL layout");

		struct LightsBuffer
		{
			uint32_t count = 0;
			float kd = 1.0f;		// material diffuse coefficient
			float ks = 0.5f;		// material specular coefficient
			float hardness = 32.0f; // material shininess/hardness
									// lights array is managed separately
		};
		static_assert(sizeof(LightsBuffer) % 16 == 0, "LightsBuffer must match WGSL layout");

		struct CameraState
		{
			// angles.x is the rotation of the camera around the global vertical axis, affected by mouse.x
			// angles.y is the rotation of the camera around its local horizontal axis, affected by mouse.y
			vec2 angles = {0.8f, 0.5f};
			// zoom is the position of the camera along its local forward axis, affected by the scroll wheel
			float zoom = -1.2f;
		};

		struct DragState
		{
			// Whether a drag action is ongoing (i.e., we are between mouse press and mouse release)
			bool active = false;
			// The position of the mouse at the beginning of the drag action
			vec2 startMouse;
			// The camera state at the beginning of the drag action
			CameraState startCameraState;

			// Constant settings
			float sensitivity = 0.01f;
			float scrollSensitivity = 0.1f;

			// Inertia
			vec2 velocity = {0.0, 0.0};
			vec2 previousDelta;
			float intertia = 0.9f;
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

		// Texture
		wgpu::Sampler m_sampler = nullptr;
		wgpu::Texture m_baseColorTexture = nullptr;
		wgpu::TextureView m_baseColorTextureView = nullptr;
		wgpu::Texture m_normalTexture = nullptr;
		wgpu::TextureView m_normalTextureView = nullptr;

		// Geometry
		wgpu::Buffer m_vertexBuffer = nullptr;
		wgpu::Buffer m_indexBuffer = nullptr;
		int32_t m_vertexCount = 0;
		int32_t m_indexCount = 0;

		// Uniforms
		wgpu::Buffer m_uniformBuffer = nullptr;
		MyUniforms m_uniforms;

		// Lights
		std::vector<LightStruct> m_lights;
		wgpu::Buffer m_lightsBuffer = nullptr;
		LightsBuffer m_lightsBufferHeader;
		bool m_lightsChanged = true;
		std::map<size_t, glm::vec3> m_lightDirectionsUI; // Store UI direction angles in degrees

		// Bind Group Layout
		static constexpr size_t kBindGroupLayoutCount = 3;
		// Enum for bind group layout indices
		enum BindGroupLayoutIndex
		{
			Uniform = 0,
			Material = 1,
			Light = 2
		};
		std::array<wgpu::BindGroupLayout, kBindGroupLayoutCount> m_bindGroupLayouts = {nullptr, nullptr, nullptr};

		// Bind Group
		wgpu::BindGroup m_bindGroup = nullptr;
		wgpu::BindGroup m_materialBindGroup = nullptr;
		wgpu::BindGroup m_uniformBindGroup = nullptr;
		wgpu::BindGroup m_lightBindGroup = nullptr;

		CameraState m_cameraState;
		DragState m_drag;

		std::vector<std::shared_ptr<engine::rendering::webgpu::WebGPUModel>> m_webgpuModels;

		bool m_pendingShaderReload = false;

		// Debug
		bool m_showDebugAxes = false;
		// Debug rendering resources
		wgpu::ShaderModule m_debugShaderModule = nullptr;
		wgpu::RenderPipeline m_debugPipeline = nullptr;

		// Helper methods for light management
		void addLight();
		void removeLight(size_t index);
	};
}