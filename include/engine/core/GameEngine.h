#pragma once
#include <vector>
#include <memory>
#include "engine/core/RenderBufferManager.h"
#include "engine/core/InputManager.h"
#include "engine/core/PhysicsEngine.h"
#include "engine/core/GameComponent.h"

namespace engine::core {

class GameEngine {
public:
    GameEngine();
    void Run();
    void AddComponent(std::shared_ptr<GameComponent> comp);
    void Stop();

private:
    RenderBufferManager renderBufferManager;
    InputManager inputManager;
    PhysicsEngine physicsEngine;
    std::vector<std::shared_ptr<GameComponent>> gameComponents;
    bool running = true;
    float fixedDeltaTime = 1.0f / 60.0f;
    float accumulatedTime = 0.0f;
};

} // namespace engine::core
