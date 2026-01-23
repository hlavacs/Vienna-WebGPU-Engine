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
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"
#include "engine/scene/nodes/Node.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/UpdateNode.h"

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
