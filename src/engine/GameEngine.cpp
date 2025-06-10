#include "engine/GameEngine.h"
#include <chrono>

namespace engine
{

	GameEngine::GameEngine()
		: renderBufferManager(2), running(true) {}

	void GameEngine::setOptions(const GameEngineOptions &opts)
	{
		options = opts;
	}

	void GameEngine::addComponent(std::shared_ptr<engine::game::GameComponent> comp)
	{
		std::lock_guard<std::mutex> lock(componentMutex);
		gameComponents.push_back(comp);
	}

	void GameEngine::clearComponents()
	{
		std::lock_guard<std::mutex> lock(componentMutex);
		gameComponents.clear();
	}

	void GameEngine::stop()
	{
		running = false;
		if (physicsThread.joinable())
			physicsThread.join();
		if (renderThread.joinable())
			renderThread.join();
	}

	static double getCurrentTime()
	{
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}

	void GameEngine::run()
	{
		running = true;
		// Launch physics and render threads
		physicsThread = std::thread(&GameEngine::physicsLoop, this);
		renderThread = std::thread(&GameEngine::renderLoop, this);
		// Main/game logic loop
		gameLoop();
		stop();
	}

	void GameEngine::gameLoop()
	{
		double previousTime = getCurrentTime();
		while (running)
		{
			double currentTime = getCurrentTime();
			float frameDelta = static_cast<float>(currentTime - previousTime);
			previousTime = currentTime;
			accumulatedTime += frameDelta;
			// inputManager.pollEvents();
			// Variable timestep update & late update
			{
				std::lock_guard<std::mutex> lock(componentMutex);
				for (auto &comp : gameComponents)
					comp->update(frameDelta);
				for (auto &comp : gameComponents)
					comp->lateUpdate(frameDelta);
			}
			// (Optionally sleep/yield for pacing)
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
			while (localAccum >= options.fixedDeltaTime)
			{
				physicsEngine.step(options.fixedDeltaTime);
				{
					std::lock_guard<std::mutex> lock(componentMutex);
					for (auto &comp : gameComponents)
						comp->fixedUpdate(options.fixedDeltaTime);
				}
				localAccum -= options.fixedDeltaTime;
			}
			// TODO: (Optionally sleep/yield for pacing)
		}
	}

	void GameEngine::renderLoop()
	{
		while (running)
		{
			// Acquire the latest ready render state for rendering
			const auto &renderState = renderBufferManager.acquireReadBuffer();
			// TODO: Submit renderState to the renderer here
			// renderer.render(renderState);
			renderBufferManager.releaseReadBuffer();
			// (Optionally sleep/yield for pacing)
		}
	}

} // namespace engine
