// This file is based on the "Learn WebGPU for C++" tutorial by Elie Michel (https://github.com/eliemichel/LearnWebGPU).
// Significant modifications, refactoring, and extensions have been made for this project.
// Original code © 2022-2024 Elie Michel, MIT License.

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/polar_coordinates.hpp>

#include <SDL.h>
#include <sdl2webgpu.h>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>
#include <imgui.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "engine/Application.h"
#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/entity/Node.h"

using namespace wgpu;
using engine::rendering::Vertex;

constexpr float PI = 3.14159265358979323846f;

// Custom ImGui widgets
namespace ImGui
{
bool DragDirection(const char *label, glm::vec4 &direction)
{
	glm::vec2 angles = glm::degrees(glm::polar(glm::vec3(direction)));
	bool changed = ImGui::DragFloat2(label, glm::value_ptr(angles));
	direction = glm::vec4(glm::euclidean(glm::radians(angles)), direction.w);
	return changed;
}
} // namespace ImGui

namespace engine
{
using engine::rendering::BindGroupLayoutIndex;

Application::Application()
{
#ifdef DEBUG_ROOT_DIR
	engine::core::PathProvider::initialize(DEBUG_ROOT_DIR, ASSETS_ROOT_DIR);
#else
	PathProvider::initialize();
#endif
	spdlog::info("EXE Root: {}", engine::core::PathProvider::getExecutableRoot().string());
	spdlog::info("LIB Root: {}", engine::core::PathProvider::getLibraryRoot().string());
	m_resourceManager = std::make_shared<engine::resources::ResourceManager>(engine::core::PathProvider::getResourceRoot());
	m_context = std::make_shared<engine::rendering::webgpu::WebGPUContext>();

	// Initialize the UI angles map
	m_lightDirectionsUI.clear();
}

///////////////////////////////////////////////////////////////////////////////
// Public methods

bool Application::onInit()
{
	// Create SDL window first
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0)
	{
		std::cerr << "Could not initialize SDL2: " << SDL_GetError() << std::endl;
		return false;
	}
	Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
	int width = 640, height = 480;
	m_window = SDL_CreateWindow("Learn WebGPU", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, windowFlags);
	if (!m_window)
	{
		std::cerr << "Could not open window!" << std::endl;
		return false;
	}
	// Now initialize context with window
	m_context->initialize(m_window);
	if (!initSurface())
		return false;
	if (!initDepthBuffer())
		return false;
	if (!initBindGroupLayout())
		return false;
	if (!initRenderPipeline())
		return false;
	if (!initGeometry())
		return false;
	if (!initUniforms())
		return false;
	if (!initLightingUniforms())
		return false;
	if (!initBindGroups())
		return false;
	if (!initGui())
		return false;
	return true;
}

void Application::onFrame()
{
	if (m_pendingShaderReload)
	{
		reloadShader();
		m_pendingShaderReload = false;
	}
	// Calculate delta time for node updates
	static Uint64 lastFrameTime = SDL_GetTicks64();
	Uint64 currentTime = SDL_GetTicks64();
	float deltaTime = (currentTime - lastFrameTime) / 1000.0f;
	lastFrameTime = currentTime;
	processSDLEvents(deltaTime);
	updateDragInertia(deltaTime);
	updateLightingUniforms();


	// Process frame lifecycle using the Scene system
	if (m_scene)
	{
		// Handle Update and LateUpdate phases
		updateSceneGraph(deltaTime);

		// Handle preRender phase - this is where camera matrices are prepared
		m_scene->preRender();

		// Update frame uniforms and camera data from the active camera
		if (m_cameraNode)
		{
			// Update frame uniforms for backwards compatibility
			m_frameUniforms.viewMatrix = m_cameraNode->getViewMatrix();
			m_frameUniforms.projectionMatrix = m_cameraNode->getProjectionMatrix();
			m_frameUniforms.cameraWorldPosition = m_cameraNode->getPosition();
		}
	}

	// Update time uniform
	m_frameUniforms.time = static_cast<float>(static_cast<double>(SDL_GetTicks64() / 1000.0));
	m_context->getQueue().writeBuffer(m_frameUniformBuffer, 0, &m_frameUniforms, sizeof(FrameUniforms));

#ifdef WEBGPU_BACKEND_WGPU
	SurfaceTexture surfaceTexture;
	m_surface.getCurrentTexture(&surfaceTexture);
	TextureView nextTexture = Texture(surfaceTexture.texture).createView();
#else  // WEBGPU_BACKEND_WGPU
	TextureView nextTexture = m_swapChain.getCurrentTextureView();
#endif // WEBGPU_BACKEND_WGPU
	if (!nextTexture)
	{
		std::cerr << "Cannot acquire next swap chain texture" << std::endl;
	}

	CommandEncoderDescriptor commandEncoderDesc;
	commandEncoderDesc.label = "Command Encoder";
	CommandEncoder encoder = m_context->getDevice().createCommandEncoder(commandEncoderDesc);

	RenderPassDescriptor renderPassDesc{};

	RenderPassColorAttachment renderPassColorAttachment{};
	renderPassColorAttachment.view = nextTexture;
	renderPassColorAttachment.resolveTarget = nullptr;
#ifdef __EMSCRIPTEN__
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
	renderPassColorAttachment.loadOp = LoadOp::Clear;
	renderPassColorAttachment.storeOp = StoreOp::Store;
	renderPassColorAttachment.clearValue = Color{0.05, 0.05, 0.05, 1.0};
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;

	RenderPassDepthStencilAttachment depthStencilAttachment;
	depthStencilAttachment.view = m_depthTextureView;
	depthStencilAttachment.depthClearValue = 1.0f;
	depthStencilAttachment.depthLoadOp = LoadOp::Clear;
	depthStencilAttachment.depthStoreOp = StoreOp::Store;
	depthStencilAttachment.depthReadOnly = false;
	depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
	depthStencilAttachment.stencilLoadOp = LoadOp::Clear;
	depthStencilAttachment.stencilStoreOp = StoreOp::Store;
#else
	depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
	depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
#endif
	depthStencilAttachment.stencilReadOnly = true;

	renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

	renderPassDesc.timestampWrites = nullptr;
	RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	// Ensure pipeline is valid before setting it
	if (m_pipeline)
	{
		renderPass.setPipeline(m_pipeline);
	}
	else
	{
		spdlog::error("Invalid render pipeline in onFrame!");
		renderPass.end();
		renderPass.release();
		encoder.release();
		nextTexture.release();
		return;
	}

	// Update materials and models before rendering
	m_material->update();
	for (const auto &model : m_webgpuModels)
	{
		model->update();
	}

	// Set binding groups that apply to all models
	renderPass.setBindGroup(0, m_frameBindGroup, 0, nullptr);	// Frame uniforms (group 0)
	renderPass.setBindGroup(1, m_lightBindGroup, 0, nullptr);	// Light data (group 1)
	renderPass.setBindGroup(2, m_uniformBindGroup, 0, nullptr); // Object uniforms (group 2)

	// Execute the render phase of our scene
	if (m_scene)
	{
		m_scene->render();
	}

	// Render WebGPU models
	for (const auto &model : m_webgpuModels)
	{
		model->render(encoder, renderPass);
	}

	// If no models were rendered, log a warning
	if (m_webgpuModels.empty() && !m_scene->getActiveCamera())
	{
		spdlog::warn("No rendering occurred - no models or active camera!");
	}

	// Render debug axes if enabled (before UI so they appear in world space)
	if (m_showDebugAxes)
	{
		// Load the debug shader module if not already loaded
		if (!m_debugShaderModule)
		{
			m_debugShaderModule = engine::resources::ResourceManager::loadShaderModule(
				engine::core::PathProvider::getResource("debug.wgsl"),
				m_context->getDevice()
			);

			if (!m_debugShaderModule)
			{
				spdlog::error("Failed to load debug shader!");
			}
			else
			{
				// Create bind group layout for debug shader using the factory
				std::vector<wgpu::BindGroupLayoutEntry> entries;

				// View-Projection matrix (binding 0)
				entries.push_back(m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<glm::mat4x4>(0, static_cast<uint32_t>(ShaderStage::Vertex)));

				// Transform matrices storage buffer (binding 1)
				entries.push_back(m_context->bindGroupFactory().createStorageBindGroupLayoutEntry(1, wgpu::ShaderStage::Vertex, true));

				// Create bind group layout
				wgpu::BindGroupLayoutDescriptor layoutDesc{};
				layoutDesc.entryCount = static_cast<uint32_t>(entries.size());
				layoutDesc.entries = entries.data();

				wgpu::BindGroupLayout debugBindGroupLayout =
					m_context->getDevice().createBindGroupLayout(layoutDesc);

				// Create pipeline layout with debug bind group layout
				wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
				pipelineLayoutDesc.bindGroupLayoutCount = 1;
				pipelineLayoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)&debugBindGroupLayout;
				wgpu::PipelineLayout debugPipelineLayout =
					m_context->getDevice().createPipelineLayout(pipelineLayoutDesc);

				// Create shader info
				engine::rendering::webgpu::WebGPUShaderInfo vertexShaderInfo(m_debugShaderModule, "vs_main");
				engine::rendering::webgpu::WebGPUShaderInfo fragmentShaderInfo(m_debugShaderModule, "fs_main");

				// Create pipeline descriptor using the factory
				wgpu::RenderPipelineDescriptor pipelineDesc = m_context->pipelineFactory().createRenderPipelineDescriptor(
					&vertexShaderInfo,
					&fragmentShaderInfo,
					m_context->getSwapChainFormat(),
					m_depthTextureFormat,
					true
				);

				// Customize for debug rendering (line list, no vertex input)
				pipelineDesc.vertex.bufferCount = 0; // No vertex buffers needed for debug axes
				pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::LineList;
				pipelineDesc.layout = debugPipelineLayout;

				// Create the pipeline using the factory
				m_debugPipeline = m_context->pipelineFactory().createRenderPipeline(pipelineDesc);

				// Clean up
				debugBindGroupLayout.release();
				debugPipelineLayout.release();
			}
		}

		// Draw debug axes if pipeline exists
		if (m_debugPipeline)
		{
			// Set debug pipeline
			renderPass.setPipeline(m_debugPipeline);

			// Calculate total number of instances
			size_t numInstances = m_lights.size(); // Debug axes for each light

			// Create transforms buffer for the instances
			std::vector<glm::mat4> transforms;
			transforms.reserve(numInstances);

			// Add transforms for each light
			for (const auto &light : m_lights)
			{
				glm::mat4 transform = glm::mat4(1.0f);

				// For point/spot lights, use position
				if (light.light_type == 2 || light.light_type == 3)
				{
					transform[3] = glm::vec4(glm::vec3(light.transform[3]), 1.0f); // Extract position from transform
				}

				// Get the rotation from the stored UI angles to ensure consistency
				size_t lightIndex = &light - &m_lights[0]; // Calculate light index
				if (m_lightDirectionsUI.find(lightIndex) != m_lightDirectionsUI.end())
				{
					glm::vec3 angles = m_lightDirectionsUI[lightIndex];

					// Build rotation matrix from UI angles
					glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(angles.x), glm::vec3(1.0f, 0.0f, 0.0f));
					glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::radians(angles.y), glm::vec3(0.0f, 1.0f, 0.0f));
					glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians(angles.z), glm::vec3(0.0f, 0.0f, 1.0f));
					glm::mat4 rotation = rotZ * rotY * rotX;

					// Copy rotation part to transform
					glm::mat3 rotPart = glm::mat3(rotation);
					transform[0] = glm::vec4(rotPart[0], 0.0f);
					transform[1] = glm::vec4(rotPart[1], 0.0f);
					transform[2] = glm::vec4(rotPart[2], 0.0f);
					// Position is already set in transform[3]
				}
				else
				{
					// Fallback to using the light's transform directly if no UI angles are stored
					glm::mat3 rotPart = glm::mat3(light.transform);
					transform[0] = glm::vec4(rotPart[0], 0.0f);
					transform[1] = glm::vec4(rotPart[1], 0.0f);
					transform[2] = glm::vec4(rotPart[2], 0.0f);
				}

				// Scale axes based on light intensity
				float scale = 0.2f + light.intensity * 0.1f;
				transform = glm::scale(transform, glm::vec3(scale));

				transforms.push_back(transform);
			}

			// Create combined view-projection matrix
			glm::mat4 viewProj = m_frameUniforms.projectionMatrix * m_frameUniforms.viewMatrix;

			// Create buffers
			wgpu::BufferDescriptor viewProjBufferDesc{};
			viewProjBufferDesc.size = sizeof(glm::mat4);
			viewProjBufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
			wgpu::Buffer viewProjBuffer = m_context->getDevice().createBuffer(viewProjBufferDesc);
			m_context->getQueue().writeBuffer(viewProjBuffer, 0, &viewProj, sizeof(glm::mat4));

			wgpu::BufferDescriptor transformsBufferDesc{};
			transformsBufferDesc.size = transforms.size() * sizeof(glm::mat4);
			transformsBufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
			wgpu::Buffer transformsBuffer = m_context->getDevice().createBuffer(transformsBufferDesc);
			m_context->getQueue().writeBuffer(transformsBuffer, 0, transforms.data(), transforms.size() * sizeof(glm::mat4));

			// Create bind group entries using the consistent pattern
			std::vector<wgpu::BindGroupEntry> entries;

			// View-projection matrix entry
			wgpu::BindGroupEntry viewProjEntry{};
			viewProjEntry.binding = 0;
			viewProjEntry.buffer = viewProjBuffer;
			viewProjEntry.offset = 0;
			viewProjEntry.size = sizeof(glm::mat4);
			entries.push_back(viewProjEntry);

			// Transform matrices entry
			wgpu::BindGroupEntry transformsEntry{};
			transformsEntry.binding = 1;
			transformsEntry.buffer = transformsBuffer;
			transformsEntry.offset = 0;
			transformsEntry.size = transforms.size() * sizeof(glm::mat4);
			entries.push_back(transformsEntry);

			// Create the bind group
			wgpu::BindGroupDescriptor bindGroupDesc{};
			bindGroupDesc.layout = m_debugPipeline.getBindGroupLayout(0);
			bindGroupDesc.entryCount = static_cast<uint32_t>(entries.size());
			bindGroupDesc.entries = entries.data();

			wgpu::BindGroup debugBindGroup = m_context->getDevice().createBindGroup(bindGroupDesc);

			// Set bind group and draw
			renderPass.setBindGroup(0, debugBindGroup, 0, nullptr);
			renderPass.draw(6, static_cast<uint32_t>(numInstances), 0, 0);

			// Note: We can't release these resources here because they're still being used by the GPU
			// They will be automatically released when they go out of scope after renderPass.end() and queue.submit()
		}
	}

	// We add the GUI drawing commands to the render pass
	updateGui(renderPass);

	// Defensive check before ending render pass
	assert(renderPass && "RenderPassEncoder is invalid before end()");

	// Only assert these if we have models to render
	if (!m_webgpuModels.empty())
	{
		assert(m_pipeline && "Pipeline is invalid before end()");
	}

	// End the render pass
	renderPass.end();
	renderPass.release();

	// Run postRender phase
	if (m_scene)
	{
		m_scene->postRender();
	}

	nextTexture.release();

	CommandBufferDescriptor cmdBufferDescriptor{};
	cmdBufferDescriptor.label = "Command buffer";
	CommandBuffer command = encoder.finish(cmdBufferDescriptor);
	encoder.release();
	m_context->getQueue().submit(command);
	command.release();

