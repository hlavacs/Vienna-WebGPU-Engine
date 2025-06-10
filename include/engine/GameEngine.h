#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include "engine/rendering/RenderBufferManager.h"
#include "engine/input/InputManager.h"
#include "engine/physics/PhysicsEngine.h"
#include "engine/game/GameComponent.h"

namespace engine
{

	struct GameEngineOptions
	{
		float fixedDeltaTime = 1.0f / 60.0f;
		float maxDeltaTime = 1.0f / 15.0f; // Clamp frame delta to prevent spiraling
		float targetFrameRate = 60.0f;	   // Desired framerate (used for vsync or sleeping)
		bool enableVSync = true;		   // If true, rely on GPU vsync
		bool limitFrameRate = false;	   // If true, manually cap frame rate

		int maxSubSteps = 5;	// Max fixed steps per frame to prevent spiral of death
		bool runPhysics = true; // Enable/disable physics updates (for testing)

		int renderBufferCount = 2;	 // 2 = double buffering, 3 = triple buffering
		bool runRenderThread = true; // Enable threaded rendering (async)

		bool showFrameStats = false;	// Print/log delta time, FPS, etc.
		bool logSubsystemErrors = true; // Log issues in update/render/physics
		bool enableHotReload = false;	// Watch files & reload (e.g. shaders/scripts)

		int windowWidth = 1280;
		int windowHeight = 720;
		bool fullscreen = false;
		bool resizableWindow = true;

		bool enableAudio = true;
		float masterVolume = 1.0f;
	};

	class GameEngine
	{
	public:
		GameEngine();
		void run();
		void stop();
		void setOptions(const GameEngineOptions &options);
		void addComponent(std::shared_ptr<engine::game::GameComponent> comp);
		void clearComponents();

	private:
		void gameLoop();
		void physicsLoop();
		void renderLoop();

		engine::rendering::RenderBufferManager renderBufferManager;
		engine::input::InputManager inputManager;
		engine::physics::PhysicsEngine physicsEngine;
		std::vector<std::shared_ptr<engine::game::GameComponent>> gameComponents;
		std::mutex componentMutex;
		std::atomic<bool> running = true;
		GameEngineOptions options;
		float accumulatedTime = 0.0f;
		std::thread physicsThread;
		std::thread renderThread;
	};

} // namespace engine
