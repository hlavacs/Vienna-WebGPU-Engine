// SDL3: we provide our own main() and call SDL_SetMainReady(), so SDL_main must
// not hijack main. Must be defined before <SDL3/SDL_main.h>.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include "engine/GameEngine.h"

#include <spdlog/spdlog.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <chrono>
#include <iostream>
#include <sdl3webgpu.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <vector>

#include "engine/core/PathProvider.h"
#include "engine/scene/NodeTypeRegistry.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/resources/ResourceManager.h"

namespace engine
{

GameEngine::GameEngine() :
	running(false)
{
	// Dual logging: colored console + a flushed "engine.log" in the working dir, so
	// the log survives a crash and can be read directly (no stdout capture needed).
	try
	{
		std::vector<spdlog::sink_ptr> sinks{
			std::make_shared<spdlog::sinks::stdout_color_sink_mt>(),
			std::make_shared<spdlog::sinks::basic_file_sink_mt>("engine.log", true)};
		auto logger = std::make_shared<spdlog::logger>("engine", sinks.begin(), sinks.end());
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(logger);
	}
	catch (const spdlog::spdlog_ex &)
	{
		// Fall back to the default stdout logger if the file can't be opened.
	}

#if defined(__EMSCRIPTEN__)
	// MEMFS root; --preload-file maps resources/ and assets/ under "/" (host
	// paths from ASSETS_ROOT_DIR/DEBUG_ROOT_DIR are meaningless in the browser).
	engine::core::PathProvider::initialize("/", "/");
	// Per-frame trace/debug spam must never reach the browser console or the
	// page's log panel - console.log + DOM appends at frame rate stall the tab.
	spdlog::set_level(spdlog::level::info);
#elif defined(DEBUG_ROOT_DIR) && defined(ASSETS_ROOT_DIR)
	engine::core::PathProvider::initialize(ASSETS_ROOT_DIR, DEBUG_ROOT_DIR);
#elif defined(DEBUG_ROOT_DIR)
	engine::core::PathProvider::initialize("", DEBUG_ROOT_DIR);
#else
	engine::core::PathProvider::initialize();
#endif

	spdlog::info("EXE Root: {}", engine::core::PathProvider::getExecutableRoot().string());
	spdlog::info("LIB Root: {}", engine::core::PathProvider::getLibraryRoot().string());

	m_resourceManager = std::make_shared<engine::resources::ResourceManager>(
		engine::core::PathProvider::getResourceRoot()
	);
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

	spdlog::info("Engine shut down successfully");
	spdlog::shutdown();
}

std::shared_ptr<engine::scene::SceneManager> GameEngine::getSceneManager()
{
	return m_sceneManager;
}

std::shared_ptr<engine::rendering::webgpu::WebGPUContext> GameEngine::getContext()
{
	return m_context;
}

std::shared_ptr<engine::resources::ResourceManager> GameEngine::getResourceManager()
{
	return m_resourceManager;
}

SDL_Window *GameEngine::getWindow()
{
	return m_window;
}

// ToDo: All of these should return weak_ptr or raw ptr to avoid exposing shared ownership of
// subsystems. Refactor later.

std::shared_ptr<engine::ui::ImGuiManager> GameEngine::getImGuiManager()
{
	return m_imguiManager;
}

EngineContext *GameEngine::getEngineContext()
{
	return &m_engineContext;
}

std::weak_ptr<engine::rendering::Renderer> GameEngine::getRenderer()
{
	return m_renderer;
}

engine::input::InputManager *GameEngine::getInputManager()
{
	return &m_inputManager;
}

float GameEngine::getFPS() const
{
	return m_currentFPS;
}

float GameEngine::getFrameTime() const
{
	return m_currentFrameTime;
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
			SDL_SetWindowFullscreen(m_window, options.fullscreen);
		}

		// Update window size (only if not in fullscreen)
		if (windowSizeChanged && !options.fullscreen)
		{
			SDL_SetWindowSize(m_window, options.windowWidth, options.windowHeight);
			// Center window after resize
			SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			onWindowResize(options.windowWidth, options.windowHeight);
		}

		// Update resizable state
		if (resizableChanged)
		{
			SDL_SetWindowResizable(m_window, options.resizableWindow);
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

	// Register built-in node types so scenes can (de)serialize them. Idempotent
	// re-registration is harmless; projects add their custom types after this.
	engine::scene::registerBuiltinNodeTypes();

	// Tell SDL we're handling main ourselves
	SDL_SetMainReady();

	// Create SDL window
	// SDL3 dropped SDL_INIT_TIMER (timers always available) and SDL_INIT_EVENTS
	// (implied). SDL_Init now returns bool (true on success).
	auto sdlFlags = SDL_INIT_VIDEO;
	sdlFlags |= (options.enableAudio) ? SDL_INIT_AUDIO : 0;
	if (!SDL_Init(sdlFlags))
	{
		spdlog::error("Could not initialize SDL3: {}", SDL_GetError());
		return false;
	}

	// SDL3 has no SDL_WINDOW_SHOWN (windows are shown by default).
	SDL_WindowFlags windowFlags = 0;
	if (options.resizableWindow)
		windowFlags |= SDL_WINDOW_RESIZABLE;
	if (options.fullscreen)
		windowFlags |= SDL_WINDOW_FULLSCREEN;

	// SDL3 SDL_CreateWindow takes (title, w, h, flags) — no position arguments;
	// we center the window explicitly after creation.
	m_window = SDL_CreateWindow(
		"Vienna WebGPU Engine",
		options.windowWidth,
		options.windowHeight,
		windowFlags
	);

	if (!m_window)
	{
		spdlog::error("Could not create window!");
		return false;
	}

	m_isFullscreen = options.fullscreen;
	SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

	m_context->initialize(m_window, options.enableVSync, options.overrideDeviceLimits);
	options.appliedDeviceLimits = m_context->limitsConfig();

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
	{
		m_imguiManager->shutdown();
		m_imguiManager.reset();
	}

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
	// Note: Don't call spdlog::shutdown() - it can crash if internal state is already destroyed.
	// spdlog will clean itself up automatically during static destruction.
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
#if defined(__EMSCRIPTEN__)
	if (options.runPhysics)
		spdlog::warn("Physics thread unavailable in the wasm build (no pthreads); physics is disabled.");
#else
	if (options.runPhysics)
		physicsThread = std::thread(&GameEngine::physicsLoop, this);
#endif

	// Main/game logic loop (runs on main thread)
	gameLoop();

	// Clean shutdown
	stop();
	cleanup();
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

void GameEngine::gameLoop()
{
	m_loopPreviousTime = getCurrentTime();
	onWindowResize(options.windowWidth, options.windowHeight);
#if defined(__EMSCRIPTEN__)
	// The browser owns the loop: one engine frame per requestAnimationFrame tick.
	// A while() here never yields and freezes the tab. This call unwinds gameLoop.
	emscripten_set_main_loop_arg(
		[](void *arg)
		{
			auto *self = static_cast<GameEngine *>(arg);
			if (!self->running)
			{
				emscripten_cancel_main_loop();
				return;
			}
			self->frameTick();
		},
		this, 0, true);
#else
	while (running)
		frameTick();
#endif
}

void GameEngine::frameTick()
{
	processEvents();

	const double currentTime = getCurrentTime();
	float frameDelta = static_cast<float>(currentTime - m_loopPreviousTime);
	m_loopPreviousTime = currentTime;

	if (frameDelta > options.maxDeltaTime)
		frameDelta = options.maxDeltaTime;

	updateScene(frameDelta);
	renderFrame(frameDelta);

	m_inputManager.endFrame();
	updateFrameStats(frameDelta);
#ifndef __EMSCRIPTEN__
	// rAF paces the browser; SDL_Delay-based capping would asyncify-sleep mid-tick.
	limitFrameRate(currentTime);
#endif
}

void GameEngine::processEvents()
{
	// Poll mouse state once per frame before processing SDL events
	m_inputManager.startFrame();

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (m_imguiManager)
			ImGui_ImplSDL3_ProcessEvent(&event);

		// Forward input to the game/editor InputManager unless ImGui is capturing
		// TEXT input (an active text field). Gating on WantCaptureKeyboard/Mouse was
		// too aggressive: hovering or focusing any ImGui window (e.g. an editor's
		// render-to-texture viewport panel) set those flags and starved viewport
		// navigation (WASD / right-mouse look) of input. WantTextInput is true only
		// while editing a text field, so keystrokes still never leak while typing.
		ImGuiIO &io = ImGui::GetIO();
		if (!io.WantTextInput)
			m_inputManager.processEvent(event);

		if (event.type == SDL_EVENT_QUIT)
		{
			running = false;
		}
		else if (event.type == SDL_EVENT_WINDOW_RESIZED)
		{
			onWindowResize(event.window.data1, event.window.data2);
		}
		else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F11)
		{
			toggleFullscreen();
		}
	}
}

void GameEngine::toggleFullscreen()
{
	if (!m_window)
		return;

	m_isFullscreen = !m_isFullscreen;
	if (m_isFullscreen)
	{
		// Pin a real display mode (non-null = exclusive fullscreen in SDL3, as
		// opposed to borderless desktop) so the present path can bypass the
		// windowed compositor and a vsync-off Immediate swapchain can run
		// uncapped. The follow-up SDL resize event reconfigures the surface.
		SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
		if (const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(display))
			SDL_SetWindowFullscreenMode(m_window, mode);
		SDL_SetWindowFullscreen(m_window, true);
	}
	else
	{
		SDL_SetWindowFullscreen(m_window, false);
	}
	spdlog::info("F11: exclusive fullscreen {}", m_isFullscreen ? "ON" : "OFF");
}

void GameEngine::onWindowResize(int width, int height)
{
	// Zero-size events (minimized window; emscripten canvas before CSS layout) must
	// not reconfigure the surface or render targets - WebGPU forbids empty textures.
	if (width <= 0 || height <= 0)
	{
		spdlog::info("Ignoring zero-size window resize event ({}x{})", width, height);
		return;
	}

	m_currentWidth = width;
	m_currentHeight = height;
	m_context->surfaceManager().updateIfNeeded(width, height);

	if (m_renderer)
		m_renderer->onResize(width, height);

	auto scene = m_sceneManager->getActiveScene();
	if (!scene)
		return;

	auto cameras = scene->getActiveCameras();
	if (!cameras.empty())
	{
		for (auto &camera : cameras)
		{
			camera->onResize(width, height);
		}
	}
}

void GameEngine::updateScene(float deltaTime)
{
	auto scene = m_sceneManager->getActiveScene();
	if (!scene || !scene->isLoaded())
		return;

	scene->update(deltaTime);
	scene->lateUpdate(deltaTime);
}

