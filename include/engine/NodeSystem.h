/**
 * NodeSystem.h - Convenience header for custom node implementations
 * 
 * Include this header when creating custom UpdateNode or RenderNode implementations.
 * It provides all common dependencies for node system development.
 */

#pragma once

// Core engine context and systems
#include "engine/EngineContext.h"
#include "engine/input/InputManager.h"

// Scene graph
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"
#include "engine/scene/entity/Node.h"
#include "engine/scene/entity/UpdateNode.h"
#include "engine/scene/entity/RenderNode.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/entity/LightNode.h"
#include "engine/scene/entity/ModelRenderNode.h"

// Common math
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

// SDL for input constants (SDL_SCANCODE_*, SDL_BUTTON_*)
#include <SDL.h>

// Logging
#include <spdlog/spdlog.h>
