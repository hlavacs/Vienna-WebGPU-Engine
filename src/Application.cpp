/**
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://github.com/eliemichel/LearnWebGPU
 *
 * MIT License
 * Copyright (c) 2022-2024 Elie Michel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define SDL_MAIN_HANDLED
#include "Application.h"
#include "engine/core/PathProvider.h"
#include "ResourceManager.h"

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

using namespace wgpu;
using VertexAttributes = engine::rendering::Vertex;

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

namespace engine::core
{
	Application::Application()
	{
		m_resourceManager = std::make_shared<engine::core::ResourceManager>(PathProvider::getResourceRoot());
	}

	///////////////////////////////////////////////////////////////////////////////
	// Public methods

	bool Application::onInit()
	{
		if (!initWindowAndDevice())
			return false;
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
		m_queue.writeBuffer(m_uniformBuffer, offsetof(MyUniforms, time), &m_uniforms.time, sizeof(MyUniforms::time));

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
		CommandEncoder encoder = m_device.createCommandEncoder(commandEncoderDesc);

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

		renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * sizeof(VertexAttributes));
		if (m_indexCount > 0)
			renderPass.setIndexBuffer(m_indexBuffer, IndexFormat::Uint32, 0, m_indexCount * sizeof(uint32_t));

		// Set binding group
		renderPass.setBindGroup(0, m_bindGroup, 0, nullptr);

		if (m_indexCount)
			renderPass.drawIndexed(m_indexCount, 1, 0, 0, 0);
		else
			renderPass.draw(m_vertexCount, 1, 0, 0);

		// We add the GUI drawing commands to the render pass
		updateGui(renderPass);

		renderPass.end();
		renderPass.release();

		nextTexture.release();

		CommandBufferDescriptor cmdBufferDescriptor{};
		cmdBufferDescriptor.label = "Command buffer";
		CommandBuffer command = encoder.finish(cmdBufferDescriptor);
		encoder.release();
		m_queue.submit(command);
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
		terminateWindowAndDevice();
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

	bool Application::initWindowAndDevice()
	{
#ifdef __EMSCRIPTEN__
		m_instance = wgpuCreateInstance(nullptr);
#else
		m_instance = createInstance(InstanceDescriptor{});
#endif

		if (!m_instance)
		{
			std::cerr << "Could not initialize WebGPU!" << std::endl;
			return false;
		}

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
#ifdef __EMSCRIPTEN__
		setCanvasNativeSize(width, height);
#endif

		if (!m_window)
		{
			std::cerr << "Could not open window!" << std::endl;
			return false;
		}

		std::cout << "Requesting adapter..." << std::endl;
		m_surface = SDL_GetWGPUSurface(m_instance, m_window);
		RequestAdapterOptions adapterOpts{};
		adapterOpts.compatibleSurface = m_surface;
		Adapter adapter = m_instance.requestAdapter(adapterOpts);
		std::cout << "Got adapter: " << adapter << std::endl;

		SupportedLimits supportedLimits;
#ifdef WEBGPU_BACKEND_WGPU
		adapter.getLimits(&supportedLimits);
#else
		supportedLimits.limits.minStorageBufferOffsetAlignment = 256;
		supportedLimits.limits.minUniformBufferOffsetAlignment = 256;
#endif

		std::cout << "Requesting device..." << std::endl;
		RequiredLimits requiredLimits = Default;
		requiredLimits.limits.maxVertexAttributes = 6;
		//                                          ^ This was a 4
		requiredLimits.limits.maxVertexBuffers = 1;
		requiredLimits.limits.maxBufferSize = 150000 * sizeof(VertexAttributes);
		requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);
		requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
		requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
#ifdef WEBGPU_BACKEND_WGPU
		requiredLimits.limits.maxInterStageShaderComponents = 17;
		//                                                    ^^ This was a 11
#else
		requiredLimits.limits.maxInterStageShaderComponents = 0xffffffffu; // undefined
#endif
		requiredLimits.limits.maxBindGroups = 2;
		requiredLimits.limits.maxUniformBuffersPerShaderStage = 2;
		requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
		// Allow textures up to 2K
		requiredLimits.limits.maxTextureDimension1D = 2048;
		requiredLimits.limits.maxTextureDimension2D = 2048;
		requiredLimits.limits.maxTextureArrayLayers = 1;
		requiredLimits.limits.maxSampledTexturesPerShaderStage = 2;
		//                                                       ^ This was 1
		requiredLimits.limits.maxSamplersPerShaderStage = 1;
		requiredLimits.limits.maxBindGroupsPlusVertexBuffers = 2;
		requiredLimits.limits.maxBindingsPerBindGroup = 5;

		DeviceDescriptor deviceDesc;
		deviceDesc.label = "My Device";
		deviceDesc.requiredFeatureCount = 0;
		deviceDesc.requiredLimits = &requiredLimits;
		deviceDesc.defaultQueue.label = "The default queue";
		m_device = adapter.requestDevice(deviceDesc);
		std::cout << "Got device: " << m_device << std::endl;

		// Add an error callback for more debug info
		m_errorCallbackHandle = m_device.setUncapturedErrorCallback([](ErrorType type, char const *message)
																	{
		 std::cout << "Device error: type " << type;
		 if (message) std::cout << " (message: " << message << ")";
		 std::cout << std::endl; });

		m_queue = m_device.getQueue();

#ifdef WEBGPU_BACKEND_WGPU
		m_swapChainFormat = m_surface.getPreferredFormat(adapter);
#else
		m_swapChainFormat = TextureFormat::BGRA8Unorm;

#endif

		adapter.release();
		return nullptr != m_device;
	}

	void Application::terminateWindowAndDevice()
	{
		m_queue.release();
		m_device.release();
		m_surface.release();
		m_instance.release();

		SDL_DestroyWindow(m_window);
		SDL_Quit();
	}

	bool Application::initSurface()
	{
		m_surface = SDL_GetWGPUSurface(m_instance, m_window);
		// Get the current size of the window's framebuffer:
		int width, height;
		SDL_GL_GetDrawableSize(m_window, &width, &height);

#ifdef WEBGPU_BACKEND_WGPU
		SurfaceConfiguration config;
		config.width = static_cast<uint32_t>(width);
		config.height = static_cast<uint32_t>(height);
		config.usage = TextureUsage::RenderAttachment;
		config.format = m_swapChainFormat;
		config.presentMode = PresentMode::Fifo;
		config.alphaMode = CompositeAlphaMode::Auto;
		config.device = m_device;
		m_surface.configure(config);
#else  // WEBGPU_BACKEND_WGPU
		SwapChainDescriptor desc;
		desc.width = static_cast<uint32_t>(width);
		desc.height = static_cast<uint32_t>(height);
		desc.usage = TextureUsage::RenderAttachment;
		desc.format = m_swapChainFormat;
		desc.presentMode = PresentMode::Fifo;
		m_swapChain = wgpuDeviceCreateSwapChain(m_device, m_surface, &desc);
#endif // WEBGPU_BACKEND_WGPU

		return true;
	}

	void Application::terminateSurface()
	{

#ifndef WEBGPU_BACKEND_WGPU
		m_swapChain.release();
		m_surface.release();
#else
		m_surface.unconfigure();
		m_surface.release();
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
		m_depthTexture = m_device.createTexture(depthTextureDesc);
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
		m_shaderModule = engine::core::ResourceManager::loadShaderModule(PathProvider::getResource("shader.wgsl"), m_device);
		std::cout << "Shader module: " << m_shaderModule << std::endl;

		std::cout << "Creating render pipeline..." << std::endl;
		RenderPipelineDescriptor pipelineDesc;

		// Vertex fetch
		std::vector<VertexAttribute> vertexAttribs(6);

		// Position attribute
		vertexAttribs[0].shaderLocation = 0;
		vertexAttribs[0].format = VertexFormat::Float32x3;
		vertexAttribs[0].offset = 0;

		// Normal attribute
		vertexAttribs[1].shaderLocation = 1;
		vertexAttribs[1].format = VertexFormat::Float32x3;
		vertexAttribs[1].offset = offsetof(VertexAttributes, normal);

		// Color attribute
		vertexAttribs[2].shaderLocation = 2;
		vertexAttribs[2].format = VertexFormat::Float32x3;
		vertexAttribs[2].offset = offsetof(VertexAttributes, color);

		// UV attribute
		vertexAttribs[3].shaderLocation = 3;
		vertexAttribs[3].format = VertexFormat::Float32x2;
		vertexAttribs[3].offset = offsetof(VertexAttributes, uv);

		// Tangent attribute
		vertexAttribs[4].shaderLocation = 4;
		vertexAttribs[4].format = VertexFormat::Float32x3;
		vertexAttribs[4].offset = offsetof(VertexAttributes, tangent);

		// Bitangent attribute
		vertexAttribs[5].shaderLocation = 5;
		vertexAttribs[5].format = VertexFormat::Float32x3;
		vertexAttribs[5].offset = offsetof(VertexAttributes, bitangent);

		VertexBufferLayout vertexBufferLayout;
		vertexBufferLayout.attributeCount = (uint32_t)vertexAttribs.size();
		vertexBufferLayout.attributes = vertexAttribs.data();
		vertexBufferLayout.arrayStride = sizeof(VertexAttributes);
		vertexBufferLayout.stepMode = VertexStepMode::Vertex;

		pipelineDesc.vertex.bufferCount = 1;
		pipelineDesc.vertex.buffers = &vertexBufferLayout;

		pipelineDesc.vertex.module = m_shaderModule;
		pipelineDesc.vertex.entryPoint = "vs_main";
		pipelineDesc.vertex.constantCount = 0;
		pipelineDesc.vertex.constants = nullptr;

		pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
		pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
		pipelineDesc.primitive.frontFace = FrontFace::CCW;
		pipelineDesc.primitive.cullMode = CullMode::None;

		FragmentState fragmentState;
		pipelineDesc.fragment = &fragmentState;
		fragmentState.module = m_shaderModule;
		fragmentState.entryPoint = "fs_main";
		fragmentState.constantCount = 0;
		fragmentState.constants = nullptr;

		BlendState blendState;
#ifdef WEBGPU_BACKEND_WGPU
		blendState.color.operation = BlendOperation::Add;
		blendState.alpha.operation = BlendOperation::Add;
#else
		blendState.color.operation = BlendOperation::Undefined;
		blendState.alpha.operation = BlendOperation::Undefined;
#endif
		blendState.color.srcFactor = BlendFactor::SrcAlpha;
		blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
		blendState.alpha.srcFactor = BlendFactor::Zero;
		blendState.alpha.dstFactor = BlendFactor::One;

		ColorTargetState colorTarget;
		colorTarget.format = m_swapChainFormat;
		colorTarget.blend = &blendState;
		colorTarget.writeMask = ColorWriteMask::All;

		fragmentState.targetCount = 1;
		fragmentState.targets = &colorTarget;

		DepthStencilState depthStencilState = Default;
		depthStencilState.depthCompare = CompareFunction::Less;
		depthStencilState.depthWriteEnabled = true;
		depthStencilState.format = m_depthTextureFormat;
		depthStencilState.stencilReadMask = 0;
		depthStencilState.stencilWriteMask = 0;
		depthStencilState.stencilFront.compare = CompareFunction::Always;
		depthStencilState.stencilFront.depthFailOp = StencilOperation::Keep;
		depthStencilState.stencilFront.failOp = StencilOperation::Keep;
		depthStencilState.stencilFront.passOp = StencilOperation::Keep;
		depthStencilState.stencilBack.compare = CompareFunction::Always;
		depthStencilState.stencilBack.depthFailOp = StencilOperation::Keep;
		depthStencilState.stencilBack.failOp = StencilOperation::Keep;
		depthStencilState.stencilBack.passOp = StencilOperation::Keep;

		pipelineDesc.depthStencil = &depthStencilState;

		pipelineDesc.multisample.count = 1;
		pipelineDesc.multisample.mask = ~0u;
		pipelineDesc.multisample.alphaToCoverageEnabled = false;

		// Create the pipeline layout
		PipelineLayoutDescriptor layoutDesc{};
		layoutDesc.bindGroupLayoutCount = 1;
		layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)&m_bindGroupLayout;
		PipelineLayout layout = m_device.createPipelineLayout(layoutDesc);
		pipelineDesc.layout = layout;

		m_pipeline = m_device.createRenderPipeline(pipelineDesc);
		std::cout << "Render pipeline: " << m_pipeline << std::endl;

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
		m_sampler = m_device.createSampler(samplerDesc);

		// Create textures
		// m_baseColorTexture = engine::core::ResourceManager::loadTexture(PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg", m_device, &m_baseColorTextureView);
		m_baseColorTexture = engine::core::ResourceManager::loadTexture(PathProvider::getResource("fourareen2K_albedo.jpg"), m_device, &m_baseColorTextureView);
		if (!m_baseColorTexture)
		{
			std::cerr << "Could not load base color texture!" << std::endl;
			return false;
		}

		// m_normalTexture = engine::core::ResourceManager::loadTexture(PathProvider::getResource("cobblestone_floor_08_nor_gl_2k.png"), m_device, &m_normalTextureView);
		m_normalTexture = engine::core::ResourceManager::loadTexture(PathProvider::getResource("fourareen2K_normals.png"), m_device, &m_normalTextureView);
		if (!m_normalTexture)
		{
			std::cerr << "Could not load normal texture!" << std::endl;
			return false;
		}

		return m_baseColorTextureView != nullptr && m_normalTextureView != nullptr;
	}

	void Application::terminateTextures()
	{
		m_baseColorTextureView.release();
		m_baseColorTexture.destroy();
		m_baseColorTexture.release();
		m_normalTextureView.release();
		m_normalTexture.destroy();
		m_normalTexture.release();
		m_sampler.release();
	}

	bool Application::initGeometry()
	{
		// Load mesh data from OBJ file
		engine::rendering::Mesh mesh{};
		// bool success = ResourceManager::loadGeometryFromObj(PathProvider::getResource("fourareen.obj"), vertexData);
		bool success = m_resourceManager->loadGeometryFromObj(PathProvider::getResource("fourareen.obj"), mesh, true);
		if (!success)
		{
			std::cerr << "Could not load geometry!" << std::endl;
			return false;
		}

		// Create vertex buffer
		BufferDescriptor bufferDesc;
		bufferDesc.size = mesh.vertices.size() * sizeof(VertexAttributes);
		bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
		bufferDesc.mappedAtCreation = false;
		m_vertexBuffer = m_device.createBuffer(bufferDesc);
		m_queue.writeBuffer(m_vertexBuffer, 0, mesh.vertices.data(), bufferDesc.size);

		m_vertexCount = static_cast<int32_t>(mesh.vertices.size());

		if (mesh.isIndexed())
		{
			BufferDescriptor indexBufferDesc;
			indexBufferDesc.size = mesh.indices.size() * sizeof(uint32_t); // Assuming indices are uint32_t
			indexBufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
			indexBufferDesc.mappedAtCreation = false;
			m_indexBuffer = m_device.createBuffer(indexBufferDesc);
			m_queue.writeBuffer(m_indexBuffer, 0, mesh.indices.data(), indexBufferDesc.size);

			m_indexCount = static_cast<int32_t>(mesh.indices.size());
			return m_vertexBuffer != nullptr && m_indexBuffer != nullptr;
		}
		else
		{
			m_indexBuffer = nullptr; // No index buffer
			m_indexCount = 0;
			return m_vertexBuffer != nullptr;
		}
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
		m_uniformBuffer = m_device.createBuffer(bufferDesc);

		// Upload the initial value of the uniforms
		m_uniforms.modelMatrix = mat4x4(1.0);
		m_uniforms.viewMatrix = glm::lookAt(vec3(-2.0f, -3.0f, 2.0f), vec3(0.0f), vec3(0, 0, 1));
		m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, 640.0f / 480.0f, 0.01f, 100.0f);
		m_uniforms.time = 1.0f;
		m_uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
		m_queue.writeBuffer(m_uniformBuffer, 0, &m_uniforms, sizeof(MyUniforms));

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
		m_lightingUniformBuffer = m_device.createBuffer(bufferDesc);

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
			m_queue.writeBuffer(m_lightingUniformBuffer, 0, &m_lightingUniforms, sizeof(LightingUniforms));
			m_lightingUniformsChanged = false;
		}
	}

	bool Application::initBindGroupLayout()
	{
		std::vector<BindGroupLayoutEntry> bindingLayoutEntries(5, Default);
		//                                                     ^ This was a 4

		// The uniform buffer binding
		BindGroupLayoutEntry &bindingLayout = bindingLayoutEntries[0];
		bindingLayout.binding = 0;
		bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
		bindingLayout.buffer.type = BufferBindingType::Uniform;
		bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

		// The base color texture binding
		BindGroupLayoutEntry &textureBindingLayout = bindingLayoutEntries[1];
		textureBindingLayout.binding = 1;
		textureBindingLayout.visibility = ShaderStage::Fragment;
		textureBindingLayout.texture.sampleType = TextureSampleType::Float;
		textureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

		// The normal map binding
		BindGroupLayoutEntry &normalTextureBindingLayout = bindingLayoutEntries[2];
		normalTextureBindingLayout.binding = 2;
		normalTextureBindingLayout.visibility = ShaderStage::Fragment;
		normalTextureBindingLayout.texture.sampleType = TextureSampleType::Float;
		normalTextureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

		// The texture sampler binding
		BindGroupLayoutEntry &samplerBindingLayout = bindingLayoutEntries[3];
		samplerBindingLayout.binding = 3;
		//                             ^ This was a 2
		samplerBindingLayout.visibility = ShaderStage::Fragment;
		samplerBindingLayout.sampler.type = SamplerBindingType::Filtering;

		// The lighting uniform buffer binding
		BindGroupLayoutEntry &lightingUniformLayout = bindingLayoutEntries[4];
		lightingUniformLayout.binding = 4;
		//                              ^ This was a 3
		lightingUniformLayout.visibility = ShaderStage::Fragment; // only Fragment is needed
		lightingUniformLayout.buffer.type = BufferBindingType::Uniform;
		lightingUniformLayout.buffer.minBindingSize = sizeof(LightingUniforms);

		// Create a bind group layout
		BindGroupLayoutDescriptor bindGroupLayoutDesc{};
		bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
		bindGroupLayoutDesc.entries = bindingLayoutEntries.data();
		m_bindGroupLayout = m_device.createBindGroupLayout(bindGroupLayoutDesc);

		return m_bindGroupLayout != nullptr;
	}

	void Application::terminateBindGroupLayout()
	{
		m_bindGroupLayout.release();
	}

	bool Application::initBindGroup()
	{
		// Create a binding
		std::vector<BindGroupEntry> bindings(5);
		//                                   ^ This was a 4

		bindings[0].binding = 0;
		bindings[0].buffer = m_uniformBuffer;
		bindings[0].offset = 0;
		bindings[0].size = sizeof(MyUniforms);

		bindings[1].binding = 1;
		bindings[1].textureView = m_baseColorTextureView;

		bindings[2].binding = 2;
		bindings[2].textureView = m_normalTextureView;

		bindings[3].binding = 3;
		bindings[3].sampler = m_sampler;
		//       ^ This was a 2

		bindings[4].binding = 4;
		bindings[4].buffer = m_lightingUniformBuffer;
		bindings[4].offset = 0;
		bindings[4].size = sizeof(LightingUniforms);
		//       ^ This was a 3

		BindGroupDescriptor bindGroupDesc;
		bindGroupDesc.layout = m_bindGroupLayout;
		bindGroupDesc.entryCount = (uint32_t)bindings.size();
		bindGroupDesc.entries = bindings.data();
		m_bindGroup = m_device.createBindGroup(bindGroupDesc);

		return m_bindGroup != nullptr;
	}

	void Application::terminateBindGroup()
	{
		m_bindGroup.release();
	}

	void Application::updateProjectionMatrix()
	{
		// Update projection matrix
		int width, height;
		SDL_GL_GetDrawableSize(m_window, &width, &height);
		float ratio = width / (float)height;
		m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, ratio, 0.01f, 100.0f);
		m_queue.writeBuffer(
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
		m_queue.writeBuffer(
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
		ImGui_ImplWGPU_Init(m_device, 3, m_swapChainFormat, m_depthTextureFormat);
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