void GameEngine::renderFrame(float /* deltaTime*/)
{
	auto scene = m_sceneManager->getActiveScene();
	if (!scene || !m_renderer)
		return;
	if (scene != m_lastRenderedScene)
	{
		onWindowResize(m_currentWidth, m_currentHeight);
		m_lastRenderedScene = scene;
	}

	scene->preRender();

	// An empty camera set is NOT an early-out: the renderer must still run its
	// composite + UI pass and present, otherwise the surface acquired in
	// startFrame() is never presented and the next acquireNextTexture()
	// deadlocks - which froze the editor whenever the active camera was disabled.
	auto cameras = scene->getActiveCameras();

	// Sort cameras by depth (lower depth renders first)
	std::sort(cameras.begin(), cameras.end(), [](const auto &a, const auto &b)
			  { return a->getDepth() < b->getDepth(); });

	engine::rendering::RenderCollector renderCollector;
	// Collect render data directly from scene graph
	scene->collectRenderData(renderCollector);

	// Sort with camera position for proper transparent object ordering
	glm::vec3 cameraPosition = cameras.empty() ? glm::vec3(0.0f) : cameras[0]->getPosition();
	renderCollector.sort(cameraPosition);

	scene->collectDebugData();
	auto debugCollector = scene->getDebugCollector();

	float time = static_cast<float>(SDL_GetTicks()) * 0.001f;

	std::vector<engine::rendering::RenderTarget> renderTargets;
	renderTargets.reserve(cameras.size());
	// Extract RenderTarget from each camera
	for (auto &camera : cameras)
	{
		engine::rendering::RenderTarget target{};
		target.cameraId = camera->getId();
		target.viewMatrix = camera->getViewMatrix();
		target.projectionMatrix = camera->getProjectionMatrix();
		target.viewProjectionMatrix = camera->getViewProjectionMatrix();
		target.cameraPosition = camera->getPosition();
		target.nearPlane = camera->getNear();
		target.farPlane = camera->getFar();
		target.depth = camera->getDepth();
		target.frustum = camera->getFrustum();
		target.msaa = camera->isMSAAEnabled() ? options.msaaSampleCount : 1; // ToDo: Allow per-camera MSAA settings
		target.viewport = camera->getViewport();
		target.clearFlags = camera->getClearFlags();
		target.backgroundColor = camera->getBackgroundColor();
		target.cpuTarget = camera->getRenderTarget();
		target.hdr = camera->isHDREnabled();
		target.environmentTexture = camera->getEnvironmentTexture();
		target.skyboxEnabled = camera->isSkyboxEnabled();
		target.irradianceEnabled = camera->isIrradianceEnabled();
		target.irradianceIntensity = camera->getIrradianceIntensity();
		target.offscreenOnly = camera->isOffscreenOnly();
		target.renderSize = camera->getRenderSize();
		target.gpuTexture = nullptr; // Will be set by renderer

		renderTargets.push_back(target);
	}

	std::sort(
		renderTargets.begin(),
		renderTargets.end(),
		[](const engine::rendering::RenderTarget &a, const engine::rendering::RenderTarget &b)
		{
			// ToDo: Implement proper alpha blending sorting based on camera distance for transparent objects.
			// Proboply need to separate opaque and transparent render targets and sort transparents by distance from camera.
			return a.depth < b.depth;
		}
	);

	// Single call to renderer with frame cache
	auto uiCallback = createUICallback();
	m_renderer->renderFrame(renderTargets, renderCollector, debugCollector, time, scene->getCustomBindGroupProviders(), uiCallback);

	scene->postRender();
}

std::function<void(wgpu::RenderPassEncoder)> GameEngine::createUICallback()
{
	if (!m_imguiManager)
		return nullptr;

	return [this](wgpu::RenderPassEncoder pass)
	{
		m_imguiManager->render(pass);
	};
}

void GameEngine::updateFrameStats(float frameDelta)
{
	static int frameCount = 0;
	static double fpsTimer = 0.0;

	frameCount++;
	fpsTimer += frameDelta;
	m_currentFrameTime = frameDelta * 1000.0f;

	if (fpsTimer >= 1.0)
	{
		m_currentFPS = frameCount / static_cast<float>(fpsTimer);

		if (options.showFrameStats)
		{
			spdlog::info(
				"FPS: {} | Frame Time: {:.2f}ms",
				static_cast<int>(m_currentFPS),
				m_currentFrameTime
			);
		}

		frameCount = 0;
		fpsTimer = 0.0;
	}
}

void GameEngine::limitFrameRate(double frameStartTime)
{
	if (!options.limitFrameRate || options.enableVSync)
		return;

	const double targetFrameTime = 1.0 / options.targetFrameRate;
	const double frameTime = getCurrentTime() - frameStartTime;

	if (frameTime < targetFrameTime)
	{
		SDL_Delay(static_cast<Uint32>((targetFrameTime - frameTime) * 1000.0));
	}
}

} // namespace engine