#ifdef WEBGPU_BACKEND_WGPU
	m_surface.present();
#else
#ifndef __EMSCRIPTEN__
	m_swapChain.present();
#endif
#endif

#ifdef WEBGPU_BACKEND_DAWN
	// Check for pending error callbacks
	m_device.tick();
#endif
}

void Application::processSDLEvents(float deltaTime)
{

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		ImGui_ImplSDL2_ProcessEvent(&event);

		ImGuiIO &io = ImGui::GetIO();
		if (io.WantCaptureMouse || io.WantCaptureKeyboard)
		{
			continue;
		}
		switch (event.type)
		{
		case SDL_QUIT:
			m_shouldClose = true;
			break;

		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOW_FULLSCREEN)
			{
				onResize(); // No need to fetch width/height unless you want to
			}
			break;

		case SDL_MOUSEMOTION:
			onMouseMove(event.motion.x, event.motion.y, deltaTime);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			onMouseButton(
				event.button.button,
				event.button.state == SDL_PRESSED ? 1 : 0,
				event.button.x,
				event.button.y
			);
			break;

		case SDL_MOUSEWHEEL:
			onScroll(event.wheel.x, event.wheel.y);
			break;

		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_F5)
			{
				// F5 key pressed - reload the shader
				reloadShader();
			}
			else if (event.key.keysym.sym == SDLK_UP)
			{
				m_drag.elevation += 0.05f;
			}
			else if (event.key.keysym.sym == SDLK_DOWN)
			{
				m_drag.elevation -= 0.05f;
			}
			else if (event.key.keysym.sym == SDLK_LEFT)
			{
				m_drag.azimuth += 0.05f;
			}
			else if (event.key.keysym.sym == SDLK_RIGHT)
			{
				m_drag.azimuth -= 0.05f;
			}
			updateOrbitCamera();
			// Fall through to handle other key events
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
			// handle key event
			break;
		}
	}
}

void Application::onFinish()
{
	terminateGui();
	terminateLightingUniforms();
	terminateUniforms();
	terminateGeometry();

	// Release bind groups
	if (m_frameBindGroup)
		m_frameBindGroup.release();
	if (m_uniformBindGroup)
		m_uniformBindGroup.release();
	if (m_lightBindGroup)
		m_lightBindGroup.release();

	m_context->bindGroupFactory().cleanup();
	terminateRenderPipeline();
	terminateDepthBuffer();
	terminateSurface();
}

bool Application::isRunning()
{
	return !m_shouldClose;
}

void Application::onResize()
{
	// Terminate in reverse order
	terminateDepthBuffer();
	terminateSurface();

	// Re-init
	initSurface();
	initDepthBuffer();

	updateProjectionMatrix();

	// Defensive asserts after resize
	assert(m_context);
	assert(m_window);
}

