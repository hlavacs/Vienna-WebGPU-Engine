#include "engine/core/GameEngine.h"
#include <chrono>

namespace engine::core {

GameEngine::GameEngine()
    : renderBufferManager(2) // double buffering by default
{}

void GameEngine::AddComponent(std::shared_ptr<GameComponent> comp) {
    gameComponents.push_back(comp);
}

void GameEngine::Stop() {
    running = false;
}

static double GetCurrentTime() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void GameEngine::Run() {
    double previousTime = GetCurrentTime();
    while (running) {
        double currentTime = GetCurrentTime();
        float frameDelta = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;
        accumulatedTime += frameDelta;

        // 1. Process input events
        inputManager.PollEvents();

        // 2. Fixed timestep loop (physics + fixed updates)
        while (accumulatedTime >= fixedDeltaTime) {
            physicsEngine.Step(fixedDeltaTime);
            for (auto& comp : gameComponents) {
                comp->FixedUpdate(fixedDeltaTime);
            }
            accumulatedTime -= fixedDeltaTime;
        }

        // 3. Variable timestep update & late update
        for (auto& comp : gameComponents) {
            comp->Update(frameDelta);
        }
        for (auto& comp : gameComponents) {
            comp->LateUpdate(frameDelta);
        }

        // 4. Extract render state snapshot safely
        RenderState& renderState = renderBufferManager.acquireWriteBuffer();
        // renderState = ExtractRenderState(); // User should implement this
        renderBufferManager.submitWrite();
        // (Optionally sleep or yield here for frame pacing)
    }
}

} // namespace engine::core
