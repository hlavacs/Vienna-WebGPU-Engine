#pragma once
#include <SDL.h>
#include <unordered_map>

namespace engine::input {

class InputManager {
public:
    // TODO: Support multiple input backends (SDL, platform-specific, etc.)
    void pollEvents();
    bool isKeyPressed(SDL_Scancode key) const;
    // Add mouse/button state as needed
private:
    std::unordered_map<SDL_Scancode, bool> m_keyStates;
};

} // namespace engine::input