void Application::onMouseMove(double xpos, double ypos, float deltaTime)
{
	if (m_drag.active)
	{
		// Calculate delta from starting position
		glm::vec2 currentMouse = glm::vec2((float)xpos, (float)ypos);
		glm::vec2 delta = (currentMouse - m_drag.startMouse) * m_drag.sensitivity * deltaTime;
		// Use sensitivity parameter for orbit speed
		m_drag.azimuth -= delta.x;
		m_drag.elevation += delta.y;
		m_drag.elevation = glm::clamp(m_drag.elevation, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);

		// Update camera position based on new orbital parameters
		updateOrbitCamera();
		m_drag.startMouse = currentMouse;

		// Inertia calculation for when the user releases the mouse
		m_drag.velocity = delta - m_drag.previousDelta;
		m_drag.previousDelta = delta;
	}
}
void Application::onMouseButton(int button, bool pressed, int x, int y)
{
	if (pressed && button == SDL_BUTTON_LEFT)
	{
		m_drag.active = true;
		int xpos, ypos;
		SDL_GetMouseState(&xpos, &ypos);
		// Store without inversion to match onMouseMove
		m_drag.startMouse = glm::vec2((float)xpos, (float)ypos);
		m_drag.previousDelta = glm::vec2(0.0f);
	}
	else if (!pressed && button == SDL_BUTTON_LEFT)
	{
		m_drag.active = false;
	}
}

void Application::onScroll(double /* xoffset */, double yoffset, float  deltaTime)
{
	// Change the orbit distance - negative yoffset means zoom in
	m_drag.distance -= static_cast<float>(yoffset) * m_drag.scrollSensitivity * deltaTime;

	// Update camera position based on new distance
	updateOrbitCamera();
}

///////////////////////////////////////////////////////////////////////////////
// Private methods

#ifdef __EMSCRIPTEN__
EM_JS(void, setCanvasNativeSize, (int width, int height), {
	Module.setCanvasSize(width, height, true);
});
#endif

bool Application::initSurface()
{
	m_context->terminateSurface();
	m_surface = m_context->getSurface();
	// Get the current size of the window's framebuffer:
	int width, height;
	SDL_GL_GetDrawableSize(m_window, &width, &height);

#ifdef WEBGPU_BACKEND_WGPU
	SurfaceConfiguration config;
	config.width = static_cast<uint32_t>(width);
	config.height = static_cast<uint32_t>(height);
	config.usage = TextureUsage::RenderAttachment;
	config.format = m_context->getSwapChainFormat();
	config.presentMode = PresentMode::Fifo;
	config.alphaMode = CompositeAlphaMode::Auto;
	config.device = m_context->getDevice();
	m_surface.configure(config);
#else  // WEBGPU_BACKEND_WGPU
	SwapChainDescriptor desc;
	desc.width = static_cast<uint32_t>(width);
	desc.height = static_cast<uint32_t>(height);
	desc.usage = TextureUsage::RenderAttachment;
	desc.format = m_context->getSwapChainFormat();
	desc.presentMode = PresentMode::Fifo;
	m_swapChain = wgpuDeviceCreateSwapChain(m_context->getDevice(), m_surface, &desc);
#endif // WEBGPU_BACKEND_WGPU

	// Check that surface and swapchain/surface config are valid
	assert(m_surface);
	assert(m_context->getSwapChainFormat() != wgpu::TextureFormat::Undefined && "SwapChain format must be defined");

	return true;
}

void Application::terminateSurface()
{

#ifndef WEBGPU_BACKEND_WGPU
	m_swapChain.release();
	m_surface.release();
#else
	m_context->terminateSurface();
#endif
}

bool Application::initDepthBuffer()
{
	// Get the current size of the window's framebuffer:
	int width, height;
	SDL_GL_GetDrawableSize(m_window, &width, &height);

	// Create the depth texture
	TextureDescriptor depthTextureDesc;
	depthTextureDesc.dimension = TextureDimension::_2D;
	depthTextureDesc.format = m_depthTextureFormat;
	depthTextureDesc.mipLevelCount = 1;
	depthTextureDesc.sampleCount = 1;
	depthTextureDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
	depthTextureDesc.usage = TextureUsage::RenderAttachment;
	depthTextureDesc.viewFormatCount = 1;
	depthTextureDesc.viewFormats = (WGPUTextureFormat *)&m_depthTextureFormat;
	m_depthTexture = m_context->getDevice().createTexture(depthTextureDesc);
	std::cout << "Depth texture: " << m_depthTexture << std::endl;

	// Create the view of the depth texture manipulated by the rasterizer
	TextureViewDescriptor depthTextureViewDesc;
	depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
	depthTextureViewDesc.baseArrayLayer = 0;
	depthTextureViewDesc.arrayLayerCount = 1;
	depthTextureViewDesc.baseMipLevel = 0;
	depthTextureViewDesc.mipLevelCount = 1;
	depthTextureViewDesc.dimension = TextureViewDimension::_2D;
	depthTextureViewDesc.format = m_depthTextureFormat;
	m_depthTextureView = m_depthTexture.createView(depthTextureViewDesc);
	std::cout << "Depth texture view: " << m_depthTextureView << std::endl;

	// Check that created texture and view are valid
	assert(m_depthTexture);
	assert(m_depthTextureView);

	return m_depthTextureView != nullptr;
}

void Application::terminateDepthBuffer()
{
	m_depthTextureView.release();
	m_depthTexture.destroy();
	m_depthTexture.release();
}

bool Application::initRenderPipeline()
{
	// Defensive asserts for resource validity and format consistency
	assert(m_context);
	assert(m_context->getDevice());
	assert(m_context->getSwapChainFormat() != wgpu::TextureFormat::Undefined && "SwapChain format must be defined");
	assert(m_depthTextureFormat != wgpu::TextureFormat::Undefined && "Depth texture format must be defined");

	m_shaderModule = engine::resources::ResourceManager::loadShaderModule(engine::core::PathProvider::getResource("shader.wgsl"), m_context->getDevice());
	if (!m_shaderModule)
	{
		std::cerr << "Could not load shader module!" << std::endl;
		return false;
	}

	engine::rendering::webgpu::WebGPUShaderInfo vertexShaderInfo(m_shaderModule, "vs_main");
	engine::rendering::webgpu::WebGPUShaderInfo fragmentShaderInfo(m_shaderModule, "fs_main");

	std::cout << "Creating render pipeline using WebGPUPipelineFactory..." << std::endl;

	wgpu::RenderPipelineDescriptor pipelineDesc = m_context->pipelineFactory().createRenderPipelineDescriptor(
		&vertexShaderInfo,
		&fragmentShaderInfo,
		m_context->getSwapChainFormat(),
		m_depthTextureFormat,
		true
	);

	// Create the pipeline layout with all four bind group layouts
	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = kBindGroupLayoutCount;

	// Make sure all bind groups are non-null before creating the layout
	for (size_t i = 0; i < kBindGroupLayoutCount; i++)
	{
		if (!m_bindGroupLayouts[i])
		{
			spdlog::error("Bind group layout at index {} is null!", i);
			return false;
		}
	}

	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)m_bindGroupLayouts.data();
	wgpu::PipelineLayout layout = m_context->getDevice().createPipelineLayout(layoutDesc);
	pipelineDesc.layout = layout;

	// Create the pipeline using the factory
	m_pipeline = m_context->pipelineFactory().createRenderPipeline(pipelineDesc);
	std::cout << "Render pipeline: " << m_pipeline << std::endl;

	// Check that color target format matches swapchain format
	assert(m_context->getSwapChainFormat() == m_context->getSwapChainFormat() && "Pipeline color target format must match swapchain format");
	// Check that depth stencil format matches depth texture format
	assert(m_depthTextureFormat == m_depthTextureFormat && "Pipeline depth stencil format must match depth texture format");

	return m_pipeline != nullptr;
}

