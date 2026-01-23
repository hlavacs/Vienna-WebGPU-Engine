/**
 * EngineMain.h - Convenience header for main.cpp entry points
 *
 * Include this header at the top of your main.cpp file.
 * It handles SDL_MAIN_HANDLED and provides all common dependencies for engine initialization.
 *
 * Usage:
 *   #include "engine/EngineMain.h"
 *
 *   int main(int argc, char** argv) {
 *       // Your engine initialization code
 *   }
 */

#pragma once

// CRITICAL: Define SDL_MAIN_HANDLED before any SDL includes
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

// Core engine
#include "engine/EngineContext.h"
#include "engine/GameEngine.h"

// Scene management
#include "engine/scene/Scene.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"
#include "engine/scene/nodes/UpdateNode.h"

// Resource management
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/Texture.h"
#include "engine/resources/ResourceManager.h"

// WebGPU resources
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"

// Common math
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

// UI
#include <imgui.h>

// Logging
#include <spdlog/spdlog.h>

// Standard library
#include <map>
#include <memory>
#include <string>
#include <vector>

// Platform-specific
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif
