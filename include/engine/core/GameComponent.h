#pragma once

namespace engine::core {

class GameComponent {
public:
    virtual ~GameComponent() = default;
    virtual void FixedUpdate(float fixedDeltaTime) = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void LateUpdate(float deltaTime) = 0;
};

} // namespace engine::core
