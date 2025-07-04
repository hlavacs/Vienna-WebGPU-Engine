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
		if (!initTextures())
			return false;
		if (!initGeometry())
			return false;
		if (!initUniforms())
			return false;
		if (!initLightingUniforms())
			return false;
		if (!initBindGroup())
			return false;
		if (!initUniformBindGroup())
			return false;
		if (!initLightBindGroup())
			return false;
		if (!initMaterialBindGroup())
			return false;
		if (!initGui())
			return false;
		return true;
	}

	void Application::onFrame()
	{
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

		renderPass.setPipeline(m_pipeline);

		renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * sizeof(Vertex));
		if (m_indexCount > 0)
			renderPass.setIndexBuffer(m_indexBuffer, IndexFormat::Uint32, 0, m_indexCount * sizeof(uint32_t));

		// Set binding group
		renderPass.setBindGroup(0, m_uniformBindGroup, 0, nullptr);
		renderPass.setBindGroup(1, m_materialBindGroup, 0, nullptr);
		renderPass.setBindGroup(2, m_lightBindGroup, 0, nullptr);

		if (m_indexCount)
			renderPass.drawIndexed(m_indexCount, 1, 0, 0, 0);
		else
			renderPass.draw(m_vertexCount, 1, 0, 0);

		// We add the GUI drawing commands to the render pass
		updateGui(renderPass);

		// Defensive check before ending render pass
		assert(renderPass && "RenderPassEncoder is invalid before end()");
		// Defensive: check that all required resources are valid before ending the pass
		assert(m_pipeline && "Pipeline is invalid before end()");
		assert(m_vertexBuffer && "Vertex buffer is invalid before end()");
		if (m_indexCount > 0)
			assert(m_indexBuffer && "Index buffer is invalid before end()");
		// assert(m_bindGroup && "Bind group is invalid before end()");
		assert(m_baseColorTextureView && "Base color texture view is invalid before end()");
		assert(m_normalTextureView && "Normal texture view is invalid before end()");
		assert(m_depthTextureView && "Depth texture view is invalid before end()");
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
		terminateBindGroup();
		terminateLightingUniforms();
		terminateUniforms();
		terminateGeometry();
		terminateTextures();
		terminateRenderPipeline();
		terminateBindGroupLayout();
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
			vec2 currentMouse = vec2(-(float)xpos, (float)ypos);
			vec2 delta = (currentMouse - m_drag.startMouse) * m_drag.sensitivity;
			m_cameraState.angles = m_drag.startCameraState.angles + delta;
			// Clamp to avoid going too far when orbitting up/down
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
			m_drag.startMouse = vec2(-(float)xpos, (float)ypos);
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
		m_pipeline.release();
		m_shaderModule.release();
	}

	bool Application::initTextures()
	{
		// Create a sampler
		SamplerDescriptor samplerDesc;
		samplerDesc.addressModeU = AddressMode::Repeat;
		samplerDesc.addressModeV = AddressMode::Repeat;
		samplerDesc.addressModeW = AddressMode::Repeat;
		samplerDesc.magFilter = FilterMode::Linear;
		samplerDesc.minFilter = FilterMode::Linear;
		samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
		samplerDesc.lodMinClamp = 0.0f;
		samplerDesc.lodMaxClamp = 8.0f;
		samplerDesc.compare = CompareFunction::Undefined;
		samplerDesc.maxAnisotropy = 1;
		m_sampler = m_context->getDevice().createSampler(samplerDesc);

		// Create textures
		// m_baseColorTexture =  m_resourceManager->loadTexture(engine::core::PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg", m_device, &m_baseColorTextureView);
		m_baseColorTexture = m_resourceManager->loadTexture(engine::core::PathProvider::getResource("fourareen2K_albedo.jpg"), *m_context, &m_baseColorTextureView);
		// m_baseColorTexture = m_resourceManager->loadTexture(engine::core::PathProvider::getResource("fox/Texture.png"), m_device, &m_baseColorTextureView);
		if (!m_baseColorTexture)
		{
			std::cerr << "Could not load base color texture!" << std::endl;
			return false;
		}
		assert(m_baseColorTextureView && "Base color texture view must be valid");

		// m_normalTexture =  m_resourceManager->loadTexture(engine::core::PathProvider::getResource("cobblestone_floor_08_nor_gl_2k.png"), m_device, &m_normalTextureView);
		m_normalTexture = m_resourceManager->loadTexture(engine::core::PathProvider::getResource("fourareen2K_normals.png"), *m_context, &m_normalTextureView);
		// m_normalTexture = m_resourceManager->createNeutralNormalTexture(m_device, &m_normalTextureView);
		// m_normalTexture = m_resourceManager->loadTexture(engine::core::PathProvider::getResource("fox/neutral_normal.png"), m_device, &m_normalTextureView);
		if (!m_normalTexture)
		{
			std::cerr << "Could not load normal texture!" << std::endl;
			return false;
		}
		assert(m_normalTextureView && "Normal texture view must be valid");

		// Defensive asserts for resource/format mismatches
		assert(m_baseColorTextureView != nullptr && "Base color texture view is null!");
		assert(m_normalTextureView != nullptr && "Normal texture view is null!");
		assert(m_baseColorTexture && "Base color texture is invalid!");
		assert(m_normalTexture && "Normal texture is invalid!");

		return m_baseColorTextureView != nullptr && m_normalTextureView != nullptr;
	}

	void Application::terminateTextures()
	{
		m_baseColorTextureView.release();
		m_baseColorTexture.destroy();
		m_baseColorTexture.release();
		if (m_normalTextureView)
			m_normalTextureView.release();
		m_normalTexture.destroy();
		m_normalTexture.release();
		m_sampler.release();
	}

	bool Application::initGeometry()
	{
		engine::rendering::Mesh mesh{};
		// bool success = ResourceManager::loadGeometryFromObj(engine::core::PathProvider::getResource("cylinder.obj"), vertexData);
		bool success = m_resourceManager->loadGeometryFromObj(engine::core::PathProvider::getResource("fourareen.obj"), mesh, true);
		// bool success = m_resourceManager->loadGeometryFromObj(engine::core::PathProvider::getResource("fox/Fox.gltf"), mesh, true);
		if (!success)
		{
			std::cerr << "Could not load geometry!" << std::endl;
		}

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
		// Create vertex buffer
		BufferDescriptor bufferDesc;
		bufferDesc.size = mesh.vertices.size() * sizeof(Vertex);
		bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
		bufferDesc.mappedAtCreation = false;
		m_vertexBuffer = m_context->getDevice().createBuffer(bufferDesc);
		m_context->getQueue().writeBuffer(m_vertexBuffer, 0, mesh.vertices.data(), bufferDesc.size);

		m_vertexCount = static_cast<int32_t>(mesh.vertices.size());

		if (mesh.isIndexed())
		{
			BufferDescriptor indexBufferDesc;
			indexBufferDesc.size = mesh.indices.size() * sizeof(uint32_t); // Assuming indices are uint32_t
			indexBufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
			indexBufferDesc.mappedAtCreation = false;
			m_indexBuffer = m_context->getDevice().createBuffer(indexBufferDesc);
			m_context->getQueue().writeBuffer(m_indexBuffer, 0, mesh.indices.data(), indexBufferDesc.size);

			m_indexCount = static_cast<int32_t>(mesh.indices.size());
			return m_vertexBuffer != nullptr && m_indexBuffer != nullptr;
		}
		else
		{
			m_indexBuffer = nullptr; // No index buffer
			m_indexCount = 0;
			return m_vertexBuffer != nullptr;
		}

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
		m_vertexBuffer.destroy();
		m_vertexBuffer.release();
		m_vertexCount = 0;
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
		// Create uniform buffer
		BufferDescriptor bufferDesc;
		bufferDesc.size = sizeof(LightingUniforms);
		bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
		bufferDesc.mappedAtCreation = false;
		m_lightingUniformBuffer = m_context->getDevice().createBuffer(bufferDesc);

		// Initial values
		m_lightingUniforms.directions[0] = {0.5f, -0.9f, 0.1f, 0.0f};
		m_lightingUniforms.directions[1] = {0.2f, 0.4f, 0.3f, 0.0f};
		m_lightingUniforms.colors[0] = {1.0f, 0.9f, 0.6f, 1.0f};
		m_lightingUniforms.colors[1] = {0.6f, 0.9f, 1.0f, 1.0f};

		updateLightingUniforms();

		return m_lightingUniformBuffer != nullptr;
	}

	void Application::terminateLightingUniforms()
	{
		m_lightingUniformBuffer.destroy();
		m_lightingUniformBuffer.release();
	}

	void Application::updateLightingUniforms()
	{
		if (m_lightingUniformsChanged)
		{
			m_context->getQueue().writeBuffer(m_lightingUniformBuffer, 0, &m_lightingUniforms, sizeof(LightingUniforms));
			m_lightingUniformsChanged = false;
		}
	}

	bool Application::initBindGroupLayout()
	{
		// Uniform Bind Group Layout
		m_bindGroupLayouts[Uniform] = m_context->bindGroupFactory().createCustomBindGroupLayout(
			m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<MyUniforms>()
		);
		// Material Bind Group Layout
		m_bindGroupLayouts[Material] = m_context->bindGroupFactory().createDefaultMaterialBindGroupLayout();
		// Light Bind Group Layout
		std::vector<wgpu::BindGroupLayoutEntry> entries;
		wgpu::BindGroupLayoutEntry lightEntry = {};
		lightEntry.binding = 0;
		lightEntry.visibility = wgpu::ShaderStage::Fragment;
		lightEntry.buffer.type = wgpu::BufferBindingType::Uniform;
		lightEntry.buffer.minBindingSize = sizeof(LightingUniforms);
		entries.push_back(lightEntry);
		wgpu::BindGroupLayoutDescriptor desc{};
		desc.entryCount = (uint32_t)entries.size();
		desc.entries = entries.data();
		m_bindGroupLayouts[Light] = m_context->getDevice().createBindGroupLayout(desc);
		return m_bindGroupLayouts[Uniform] && m_bindGroupLayouts[Material] && m_bindGroupLayouts[Light];
	}

	void Application::terminateBindGroupLayout()
	{
		m_context->bindGroupFactory().cleanup();
	}

	bool Application::initBindGroup()
	{
		return true;
		// Create a binding
		std::vector<BindGroupEntry> bindings(5);
		//                                   ^ This was a 4

		bindings[0].binding = 0;
		bindings[0].buffer = m_uniformBuffer;
		bindings[0].offset = 0;
		bindings[0].size = sizeof(MyUniforms);

		bindings[1].binding = 1;
		bindings[1].textureView = m_baseColorTextureView;

		if (m_normalTextureView)
		{
			bindings[2].binding = 2;
			bindings[2].textureView = m_normalTextureView;
		}

		bindings[3].binding = 3;
		bindings[3].sampler = m_sampler;
		//       ^ This was a 2

		bindings[4].binding = 4;
		bindings[4].buffer = m_lightingUniformBuffer;
		bindings[4].offset = 0;
		bindings[4].size = sizeof(LightingUniforms);
		//       ^ This was a 3

		BindGroupDescriptor bindGroupDesc;
		bindGroupDesc.layout = m_bindGroupLayouts[Material];
		bindGroupDesc.entryCount = (uint32_t)bindings.size();
		bindGroupDesc.entries = bindings.data();
		m_bindGroup = m_context->getDevice().createBindGroup(bindGroupDesc);

		// Defensive asserts for bind group resource validity
		assert(m_uniformBuffer);
		assert(m_baseColorTextureView);
		assert(m_normalTextureView);
		assert(m_sampler);
		assert(m_lightingUniformBuffer);

		return m_bindGroup != nullptr;
	}

	bool Application::initMaterialBindGroup()
	{
		std::vector<wgpu::BindGroupEntry> entries(3);
		entries[0].binding = 0;
		entries[0].textureView = m_baseColorTextureView;
		entries[1].binding = 1;
		entries[1].textureView = m_normalTextureView;
		entries[2].binding = 2;
		entries[2].sampler = m_sampler;

		wgpu::BindGroupDescriptor desc{};
		desc.layout = m_bindGroupLayouts[Material]; // You need to create this layout for just these 3 bindings
		desc.entryCount = (uint32_t)entries.size();
		desc.entries = entries.data();
		m_materialBindGroup = m_context->getDevice().createBindGroup(desc);
		return m_materialBindGroup != nullptr;
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

		wgpu::BindGroupEntry lightEntry = {};
		lightEntry.binding = 0;
		lightEntry.buffer = m_lightingUniformBuffer;
		lightEntry.offset = 0;
		lightEntry.size = sizeof(LightingUniforms);
		entries.push_back(lightEntry);

		wgpu::BindGroupDescriptor desc{};
		desc.layout = m_bindGroupLayouts[Light]; // Layout with N+1 bindings
		desc.entryCount = (uint32_t)entries.size();
		desc.entries = entries.data();
		m_lightBindGroup = m_context->getDevice().createBindGroup(desc);
		return m_lightBindGroup != nullptr;
	}

	void Application::terminateBindGroup()
	{
		// m_bindGroup.release();
	}

	void Application::updateProjectionMatrix()
	{
		// Update projection matrix
		int width, height;
		SDL_GL_GetDrawableSize(m_window, &width, &height);
		float ratio = width / (float)height;
		m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, ratio, 0.01f, 100.0f);
		m_context->getQueue().writeBuffer(
			m_uniformBuffer,
			offsetof(MyUniforms, projectionMatrix),
			&m_uniforms.projectionMatrix,
			sizeof(MyUniforms::projectionMatrix));
	}

	void Application::updateViewMatrix()
	{
		float cx = cos(m_cameraState.angles.x);
		float sx = sin(m_cameraState.angles.x);
		float cy = cos(m_cameraState.angles.y);
		float sy = sin(m_cameraState.angles.y);
		vec3 position = vec3(cx * cy, sx * cy, sy) * std::exp(-m_cameraState.zoom);
		m_uniforms.viewMatrix = glm::lookAt(position, vec3(0.0f), vec3(0, 0, 1));
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
		/*
		ImGui_ImplWGPU_InitInfo init_info;
		init_info.Device = m_device;
		init_info.DepthStencilFormat = m_depthTextureFormat;
		init_info.RenderTargetFormat = m_swapChainFormat;
		ImGui_ImplWGPU_Init(&init_info);
		*/
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
			bool changed = false;
			ImGui::Begin("Lighting");
			changed = ImGui::ColorEdit3("Color #0", glm::value_ptr(m_lightingUniforms.colors[0])) || changed;
			changed = ImGui::DragDirection("Direction #0", m_lightingUniforms.directions[0]) || changed;
			changed = ImGui::ColorEdit3("Color #1", glm::value_ptr(m_lightingUniforms.colors[1])) || changed;
			changed = ImGui::DragDirection("Direction #1", m_lightingUniforms.directions[1]) || changed;
			changed = ImGui::SliderFloat("Hardness", &m_lightingUniforms.hardness, 1.0f, 100.0f) || changed;
			changed = ImGui::SliderFloat("K Diffuse", &m_lightingUniforms.kd, 0.0f, 1.0f) || changed;
			changed = ImGui::SliderFloat("K Specular", &m_lightingUniforms.ks, 0.0f, 1.0f) || changed;
			ImGui::End();
			m_lightingUniformsChanged = changed;
		}

		// Draw the UI
		ImGui::EndFrame();
		// Convert the UI defined above into low-level drawing commands
		ImGui::Render();
		// Execute the low-level drawing commands on the WebGPU backend
		ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
	}
}