void Application::terminateRenderPipeline()
{
	if (m_debugPipeline)
	{
		m_debugPipeline.release();
	}
	if (m_debugShaderModule)
	{
		m_debugShaderModule.release();
	}
	m_pipeline.release();
	m_shaderModule.release();
}

bool Application::initGeometry()
{
	// List of model files to load
	std::vector<std::string> modelPaths = {
		engine::core::PathProvider::getResource("fourareen.obj").string(),
		// Add more model paths here as needed
	};

	m_webgpuModels.clear();
	if (!m_resourceManager || !m_resourceManager->m_modelManager)
	{
		std::cerr << "ResourceManager or ModelManager not available!" << std::endl;
		return false;
	}

	// Create WebGPU models from model paths
	engine::rendering::webgpu::WebGPUModelFactory modelFactory(*m_context);
	for (const auto &modelPath : modelPaths)
	{
		auto modelOpt = m_resourceManager->m_modelManager->createModel(modelPath);
		if (!modelOpt || !*modelOpt)
		{
			std::cerr << "Could not load model: " << modelPath << std::endl;
			continue;
		}

		auto webgpuModel = modelFactory.createFrom(*(modelOpt.value()));
		if (webgpuModel)
		{
			m_webgpuModels.push_back(webgpuModel);
			m_material = webgpuModel->getMaterial();
		}
		else
		{
			std::cerr << "Could not create WebGPUModel for: " << modelPath << std::endl;
		}
	}

	return !m_webgpuModels.empty();
}

void Application::terminateGeometry()
{
	// Clear WebGPUModels collection first
	m_webgpuModels.clear();

	// Release vertex and index buffers
	if (m_vertexBuffer)
	{
		m_vertexBuffer.destroy();
		m_vertexBuffer.release();
		m_vertexCount = 0;
	}

	if (m_indexBuffer)
	{
		m_indexBuffer.destroy();
		m_indexBuffer.release();
		m_indexCount = 0;
	}
}

void Application::resetCamera()
{
	glm::vec3 cameraPosition = glm::vec3(0.0f, 2.0f, -m_drag.distance); // Use negative Z to face towards origin

	m_cameraNode->getTransform()->setLocalPosition(cameraPosition);
	m_cameraNode->lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // Look at origin with Y up

	// Set projection parameters
	if (m_cameraNode && m_cameraNode->getTransform())
	{
		glm::vec3 camPos = m_cameraNode->getTransform()->getLocalPosition();
		glm::vec3 toCam = camPos - m_drag.targetPoint;
		m_drag.distance = glm::length(toCam);
		if (m_drag.distance > 1e-5f)
		{
			glm::vec3 dir = toCam / m_drag.distance;
			m_drag.elevation = std::asin(dir.y);
			m_drag.azimuth = std::atan2(dir.z, dir.x);
		}
	}
}

bool Application::initUniforms()
{
	// Create frame uniform buffer
	BufferDescriptor frameBufferDesc;
	frameBufferDesc.size = sizeof(FrameUniforms);
	frameBufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
	frameBufferDesc.mappedAtCreation = false;
	m_frameUniformBuffer = m_context->getDevice().createBuffer(frameBufferDesc);

	// Create object uniform buffer
	BufferDescriptor objectBufferDesc;
	objectBufferDesc.size = sizeof(ObjectUniforms);
	objectBufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
	objectBufferDesc.mappedAtCreation = false;
	m_objectUniformBuffer = m_context->getDevice().createBuffer(objectBufferDesc);

	// Initialize scene and scene graph
	m_scene = std::make_shared<engine::scene::Scene>();
	m_rootNode = std::make_shared<engine::scene::entity::Node>();
	m_scene->setRoot(m_rootNode);

	// Create camera node
	m_cameraNode = std::make_shared<engine::scene::CameraNode>();
	m_rootNode->addChild(m_cameraNode);		// CameraNode is a SpatialNode, which is a Node
	m_scene->setActiveCamera(m_cameraNode); // Set as active camera

	resetCamera();

	// Get window aspect ratio
	int width, height;
	SDL_GL_GetDrawableSize(m_window, &width, &height);
	float aspectRatio = width / static_cast<float>(height);

	m_cameraNode->setFov(45.0f);
	m_cameraNode->setAspect(aspectRatio);
	m_cameraNode->setNearFar(0.01f, 100.0f);

	// Initialize frame uniforms
	m_frameUniforms.viewMatrix = m_cameraNode->getViewMatrix();
	m_frameUniforms.projectionMatrix = m_cameraNode->getProjectionMatrix();
	m_frameUniforms.cameraWorldPosition = m_cameraNode->getTransform()->getPosition();
	m_frameUniforms.time = 0.0f;
	m_context->getQueue().writeBuffer(m_frameUniformBuffer, 0, &m_frameUniforms, sizeof(FrameUniforms));

	// Initialize object uniforms
	m_objectUniforms.modelMatrix = mat4x4(1.0);
	m_objectUniforms.normalMatrix = glm::transpose(glm::inverse(m_objectUniforms.modelMatrix));
	m_context->getQueue().writeBuffer(m_objectUniformBuffer, 0, &m_objectUniforms, sizeof(ObjectUniforms));

	return m_frameUniformBuffer != nullptr && m_objectUniformBuffer != nullptr;
}

