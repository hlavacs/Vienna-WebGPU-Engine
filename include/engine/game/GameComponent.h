#pragma once

namespace engine::game {

class GameComponent {
public:
    virtual ~GameComponent() = default;
    virtual void fixedUpdate(float fixedDeltaTime) = 0;
    virtual void update(float deltaTime) = 0;
    virtual void lateUpdate(float deltaTime) = 0;
};

} // namespace engine::game
