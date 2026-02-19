#pragma once

#include <SDL.h>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "engine/EngineContext.h"
#include "engine/input/InputManager.h"
#include "engine/physics/PhysicsEngine.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/scene/SceneManager.h"
#include "engine/ui/ImGuiManager.h"

#include "engine/rendering/webgpu/DeviceLimitsConfig.h"

namespace engine
{

struct GameEngineOptions
{
	float fixedDeltaTime = 1.0f / 60.0f; //< Fixed timestep for physics updates (in seconds)
	float maxDeltaTime = 1.0f / 15.0f;	 //< Clamp frame delta to prevent spiraling
	float targetFrameRate = 60.0f;		 //< Desired framerate (used for vsync or sleeping)
	bool enableVSync = true;			 //< If true, rely on GPU vsync
	bool limitFrameRate = false;		 //< If true, manually cap frame rate

	int maxSubSteps = 5;	//< Max fixed steps per frame to prevent spiral of death
	bool runPhysics = true; //< Enable/disable physics updates (for testing)

	bool showFrameStats = false;	//< Print/log delta time, FPS, etc.
	bool logSubsystemErrors = true; //< Log issues in update/render/physics
	bool enableHotReload = false;	//< Watch files & reload (e.g. shaders/scripts)
	int windowWidth = 1280;			//< Initial window width
	int windowHeight = 720;			//< Initial window height
	bool fullscreen = false;		//< Start in fullscreen mode
	bool resizableWindow = true;	//< Allow window resizing
	bool enableAudio = true;		//< Enable audio subsystem (not implemented yet)
	float masterVolume = 1.0f;		//< Master volume (0.0 = silent, 1.0 = full volume)
	int msaaSampleCount = 4;		//< Number of MSAA samples (1 = no MSAA)

	std::optional<engine::rendering::webgpu::DeviceLimitsConfig> overrideDeviceLimits; //< Optional override for WebGPU device limits (for testing or compatibility)

	std::optional<engine::rendering::webgpu::DeviceLimitsConfig> getDeviceLimits() const { return appliedDeviceLimits; }

	friend class GameEngine;

  private:
	std::optional<engine::rendering::webgpu::DeviceLimitsConfig> appliedDeviceLimits; //< The actual device limits applied after initialization (for reference)
};

class GameEngine
{
  public:
	GameEngine();
	~GameEngine();

	// Setup API - call before run()
	// Can also be called at runtime to update options (VSync, window size, etc.)
	void setOptions(const GameEngineOptions &options);

	// Initialize the engine (creates window, WebGPU context, renderer, ImGui)
	// Call this before run() if you need to access ImGuiManager or other subsystems
	// @param opts Optional engine options. If not provided, uses previously set options via setOptions()
	bool initialize(std::optional<GameEngineOptions> opts = std::nullopt);

	// Access the scene manager to create and load scenes
	std::shared_ptr<engine::scene::SceneManager> getSceneManager();

	// Access the WebGPU context for advanced setup
	std::shared_ptr<engine::rendering::webgpu::WebGPUContext> getContext();

	// Access the resource manager for loading assets
	std::shared_ptr<engine::resources::ResourceManager> getResourceManager();

	// Access the window for UI initialization
	SDL_Window *getWindow();

	// Access the ImGui manager for UI setup (available after initialize() is called)
	std::shared_ptr<engine::ui::ImGuiManager> getImGuiManager();

	// Access the engine context for nodes and subsystems
	EngineContext *getEngineContext();

	std::weak_ptr<engine::rendering::Renderer> getRenderer();

	// Access the input manager
	input::InputManager *getInputManager();

	// Get current FPS
	float getFPS() const;

	// Get current frame time in milliseconds
	float getFrameTime() const;

	// Start the game engine (blocks until stopped or window closed)
	// Automatically calls initialize() if not already called
	void run();

	// Stop the engine (can be called from any thread)
	void stop();

  private:
	void cleanup();

	void physicsLoop();

	void gameLoop();
	void processEvents();
	void onWindowResize(int width, int height);
	void updateScene(float deltaTime);
	void renderFrame(float deltaTime);
	void updateFrameStats(float frameDelta);
	void limitFrameRate(double frameStartTime);

	std::function<void(wgpu::RenderPassEncoder)> createUICallback();

  private:
	// Core subsystems
	SDL_Window *m_window = nullptr;
	std::shared_ptr<engine::rendering::webgpu::WebGPUContext> m_context;
	std::shared_ptr<engine::resources::ResourceManager> m_resourceManager;
	std::shared_ptr<engine::scene::SceneManager> m_sceneManager;
	std::shared_ptr<engine::rendering::Renderer> m_renderer;
	std::shared_ptr<engine::ui::ImGuiManager> m_imguiManager;

	// Per-camera render collectors (cached across frames for bind group reuse)
	std::unordered_map<uint64_t, engine::rendering::RenderCollector> m_cameraCollectors;

	engine::input::InputManager m_inputManager;
	engine::physics::PhysicsEngine m_physicsEngine;

	// Context for node system access
	EngineContext m_engineContext;

	// Window size tracking
	int m_currentWidth = 1280;
	int m_currentHeight = 720;

	// Frame statistics
	float m_currentFPS = 0.0f;
	float m_currentFrameTime = 0.0f;

	// Threading
	std::atomic<bool> running = false;
	std::thread physicsThread;

	// Configuration
	GameEngineOptions options;
	float accumulatedTime = 0.0f;
	bool m_initialized = false;
};

} // namespace engine
