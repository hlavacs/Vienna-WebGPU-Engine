#include "engine/GameEngine.h"
#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include <chrono>
#include <iostream>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <sdl2webgpu.h>
#include <spdlog/spdlog.h>
#include <backends/imgui_impl_sdl2.h>

namespace engine
{

GameEngine::GameEngine() :
	running(false)
{
#ifdef DEBUG_ROOT_DIR
	engine::core::PathProvider::initialize(DEBUG_ROOT_DIR, ASSETS_ROOT_DIR);
#else
	engine::core::PathProvider::initialize();
#endif

	spdlog::info("EXE Root: {}", engine::core::PathProvider::getExecutableRoot().string());
	spdlog::info("LIB Root: {}", engine::core::PathProvider::getLibraryRoot().string());

	m_resourceManager = std::make_shared<engine::resources::ResourceManager>(
		engine::core::PathProvider::getResourceRoot());
	m_context = std::make_shared<engine::rendering::webgpu::WebGPUContext>();
	m_sceneManager = std::make_shared<engine::scene::SceneManager>();
	
	// Setup engine context for node system access
	m_engineContext.setInputManager(&m_inputManager);
	m_engineContext.setWebGPUContext(m_context.get());
	m_engineContext.setResourceManager(m_resourceManager.get());
	m_engineContext.setSceneManager(m_sceneManager.get());
	
	// Give scene manager access to engine context
	m_sceneManager->setEngineContext(&m_engineContext);
}

GameEngine::~GameEngine()
{
	stop();
	cleanup();
}

void GameEngine::setOptions(const GameEngineOptions &opts)
{
	// Store previous states for comparison
	bool vsyncChanged = (options.enableVSync != opts.enableVSync);
	bool windowSizeChanged = (options.windowWidth != opts.windowWidth || options.windowHeight != opts.windowHeight);
	bool resizableChanged = (options.resizableWindow != opts.resizableWindow);
	bool fullscreenChanged = (options.fullscreen != opts.fullscreen);
	
	// Update options
	options = opts;
	
	// Handle runtime changes if engine is already initialized
	if (m_window)
	{
		// Update fullscreen mode
		if (fullscreenChanged)
		{
			SDL_SetWindowFullscreen(m_window, options.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
		}
		
		// Update window size (only if not in fullscreen)
		if (windowSizeChanged && !options.fullscreen)
		{
			SDL_SetWindowSize(m_window, options.windowWidth, options.windowHeight);
			// Center window after resize
			SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			
			// Update internal tracking
			m_currentWidth = options.windowWidth;
			m_currentHeight = options.windowHeight;
			
			// Notify subsystems of resize
			if (m_context)
			{
				m_context->surfaceManager().updateIfNeeded(m_currentWidth, m_currentHeight);
			}
			if (m_renderer)
			{
				m_renderer->onResize(m_currentWidth, m_currentHeight);
			}
			
			// Update camera aspect ratio
			auto activeScene = m_sceneManager->getActiveScene();
			if (activeScene)
			{
				auto cam = activeScene->getActiveCamera();
				if (cam)
				{
					cam->setAspect(static_cast<float>(m_currentWidth) / static_cast<float>(m_currentHeight));
				}
			}
		}
		
		// Update resizable state
		if (resizableChanged)
		{
			SDL_SetWindowResizable(m_window, options.resizableWindow ? SDL_TRUE : SDL_FALSE);
		}
	}
	
	// If VSync changed and context is initialized, reconfigure it
	if (vsyncChanged && m_context)
	{
		m_context->updatePresentMode(options.enableVSync);
	}
}

void GameEngine::stop()
{
	running = false;
	if (physicsThread.joinable())
		physicsThread.join();
}

bool GameEngine::initialize(std::optional<GameEngineOptions> opts)
{
	// Use provided options or keep existing ones
	if (opts.has_value())
	{
		options = opts.value();
	}
	
	// Tell SDL we're handling main ourselves
	SDL_SetMainReady();
	
	// Create SDL window
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0)
	{
		spdlog::error("Could not initialize SDL2: {}", SDL_GetError());
		return false;
	}

	Uint32 windowFlags = SDL_WINDOW_SHOWN;
	if (options.resizableWindow)
		windowFlags |= SDL_WINDOW_RESIZABLE;
	if (options.fullscreen)
		windowFlags |= SDL_WINDOW_FULLSCREEN;

	m_window = SDL_CreateWindow(
		"Vienna WebGPU Engine",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		options.windowWidth,
		options.windowHeight,
		windowFlags);

	if (!m_window)
	{
		spdlog::error("Could not create window!");
		return false;
	}

	// Initialize WebGPU context
	m_context->initialize(m_window, options.enableVSync);

	// Create renderer
	m_renderer = std::make_shared<engine::rendering::Renderer>(m_context);
	if (!m_renderer->initialize())
	{
		spdlog::error("Failed to initialize renderer!");
		return false;
	}

	// Create ImGui manager
	m_imguiManager = std::make_shared<engine::ui::ImGuiManager>();
	if (!m_imguiManager->initialize(m_window, m_context))
	{
		spdlog::error("Failed to initialize ImGuiManager!");
		return false;
	}
	
	m_initialized = true;
	return true;
}

void GameEngine::cleanup()
{
	if (m_imguiManager)
		m_imguiManager.reset();

	if (m_renderer)
		m_renderer.reset();

	if (m_context)
		m_context.reset();

	if (m_window)
	{
		SDL_DestroyWindow(m_window);
		m_window = nullptr;
	}

	SDL_Quit();
}

static double getCurrentTime()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void GameEngine::run()
{
	// Initialize window, WebGPU, renderer if not already done
	if (!m_initialized)
	{
		if (!initialize())
		{
			spdlog::error("Failed to initialize GameEngine!");
			return;
		}
	}

	running = true;

	// Launch physics thread if enabled
	if (options.runPhysics)
		physicsThread = std::thread(&GameEngine::physicsLoop, this);

	// Main/game logic loop (runs on main thread)
	gameLoop();

	// Clean shutdown
	stop();
	cleanup();
}

void GameEngine::gameLoop()
{
	double previousTime = getCurrentTime();

	while (running)
	{
		// Handle SDL events (window close, input, etc.)
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			// Pass events to ImGui first
			if (m_imguiManager)
			{
				ImGui_ImplSDL2_ProcessEvent(&event);
			}

			// Check if ImGui wants to capture input
			ImGuiIO &io = ImGui::GetIO();
			bool imguiWantsInput = io.WantCaptureMouse || io.WantCaptureKeyboard;

			// Process input events only if ImGui doesn't want them
			if (!imguiWantsInput)
			{
				m_inputManager.processEvent(event);
			}

			// Handle window events (always process these)
			if (event.type == SDL_QUIT)
			{
				running = false;
			}
			else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED)
			{
				m_currentWidth = event.window.data1;
				m_currentHeight = event.window.data2;
				
				m_context->surfaceManager().updateIfNeeded(m_currentWidth, m_currentHeight);
				// Notify renderer of resize
				if (m_renderer)
				{
					m_renderer->onResize(m_currentWidth, m_currentHeight);
				}
				
				// Update camera aspect ratio if needed
				auto activeScene = m_sceneManager->getActiveScene();
				if (activeScene)
				{
					auto cam = activeScene->getActiveCamera();
					if (cam)
					{
						cam->setAspect(static_cast<float>(m_currentWidth) / static_cast<float>(m_currentHeight));
					}
				}
			}
		}

