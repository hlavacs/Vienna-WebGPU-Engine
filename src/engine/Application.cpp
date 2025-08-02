// This file is based on the "Learn WebGPU for C++" tutorial by Elie Michel (https://github.com/eliemichel/LearnWebGPU).
// Significant modifications, refactoring, and extensions have been made for this project.
// Original code © 2022-2024 Elie Michel, MIT License.

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/polar_coordinates.hpp>

#include <SDL.h>
#include <sdl2webgpu.h>

#include <imgui.h>
#include <backends/imgui_impl_wgpu.h>
#include <backends/imgui_impl_sdl2.h>

#include <iostream>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <string>
#include <array>

#include "engine/Application.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"

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
		m_window = SDL_CreateWindow("Learn WebGPU",
									SDL_WINDOWPOS_CENTERED,
									SDL_WINDOWPOS_CENTERED,
									width,
									height,
									windowFlags);
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
		if (!initUniformBindGroup())
			return false;
		if (!initLightBindGroup())
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
		processSDLEvents();
		updateDragInertia();
		updateLightingUniforms();

		// Update uniform buffer
		m_uniforms.time = static_cast<float>(static_cast<double>(SDL_GetTicks64() / 1000.0));
		m_context->getQueue().writeBuffer(m_uniformBuffer, offsetof(MyUniforms, time), &m_uniforms.time, sizeof(MyUniforms::time));

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

		// Set binding groups that apply to all models
		renderPass.setBindGroup(0, m_uniformBindGroup, 0, nullptr);
		renderPass.setBindGroup(2, m_lightBindGroup, 0, nullptr);

		// Render all WebGPU models
		for (const auto &model : m_webgpuModels)
		{
			model->render(encoder, renderPass);
		}

		// If no models were rendered, log a warning
		if (m_webgpuModels.empty())
		{
			spdlog::warn("No WebGPU models to render!");
		}

		// Render debug axes if enabled (before UI so they appear in world space)
		if (m_showDebugAxes)
		{
			// Load the debug shader module if not already loaded
			if (!m_debugShaderModule)
			{
				m_debugShaderModule = engine::resources::ResourceManager::loadShaderModule(
					engine::core::PathProvider::getResource("debug.wgsl"),
					m_context->getDevice());

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
						true);

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
					if (light.light_type == 1 || light.light_type == 2)
					{
						transform[3] = glm::vec4(light.position, 1.0f);
					}

					// For directional/spot lights, apply rotation
					if (light.light_type == 0 || light.light_type == 2)
					{
						glm::mat4 rotationMatrix = glm::mat4(1.0f);
						// Convert Euler angles (degrees) to radians
						glm::vec3 eulerRad = glm::radians(light.rotation);

						// Apply rotations in XYZ order
						rotationMatrix = glm::rotate(rotationMatrix, eulerRad.x, glm::vec3(1, 0, 0));
						rotationMatrix = glm::rotate(rotationMatrix, eulerRad.y, glm::vec3(0, 1, 0));
						rotationMatrix = glm::rotate(rotationMatrix, eulerRad.z, glm::vec3(0, 0, 1));

						// Combine rotation with translation
						transform = transform * rotationMatrix;
					}

					// Scale axes based on light intensity
					float scale = 0.2f + light.intensity * 0.1f;
					transform = glm::scale(transform, glm::vec3(scale));

					transforms.push_back(transform);
				}

				// Create combined view-projection matrix
				glm::mat4 viewProj = m_uniforms.projectionMatrix * m_uniforms.viewMatrix;

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

	void Application::processSDLEvents()
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
				onMouseMove(event.motion.x, event.motion.y);
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				onMouseButton(
					event.button.button,
					event.button.state == SDL_PRESSED ? 1 : 0,
					event.button.x,
					event.button.y);
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

	void Application::onMouseMove(double xpos, double ypos)
	{
		if (m_drag.active)
		{
			// When Y is up, we need to adjust the mouse coordinates differently
			// X controls rotation around Y axis, Y controls rotation around X axis
			glm::vec2 currentMouse = glm::vec2(-(float)xpos, (float)ypos);
			glm::vec2 delta = (currentMouse - m_drag.startMouse) * m_drag.sensitivity;
			
			// For Y-up, we swap the meaning of x and y angles
			// x now controls horizontal rotation (around Y)
			// y controls vertical rotation (around X)
			m_cameraState.angles.x = m_drag.startCameraState.angles.x + delta.x; // Horizontal rotation
			m_cameraState.angles.y = m_drag.startCameraState.angles.y + delta.y; // Vertical rotation
			
			// Clamp to avoid going too far when orbiting up/down
			m_cameraState.angles.y = glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
			updateViewMatrix();

			// Inertia
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
			m_drag.startMouse = glm::vec2(-(float)xpos, (float)ypos);
			m_drag.startCameraState = m_cameraState;
		}
		else if (!pressed && button == SDL_BUTTON_LEFT)
		{
			m_drag.active = false;
		}
	}

	void Application::onScroll(double /* xoffset */, double yoffset)
	{
		m_cameraState.zoom += m_drag.scrollSensitivity * static_cast<float>(yoffset);
		m_cameraState.zoom = glm::clamp(m_cameraState.zoom, -2.0f, 2.0f);
		updateViewMatrix();
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
			true);

		// Create the pipeline layout
		wgpu::PipelineLayoutDescriptor layoutDesc{};
		layoutDesc.bindGroupLayoutCount = kBindGroupLayoutCount;
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

	bool Application::initUniforms()
	{
		// Create uniform buffer
		BufferDescriptor bufferDesc;
		bufferDesc.size = sizeof(MyUniforms);
		bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
		bufferDesc.mappedAtCreation = false;
		m_uniformBuffer = m_context->getDevice().createBuffer(bufferDesc);

		// Upload the initial value of the uniforms
		m_uniforms.modelMatrix = mat4x4(1.0);
		m_uniforms.viewMatrix = glm::lookAt(vec3(-2.0f, -3.0f, 2.0f), vec3(0.0f), vec3(0, 0, 1));
		m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, 640.0f / 480.0f, 0.01f, 100.0f);
		m_uniforms.time = 1.0f;
		m_uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
		m_context->getQueue().writeBuffer(m_uniformBuffer, 0, &m_uniforms, sizeof(MyUniforms));

		updateProjectionMatrix();
		updateViewMatrix();

		return m_uniformBuffer != nullptr;
	}

	void Application::terminateUniforms()
	{
		m_uniformBuffer.destroy();
		m_uniformBuffer.release();
	}

	bool Application::initLightingUniforms()
	{
		// Initialize default material properties
		m_lightsBufferHeader.kd = 1.0f;
		m_lightsBufferHeader.ks = 0.5f;
		m_lightsBufferHeader.hardness = 32.0f;

		// Initialize the lights buffer
		// Create a storage buffer large enough for several lights
		const size_t maxLights = 16; // Support up to 16 lights
		const size_t lightsBufferSize = sizeof(LightsBuffer) + maxLights * sizeof(LightStruct);

		BufferDescriptor lightsBufDesc;
		lightsBufDesc.size = lightsBufferSize;
		lightsBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
		lightsBufDesc.mappedAtCreation = false;
		m_lightsBuffer = m_context->getDevice().createBuffer(lightsBufDesc);

		// Add a default directional light
		if (m_lights.empty())
		{
			addLight();
		}

		return m_lightsBuffer != nullptr;
	}

	void Application::terminateLightingUniforms()
	{
		if (m_lightsBuffer)
		{
			m_lightsBuffer.destroy();
			m_lightsBuffer.release();
		}
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
				sizeof(LightsBuffer));

			// Write the lights array right after the header
			if (!m_lights.empty())
			{
				m_context->getQueue().writeBuffer(
					m_lightsBuffer,
					sizeof(LightsBuffer),
					m_lights.data(),
					m_lights.size() * sizeof(LightStruct));
			}

			m_lightsChanged = false;
		}
	}

	bool Application::initBindGroupLayout()
	{
		// Uniform Bind Group Layout
		m_bindGroupLayouts[Uniform] = m_context->bindGroupFactory().createCustomBindGroupLayout(
			m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<MyUniforms>());
		// Material Bind Group Layout
		m_bindGroupLayouts[Material] = m_context->bindGroupFactory().createDefaultMaterialBindGroupLayout();
		// Light Bind Group Layout
		std::vector<wgpu::BindGroupLayoutEntry> entries;

		// Entry for the lights storage buffer
		wgpu::BindGroupLayoutEntry lightsStorageEntry = {};
		lightsStorageEntry.binding = 0;
		lightsStorageEntry.visibility = wgpu::ShaderStage::Fragment;
		lightsStorageEntry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
		lightsStorageEntry.buffer.minBindingSize = 0; // Dynamic size
		entries.push_back(lightsStorageEntry);

		wgpu::BindGroupLayoutDescriptor desc{};
		desc.entryCount = (uint32_t)entries.size();
		desc.entries = entries.data();
		m_bindGroupLayouts[Light] = m_context->getDevice().createBindGroupLayout(desc);
		return m_bindGroupLayouts[Uniform] && m_bindGroupLayouts[Material] && m_bindGroupLayouts[Light];
	}

	bool Application::initUniformBindGroup()
	{
		wgpu::BindGroupEntry entry = {};
		entry.binding = 0;
		entry.buffer = m_uniformBuffer;
		entry.offset = 0;
		entry.size = sizeof(MyUniforms);

		wgpu::BindGroupDescriptor desc{};
		desc.layout = m_bindGroupLayouts[Uniform]; // You need to create this layout for just the uniform buffer
		desc.entryCount = 1;
		desc.entries = &entry;
		m_uniformBindGroup = m_context->getDevice().createBindGroup(desc);
		return m_uniformBindGroup != nullptr;
	}

	bool Application::initLightBindGroup()
	{
		std::vector<wgpu::BindGroupEntry> entries;

		// The storage buffer entry for multiple lights
		wgpu::BindGroupEntry lightsStorageEntry = {};
		lightsStorageEntry.binding = 0;
		lightsStorageEntry.buffer = m_lightsBuffer;
		lightsStorageEntry.offset = 0;
		// Calculate max size for the buffer - header plus space for multiple lights
		uint64_t maxSize = sizeof(LightsBuffer) + 16 * sizeof(LightStruct); // Space for up to 16 lights
		lightsStorageEntry.size = maxSize;
		entries.push_back(lightsStorageEntry);

		wgpu::BindGroupDescriptor desc{};
		desc.layout = m_bindGroupLayouts[Light];
		desc.entryCount = (uint32_t)entries.size();
		desc.entries = entries.data();
		m_lightBindGroup = m_context->getDevice().createBindGroup(desc);
		return m_lightBindGroup != nullptr;
	}

	void Application::updateProjectionMatrix()
	{
		// Unity-style: left-handed, Y-up, +Z forward
		int width, height;
		SDL_GL_GetDrawableSize(m_window, &width, &height);
		float ratio = width / (float)height;
		// glm::perspectiveLH_ZO: left-handed, zero-to-one depth, +Z forward, +Y up
		m_uniforms.projectionMatrix = glm::perspective(45.0f * PI / 180.0f, ratio, 0.01f, 100.0f);
		m_context->getQueue().writeBuffer(
			m_uniformBuffer,
			offsetof(MyUniforms, projectionMatrix),
			&m_uniforms.projectionMatrix,
			sizeof(MyUniforms::projectionMatrix));
	}

	void Application::updateViewMatrix()
	{
		// Unity-style: left-handed, Y-up, +Z forward
		float cx = cos(m_cameraState.angles.x);
		float sx = sin(m_cameraState.angles.x);
		float cy = cos(m_cameraState.angles.y);
		float sy = sin(m_cameraState.angles.y);
		
		// For Y-up camera orbit:
		// x controls rotation around Y axis (horizontal)
		// y controls rotation around X axis (vertical)
		// Position is calculated differently to maintain orbit around the origin
		glm::vec3 position = glm::vec3(
			cx * cos(m_cameraState.angles.y), // X position
			sy,                              // Y position (up)
			sx * cos(m_cameraState.angles.y)  // Z position
		) * std::exp(-m_cameraState.zoom);
		
		// Target is at origin, up is +Y
		m_uniforms.viewMatrix = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0, 1, 0));
		m_context->getQueue().writeBuffer(
			m_uniformBuffer,
			offsetof(MyUniforms, viewMatrix),
			&m_uniforms.viewMatrix,
			sizeof(MyUniforms::viewMatrix));
	}

	void Application::updateDragInertia()
	{
		constexpr float eps = 1e-4f;
		// Apply inertia only when the user released the click.
		if (!m_drag.active)
		{
			// Avoid updating the matrix when the velocity is no longer noticeable
			if (std::abs(m_drag.velocity.x) < eps && std::abs(m_drag.velocity.y) < eps)
			{
				return;
			}
			m_cameraState.angles += m_drag.velocity;
			m_cameraState.angles.y = glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
			// Dampen the velocity so that it decreases exponentially and stops
			// after a few frames.
			m_drag.velocity *= m_drag.intertia;
			updateViewMatrix();
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
		{
			ImGui::Begin("Lighting");

			// Add a button to reload shader
			if (ImGui::Button("Reload Shader"))
			{
				m_pendingShaderReload = true;
			}
			ImGui::SameLine();
			ImGui::Text("(or press F5)");

			ImGui::Separator();

			ImGui::Checkbox("Show Debug Axes", &m_showDebugAxes);

			// Material properties (moved from per-light to global)
			if (ImGui::CollapsingHeader("Material Properties", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::SliderFloat("Diffuse (Kd)", &m_lightsBufferHeader.kd, 0.0f, 2.0f))
					m_lightsChanged = true;

				if (ImGui::SliderFloat("Specular (Ks)", &m_lightsBufferHeader.ks, 0.0f, 2.0f))
					m_lightsChanged = true;

				if (ImGui::SliderFloat("Hardness/Shininess", &m_lightsBufferHeader.hardness, 1.0f, 128.0f))
					m_lightsChanged = true;
			}

			ImGui::Separator();

			// Add a button to add new lights
			if (ImGui::Button("Add Light"))
			{
				addLight();
			}

			// Display all lights with their properties
			for (size_t i = 0; i < m_lights.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				LightStruct &light = m_lights[i];

				// Create a collapsing header for each light
				if (ImGui::CollapsingHeader(("Light " + std::to_string(i)).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					// Light type selector
					const char *lightTypes[] = {"Directional", "Point", "Spot"};
					int currentType = static_cast<int>(light.light_type);
					if (ImGui::Combo("Type", &currentType, lightTypes, 3))
					{
						light.light_type = static_cast<uint32_t>(currentType);
						m_lightsChanged = true;
					}

					// Common properties for all light types
					if (ImGui::ColorEdit3("Color", glm::value_ptr(light.color)))
						m_lightsChanged = true;

					if (ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f))
						m_lightsChanged = true;

					// Type-specific properties
					if (light.light_type == 0) // Directional
					{
						if (ImGui::DragFloat3("Rotation", glm::value_ptr(light.rotation), 0.1f))
							m_lightsChanged = true;
					}
					else // Point or Spot
					{
						if (ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f))
							m_lightsChanged = true;

						if (light.light_type == 2) // Spot light
						{
							if (ImGui::DragFloat3("Rotation", glm::value_ptr(light.rotation), 0.1f))
								m_lightsChanged = true;
							if (ImGui::SliderFloat("Spot Angle", &light.spot_angle, 0.1f, 2.0f))
								m_lightsChanged = true;
						}
					}

					// Button to remove this light
					if (ImGui::Button("Remove Light"))
					{
						removeLight(i);
						ImGui::PopID();
						break; // Break since we modified the array
					}
				}
				ImGui::PopID();
			}

			ImGui::End();
		}

		// Draw the UI
		ImGui::EndFrame();
		// Convert the UI defined above into low-level drawing commands
		ImGui::Render();
		// Execute the low-level drawing commands on the WebGPU backend
		ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
	}

	void Application::addLight()
	{
		LightStruct newLight;
		newLight.light_type = m_lights.empty() ? 0 : 1; // First light is directional, others are point by default
		newLight.rotation = {50.0f, -30.0f, 0.0f};		// Default for directional/spot
		newLight.position = {0.0f, 2.0f, 0.0f};
		newLight.color = {1.0f, 1.0f, 1.0f};
		newLight.intensity = 1.0f;
		newLight.spot_angle = 0.5f;
		m_lights.push_back(newLight);
		m_lightsChanged = true;
	}

	void Application::removeLight(size_t index)
	{
		if (index < m_lights.size())
		{
			m_lights.erase(m_lights.begin() + index);
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
			m_context->getDevice());

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
			true);

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
}