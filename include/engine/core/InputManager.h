#pragma once
#include <SDL.h>
#include <unordered_map>

namespace engine::core {

class InputManager {
public:
    void PollEvents();
    bool IsKeyPressed(SDL_Scancode key) const;
    // Add mouse/button state as needed
private:
    std::unordered_map<SDL_Scancode, bool> m_keyStates;
};

} // namespace engine::core