		double currentTime = getCurrentTime();
		float frameDelta = static_cast<float>(currentTime - previousTime);
		previousTime = currentTime;

		// Clamp delta time to prevent spiral of death
		if (frameDelta > options.maxDeltaTime)
			frameDelta = options.maxDeltaTime;

		accumulatedTime += frameDelta;

		// Get active scene from scene manager
		auto activeScene = m_sceneManager->getActiveScene();
		if (activeScene)
		{
			activeScene->onFrame(frameDelta);

			// Update camera uniforms and render
			auto cam = activeScene->getActiveCamera();
			if (cam && m_renderer)
			{
				// Update frame uniforms from active camera
				engine::rendering::FrameUniforms frameUniforms;
				frameUniforms.viewMatrix = cam->getViewMatrix();
				frameUniforms.projectionMatrix = cam->getProjectionMatrix();
				frameUniforms.viewProjectionMatrix = cam->getProjectionMatrix() * cam->getViewMatrix();
				frameUniforms.cameraWorldPosition = cam->getPosition();
				frameUniforms.time = static_cast<float>(SDL_GetTicks64() / 1000.0);

				// Render the frame using the scene's render collector
				m_renderer->updateFrameUniforms(frameUniforms);
				
				// Collect debug data from the scene
				activeScene->collectDebugData();
				
				// Create UI render callback that delegates to ImGuiManager
				std::function<void(wgpu::RenderPassEncoder)> uiRenderCallback = nullptr;
				if (m_imguiManager)
				{
					uiRenderCallback = [this](wgpu::RenderPassEncoder renderPass) {
						m_imguiManager->render(renderPass);
					};
				}
				
				m_renderer->renderFrame(
					activeScene->getRenderCollector(),
					&activeScene->getDebugCollector(),
					uiRenderCallback
				);
			}

			// Post-render cleanup
			activeScene->postRender();
		}
		
		// Reset per-frame input state
		m_inputManager.endFrame();

		// Update frame statistics
		static int frameCount = 0;
		static double fpsTimer = 0.0;
		frameCount++;
		fpsTimer += frameDelta;
		m_currentFrameTime = frameDelta * 1000.0f; // Convert to milliseconds
		
		if (fpsTimer >= 1.0)
		{
			m_currentFPS = frameCount / static_cast<float>(fpsTimer);
			
			// Log frame stats if enabled
			if (options.showFrameStats)
			{
				spdlog::info("FPS: {} | Frame Time: {:.2f}ms", static_cast<int>(m_currentFPS), m_currentFrameTime);
			}
			
			frameCount = 0;
			fpsTimer = 0.0;
		}

		// Frame rate limiting (if enabled)
		if (options.limitFrameRate && !options.enableVSync)
		{
			double targetFrameTime = 1.0 / options.targetFrameRate;
			double frameTime = getCurrentTime() - currentTime;
			if (frameTime < targetFrameTime)
			{
				SDL_Delay(static_cast<Uint32>((targetFrameTime - frameTime) * 1000.0));
			}
		}
	}
}

void GameEngine::physicsLoop()
{
	double previousTime = getCurrentTime();
	float localAccum = 0.0f;
	while (running)
	{
		double currentTime = getCurrentTime();
		float frameDelta = static_cast<float>(currentTime - previousTime);
		previousTime = currentTime;
		localAccum += frameDelta;
		
		int subSteps = 0;
		while (localAccum >= options.fixedDeltaTime && subSteps < options.maxSubSteps)
		{
			// Step physics engine
			m_physicsEngine.step(options.fixedDeltaTime);

			// Fixed update for active scene
			auto activeScene = m_sceneManager->getActiveScene();
			if (activeScene)
			{
				// TODO: Add fixedUpdate() method to Scene if needed for physics
				// activeScene->fixedUpdate(options.fixedDeltaTime);
			}

			localAccum -= options.fixedDeltaTime;
			subSteps++;
		}

		// Sleep briefly to avoid busy-waiting
		SDL_Delay(1);
	}
}

} // namespace engine