bool Application::initLightingUniforms()
{
	// Initialize the lights buffer
	// Create a storage buffer large enough for several lights
	const size_t maxLights = 16; // Support up to 16 lights
	const size_t lightsBufferSize = sizeof(LightsBuffer) + maxLights * sizeof(LightStruct);

	BufferDescriptor lightsBufDesc;
	lightsBufDesc.size = lightsBufferSize;
	lightsBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
	lightsBufDesc.mappedAtCreation = false;
	m_lightsBuffer = m_context->getDevice().createBuffer(lightsBufDesc);

	// Add a default ambient and directional light
	addLight();
	addLight();
	m_lights[0].intensity = 0.2f;
	m_lights[1].light_type = 1;

	return m_lightsBuffer != nullptr;
}

void Application::terminateLightingUniforms()
{
	return;
}

void Application::terminateUniforms()
{
	return;
}

void Application::updateLightingUniforms()
{
	// Update lights buffer when changed
	if (m_lightsChanged && !m_lights.empty())
	{
		// Update the header with the current count
		m_lightsBufferHeader.count = static_cast<uint32_t>(m_lights.size());

		// Write the header
		m_context->getQueue().writeBuffer(
			m_lightsBuffer,
			0,
			&m_lightsBufferHeader,
			sizeof(LightsBuffer)
		);

		// Write the lights array right after the header
		if (!m_lights.empty())
		{
			m_context->getQueue().writeBuffer(
				m_lightsBuffer,
				sizeof(LightsBuffer),
				m_lights.data(),
				m_lights.size() * sizeof(LightStruct)
			);
		}

		m_lightsChanged = false;
	}
}

bool Application::initBindGroupLayout()
{
	// Uniform Bind Group Layout
	m_bindGroupLayouts[BindGroupLayoutIndex::FrameIndex] = m_context->bindGroupFactory().createCustomBindGroupLayout(
		m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<FrameUniforms>()
	);
	// Lighting Bind Group Layout
	m_bindGroupLayouts[BindGroupLayoutIndex::LightIndex] = m_context->bindGroupFactory().createDefaultLightingBindGroupLayout();
	// Uniform Bind Group Layout
	m_bindGroupLayouts[BindGroupLayoutIndex::UniformIndex] = m_context->bindGroupFactory().createCustomBindGroupLayout(
		m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<ObjectUniforms>()
	);
	// Material Bind Group Layout
	m_bindGroupLayouts[BindGroupLayoutIndex::MaterialIndex] = m_context->bindGroupFactory().createDefaultMaterialBindGroupLayout();
	// Light Bind Group Layout

	return m_bindGroupLayouts[BindGroupLayoutIndex::FrameIndex] && m_bindGroupLayouts[BindGroupLayoutIndex::LightIndex] && m_bindGroupLayouts[BindGroupLayoutIndex::UniformIndex] && m_bindGroupLayouts[BindGroupLayoutIndex::MaterialIndex];
}

bool Application::initBindGroups()
{
	// Create frame bind group
	wgpu::BindGroupEntry frameEntry = {};
	frameEntry.binding = 0;
	frameEntry.buffer = m_frameUniformBuffer;
	frameEntry.offset = 0;
	frameEntry.size = sizeof(FrameUniforms);

	m_frameBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_bindGroupLayouts[BindGroupLayoutIndex::FrameIndex],
		std::vector<wgpu::BindGroupEntry>{frameEntry}
	);

	// Create object bind group
	wgpu::BindGroupEntry objectEntry = {};
	objectEntry.binding = 0;
	objectEntry.buffer = m_objectUniformBuffer;
	objectEntry.offset = 0;
	objectEntry.size = sizeof(ObjectUniforms);

	m_uniformBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_bindGroupLayouts[BindGroupLayoutIndex::UniformIndex],
		{objectEntry}
	);

	// The storage buffer entry for multiple lights
	wgpu::BindGroupEntry lightsStorageEntry = {};
	lightsStorageEntry.binding = 0;
	lightsStorageEntry.buffer = m_lightsBuffer;
	lightsStorageEntry.offset = 0;
	// Space for up to 16 lights
	lightsStorageEntry.size = sizeof(LightsBuffer) + 16 * sizeof(LightStruct);

	m_lightBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_bindGroupLayouts[BindGroupLayoutIndex::LightIndex],
		{lightsStorageEntry}
	);

	return m_frameBindGroup != nullptr && m_lightBindGroup != nullptr && m_uniformBindGroup != nullptr;
}

void Application::updateProjectionMatrix()
{
	// Get current window dimensions
	int width, height;
	SDL_GL_GetDrawableSize(m_window, &width, &height);
	float ratio = width / static_cast<float>(height);

	// Only update the CameraNode - no direct GPU updates
	if (m_cameraNode)
	{
		m_cameraNode->setAspect(ratio);
	}

	// The frame uniforms will be updated during the next frame's preRender phase
	// No need to write to GPU buffer here
}

void Application::updateViewMatrix()
{
	// Method now empty - camera matrices are updated in onFrame's preRender phase
	// Any changes to the camera are applied during the Scene's update/lateUpdate phase
	// The frame uniforms will be updated during the next frame's preRender phase
}

void Application::updateDragInertia(float deltaTime)
{
	constexpr float eps = 1e-4f;
	// Apply inertia only when the user released the click.
	if (!m_drag.active)
	{
		// Avoid updating when velocity is negligible
		if (std::abs(m_drag.velocity.x) < eps && std::abs(m_drag.velocity.y) < eps)
		{
			return;
		}

		// Apply velocity to orbit camera angles using sensitivity
		m_drag.azimuth -= m_drag.velocity.x * m_drag.sensitivity;
		m_drag.elevation += m_drag.velocity.y * m_drag.sensitivity;
		// Update camera position based on new orbital parameters
		updateOrbitCamera();

		// Dampen velocity for next frame
		m_drag.velocity *= m_drag.inertiaDecay;
	}
}

bool Application::initGui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(m_window, nullptr);
	ImGui_ImplWGPU_Init(m_context->getDevice(), 3, m_context->getSwapChainFormat(), m_depthTextureFormat);
	return true;
}

void Application::terminateGui()
{
	ImGui_ImplSDL2_Shutdown();
	ImGui_ImplWGPU_Shutdown();
}

