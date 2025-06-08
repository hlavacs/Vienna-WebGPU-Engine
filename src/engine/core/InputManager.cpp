#include "engine/core/InputManager.h"

namespace engine::core {

void InputManager::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            m_keyStates[event.key.keysym.scancode] = (event.type == SDL_KEYDOWN);
        }
        // Handle mouse/buttons as needed
    }
}

bool InputManager::IsKeyPressed(SDL_Scancode key) const {
    auto it = m_keyStates.find(key);
    return it != m_keyStates.end() && it->second;
}

} // namespace engine::core