void Application::updateGui(RenderPassEncoder renderPass)
{
	// Start the Dear ImGui frame
	ImGui_ImplWGPU_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	// Build our UI
	ImGui::Begin("Lighting & Camera Controls");

	// Shader reload button
	if (ImGui::Button("Reload Shader (F5)"))
	{
		m_pendingShaderReload = true;
	}

	ImGui::Separator();

	// Debug visualization toggle
	ImGui::Checkbox("Show Debug Axes", &m_showDebugAxes);

	// Material properties in a single section
	if (ImGui::CollapsingHeader("Material Properties"))
	{
		auto materialProperties = m_material->getCPUObject().getProperties();
		bool materialsChanged = false;
		materialsChanged |= ImGui::DragFloat3("Diffuse (Kd)", materialProperties.diffuse, 0.05f, 0.0f, 2.0f);
		materialsChanged |= ImGui::DragFloat3("Specular (Ks)", materialProperties.specular, 0.05f, 0.0f, 2.0f);
		materialsChanged |= ImGui::DragFloat("Hardness", &materialProperties.roughness, 0.5f, 1.0f, 128.0f);

		if (materialsChanged)
		{
			m_material->getCPUObject().setProperties(materialProperties);
		}
	}

	// Lights section
	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Add light button
		if (ImGui::Button("Add Light"))
		{
			addLight();
		}

		// Light list
		for (size_t i = 0; i < m_lights.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			LightStruct &light = m_lights[i];
			const char *lightTypeNames[] = {"Ambient", "Directional", "Point", "Spot"};

			// Create light header with type dropdown and remove button
			bool open = ImGui::TreeNodeEx(("Light " + std::to_string(i)).c_str(), ImGuiTreeNodeFlags_DefaultOpen);

			ImGui::SameLine(ImGui::GetWindowWidth() - 70);
			bool shouldRemove = false;
			if (ImGui::SmallButton("Remove"))
			{
				shouldRemove = true;
			}

			if (open)
			{
				// Light type
				int currentType = static_cast<int>(light.light_type);
				if (ImGui::Combo("Type", &currentType, lightTypeNames, 4))
				{
					light.light_type = static_cast<uint32_t>(currentType);
					m_lightsChanged = true;
				}

				if (ImGui::ColorEdit3("Color", glm::value_ptr(light.color)))
					m_lightsChanged = true;

				if (ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f))
					m_lightsChanged = true;

				glm::vec3 position = glm::vec3(light.transform[3]);

				if (m_lightDirectionsUI.find(i) == m_lightDirectionsUI.end())
				{
					glm::mat3 rotMatrix = glm::mat3(light.transform);
					glm::vec3 xAxis = glm::normalize(glm::vec3(rotMatrix[0]));
					float pitch = glm::degrees(asin(-xAxis.y));
					float yaw = glm::degrees(atan2(xAxis.z, xAxis.x));
					m_lightDirectionsUI[i] = glm::vec3(pitch, yaw, 0.0f);
				}

				glm::vec3 &angles = m_lightDirectionsUI[i];

				if (light.light_type > 1)
				{
					if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
					{
						// Update position while preserving rotation
						glm::mat3 rotation = glm::mat3(light.transform);
						light.transform = glm::mat4(
							glm::vec4(rotation[0], 0.0f),
							glm::vec4(rotation[1], 0.0f),
							glm::vec4(rotation[2], 0.0f),
							glm::vec4(position, 1.0f)
						);
						m_lightsChanged = true;
					}
				}

				if (light.light_type == 1 || light.light_type == 3)
				{
					if (ImGui::DragFloat3("Direction", glm::value_ptr(angles), 0.5f))
					{
						// Build rotation matrix
						glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(angles.z), glm::vec3(0, 0, 1)) * glm::rotate(glm::mat4(1.0f), glm::radians(angles.y), glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1.0f), glm::radians(angles.x), glm::vec3(1, 0, 0));

						// Apply rotation to transform
						if (light.light_type == 0)
						{
							// For directional lights, just use rotation
							light.transform = rotation;
						}
						else
						{
							// For spot lights, preserve position
							light.transform = glm::translate(glm::mat4(1.0f), position) * rotation;
						}
						m_lightsChanged = true;
					}
				}

				// Spot angle for spot lights
				if (light.light_type == 3)
				{
					if (ImGui::SliderFloat("Cone Angle", &light.spot_angle, 0.1f, 2.0f))
						m_lightsChanged = true;

					// Add a slider for controlling the softness of the spotlight edges
					if (ImGui::SliderFloat("Edge Softness", &light.spot_softness, 0.0f, 0.95f, "%.2f"))
						m_lightsChanged = true;
				}

				ImGui::TreePop();
			}

			ImGui::PopID();

			// Handle removal after PopID to keep the ID stack balanced
			if (shouldRemove)
			{
				removeLight(i);
				break; // Break since we modified the array
			}
		}
	}

	// Camera controls section
	if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Get current camera position, show as vector
		glm::vec3 cameraPos = m_cameraNode->getPosition();
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);

		// Display distance from origin
		float camDistance = glm::length(cameraPos);
		ImGui::Text("Distance from origin: %.2f", camDistance);

		// Camera orientation vectors
		if (auto transform = m_cameraNode->getTransform())
		{
			glm::vec3 forward = transform->forward();
			glm::vec3 up = transform->up();
			glm::vec3 right = transform->right();

			ImGui::Separator();
			ImGui::Text("Orientation Vectors:");
			ImGui::Text("Forward: (%.2f, %.2f, %.2f)", forward.x, forward.y, forward.z);
			ImGui::Text("Up: (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
			ImGui::Text("Right: (%.2f, %.2f, %.2f)", right.x, right.y, right.z);
			ImGui::Text("Azimuth/Elevation: (%.2f / %.2f)", m_drag.azimuth, m_drag.elevation);

			// Extract rotation as euler angles for easier understanding
			glm::quat rotation = transform->getRotation();
			glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));

			// Fix discontinuities in euler angle representation
			if (eulerAngles.x > 90.0f)
				eulerAngles.x -= 360.0f;
			if (eulerAngles.y > 180.0f)
				eulerAngles.y -= 360.0f;
			if (eulerAngles.z > 180.0f)
				eulerAngles.z -= 360.0f;

			ImGui::Text("Rotation (degrees): (%.1f, %.1f, %.1f)", eulerAngles.x, eulerAngles.y, eulerAngles.z);
		}

		ImGui::Separator();

		// Camera slider control
		float zoomPercentage = (camDistance - 2.0f) / 8.0f * 100.0f; // Convert to 0-100%, with larger range
		zoomPercentage = glm::clamp(zoomPercentage, 0.0f, 100.0f);	 // Ensure it's within range

		if (ImGui::SliderFloat("Camera Distance", &zoomPercentage, 0.0f, 100.0f, "%.0f%%"))
		{
			float newDistance = (zoomPercentage / 100.0f) * 8.0f + 2.0f; // Convert back, with larger range
			glm::vec3 dir = glm::normalize(cameraPos);
			m_cameraNode->getTransform()->setLocalPosition(dir * newDistance);
		}

		// Add camera reset controls
		if (ImGui::Button("Look At Origin"))
		{
			m_cameraNode->lookAt(glm::vec3(0.0f));
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset Camera"))
		{
			resetCamera();
		}
	}

	ImGui::End();

	// Render UI
	ImGui::Render();
	ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
}

void Application::addLight()
{
	LightStruct newLight;
	newLight.light_type = m_lights.empty() ? 0 : 2; // First light is directional, others are point by default

	// Set default direction angles (in degrees)
	float pitchDegrees = 140.0f;
	float yawDegrees = -30.0f;
	float rollDegrees = 0.0f;

	// Initialize rotation matrices from angles
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians(rollDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 rotation = rotZ * rotY * rotX; // Apply Z, then Y, then X rotation

	// Set position based on light type
	glm::vec3 position = glm::vec3(0.0f, 1.0f, 0.0f);

	// Create transform with both rotation and position
	glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * rotation;
	newLight.transform = transform;

	// Set other light properties
	newLight.color = {1.0f, 1.0f, 1.0f};
	newLight.intensity = 1.0f;
	newLight.spot_angle = 0.5f;
	newLight.spot_softness = 0.2f;

	// Store the index of the new light
	size_t newLightIndex = m_lights.size();

	// Add the light to the array
	m_lights.push_back(newLight);

	// Store the UI angles for the new light in the member variable
	m_lightDirectionsUI[newLightIndex] = glm::vec3(pitchDegrees, yawDegrees, rollDegrees);

	m_lightsChanged = true;
}

void Application::removeLight(size_t index)
{
	if (index < m_lights.size())
	{
		// Remove the light from the array
		m_lights.erase(m_lights.begin() + index);

		// Remove the UI angles for this light
		m_lightDirectionsUI.erase(index);

		// Re-index any UI angles for lights after the removed one
		for (size_t i = index; i < m_lights.size(); i++)
		{
			if (m_lightDirectionsUI.find(i + 1) != m_lightDirectionsUI.end())
			{
				m_lightDirectionsUI[i] = m_lightDirectionsUI[i + 1];
				m_lightDirectionsUI.erase(i + 1);
			}
		}

		m_lightsChanged = true;
	}
}

bool Application::reloadShader()
{
	spdlog::info("Reloading shader...");

	// Wait for any pending GPU operations to complete
	m_context->getQueue().submit(0, nullptr);

	// Create a new shader module first
	auto newShaderModule = engine::resources::ResourceManager::loadShaderModule(
		engine::core::PathProvider::getResource("shader.wgsl"),
		m_context->getDevice()
	);

	if (!newShaderModule)
	{
		spdlog::error("Failed to reload shader module!");
		return false;
	}

	// Create new pipeline with updated shader
	engine::rendering::webgpu::WebGPUShaderInfo vertexShaderInfo(newShaderModule, "vs_main");
	engine::rendering::webgpu::WebGPUShaderInfo fragmentShaderInfo(newShaderModule, "fs_main");

	wgpu::RenderPipelineDescriptor pipelineDesc = m_context->pipelineFactory().createRenderPipelineDescriptor(
		&vertexShaderInfo,
		&fragmentShaderInfo,
		m_context->getSwapChainFormat(),
		m_depthTextureFormat,
		true
	);

	// Use the existing pipeline layout
	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = kBindGroupLayoutCount;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)m_bindGroupLayouts.data();
	wgpu::PipelineLayout layout = m_context->getDevice().createPipelineLayout(layoutDesc);
	pipelineDesc.layout = layout;

	// Create the new pipeline
	auto newPipeline = m_context->pipelineFactory().createRenderPipeline(pipelineDesc);

	// Release the temporary layout
	if (layout)
	{
		layout.release();
	}

	if (!newPipeline)
	{
		spdlog::error("Failed to create new pipeline after shader reload!");
		if (newShaderModule)
		{
			newShaderModule.release();
		}
		return false;
	}

	// Only release old resources after new ones are successfully created
	if (m_pipeline)
	{
		m_pipeline.release();
	}
	if (m_shaderModule)
	{
		m_shaderModule.release();
	}

	// Assign new resources
	m_shaderModule = newShaderModule;
	m_pipeline = newPipeline;

	spdlog::info("Shader reloaded successfully");
	return true;
}

void Application::updateSceneGraph(float deltaTime)
{
	// Simply delegate to the Scene class which handles the full update cycle
	if (m_scene)
	{
		m_scene->update(deltaTime);
		m_scene->lateUpdate(deltaTime);
	}
}

void Application::updateOrbitCamera()
{
	m_drag.azimuth = fmod(m_drag.azimuth, 2.0f * glm::pi<float>());
	if (m_drag.azimuth < 0)
	{
		m_drag.azimuth += 2.0f * glm::pi<float>();
	}
	// Clamp distance
	m_drag.distance = glm::clamp(m_drag.distance, 0.5f, 20.0f);

	// --- Convert spherical coordinates to Cartesian (LH Y-up Z-forward) ---
	float x = cos(m_drag.elevation) * cos(m_drag.azimuth);
	float y = sin(m_drag.elevation);
	float z = cos(m_drag.elevation) * sin(m_drag.azimuth);

	glm::vec3 position = m_drag.targetPoint + glm::vec3(x, y, z) * m_drag.distance;

	if (m_cameraNode && m_cameraNode->getTransform())
	{
		m_cameraNode->getTransform()->setLocalPosition(position);
		m_cameraNode->lookAt(m_drag.targetPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

} // namespace engine