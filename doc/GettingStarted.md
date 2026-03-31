# Getting Started with Vienna-WebGPU-Engine

This guide will help you create your first application using the Vienna-WebGPU-Engine.

## Table of Contents

1. [Creating Your First Application](#creating-your-first-application)
2. [Project Structure and Asset Paths](#project-structure-and-asset-paths)
3. [Loading and Rendering a Model](#loading-and-rendering-a-model)
4. [Working with the Scene](#working-with-the-scene)
5. [Adding Lights](#adding-lights)
6. [Camera Setup](#camera-setup)
7. [Input Handling](#input-handling)
8. [ImGui Integration](#imgui-integration)
9. [Next Steps](#next-steps)

---

## Creating Your First Application

### Basic Application Structure

Applications use the `GameEngine` directly and create scenes via `SceneManager`:

```cpp
#include "engine/EngineMain.h"
// ^ Must be first to define SDL_MAIN_HANDLED ^

#include <engine/GameEngine.h>

int main(int argc, char** argv)
{
    // Configure engine options
    engine::GameEngineOptions options;
    options.windowWidth = 1280;
    options.windowHeight = 720;
    options.enableVSync = false;
    
    // Initialize engine
    engine::GameEngine engine;
    engine.initialize(options);
    
    // Get managers
    auto sceneManager = engine.getSceneManager();
    auto resourceManager = engine.getResourceManager();
    
    // Create and setup scene
    auto mainScene = sceneManager->createScene("Main");
    auto rootNode = mainScene->getRoot();
    
    // Setup camera
    auto camera = mainScene->getMainCamera();
    camera->getTransform().setLocalPosition(glm::vec3(0, 2, 5));
    camera->getTransform().lookAt(glm::vec3(0, 0, 0));
    
    // Load scene and run
    sceneManager->loadScene("Main");
    engine.run();
    
    return 0;
}
```

### Application Lifecycle

1. **Initialize** - Configure options and initialize engine
2. **Setup** - Create scenes, load resources, setup nodes
3. **Load** - Load the active scene
4. **Run** - Main game loop (engine handles update/render)
5. **Cleanup** - Automatic cleanup on exit

---

## Project Structure and Asset Paths

### ⚠️ Important: Debug vs Release Paths

**Path behavior differs between Debug and Release builds:**

- **Debug builds**: `PathProvider` uses the **project root directory** as `basePath`
  - Example: `E:\Projects\MyGame\` (where CMakeLists.txt is)
  - Resources are accessed directly from source directories

- **Release builds**: `PathProvider` uses the **executable directory** as `basePath`
  - Example: `E:\Projects\MyGame\build\Release\`
  - Resources will be copied to the build output directory if named `assets` folder exist

**Recommendation:** Always use `PathProvider` to abstract path differences between build configurations.

### Directory Layout

**Your Application Structure:**
```
YourApp/
├── assets/                  # Application-specific assets
│   ├── models/              # Your 3D models
│   ├── textures/            # Your textures
│   ├── shaders/             # Your custom shaders
│   ├── materials/           # Material definitions
│   └── audio/               # Audio files
├── configs/                 # Configuration files
└── logs/                    # Log output
```

**Engine Structure:**
```
Vienna-WebGPU-Engine/
└── resources/               # Engine resources (shaders, default assets)
    ├── PBR_Lit_Shader.wgsl
    ├── shadow2d.wgsl
    ├── debug.wgsl
    └── ...
```

### Loading Resources - Best Practices

**Always use `PathProvider` to avoid hardcoded paths:**
```cpp
auto resourceManager = engine.getResourceManager();

// ✅ RECOMMENDED: Use PathProvider for application assets
auto modelPath = PathProvider::resolve("models") / "my_model.obj";
auto maybeModel = resourceManager->m_modelManager->createModel(modelPath.string());

// ✅ RECOMMENDED: Use PathProvider for engine resources
auto engineTexture = PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg");
auto texture = resourceManager->m_textureManager->createTextureFromFile(engineTexture);

// ⚠️ AVOID: Hardcoded paths won't work across Debug/Release
auto maybeModel2 = resourceManager->m_modelManager->createModel("C:/Models/my_model.obj");

// ⚠️ AVOID: Relative paths depend on working directory
auto maybeModel3 = resourceManager->m_modelManager->createModel("my_model.obj");

if (!maybeModel.has_value()) {
    spdlog::error("Failed to load model");
    return -1;
}
```

### Using PathProvider

`PathProvider` resolves paths based on two roots:

1. **Base Path** (`basePath`) - Application root directory
   - **Debug**: Project root (where CMakeLists.txt is)
   - **Release**: Executable directory
   
2. **Library Root** (`libraryRoot`) - Engine installation directory
   - Always points to Vienna-WebGPU-Engine root

**⚠️ Critical: Default behavior uses `basePath` for resources**

```cpp
// By default, resources/ is resolved relative to basePath (NOT libraryRoot)
// This means in Debug: project_root/resources/
//           in Release: executable_dir/resources/
```

**Access application assets (recommended):**
```cpp
// Load from assets/models/ (relative to basePath)
auto modelPath = PathProvider::resolve("models") / "my_model.obj";
auto model = resourceManager->m_modelManager->createModel(modelPath.string());

// Load from assets/textures/ (relative to basePath)
auto texturePath = PathProvider::resolve("textures") / "albedo.png";
auto texture = resourceManager->m_textureManager->createTextureFromFile(texturePath.string());

// Shorthand for common paths (all relative to basePath)
auto modelsDir = PathProvider::resolve("models");    // -> {basePath}/assets/models/
auto texturesDir = PathProvider::resolve("textures"); // -> {basePath}/assets/textures/
auto shadersDir = PathProvider::resolve("shaders");   // -> {basePath}/assets/shaders/
```

**Function | Resolves To | Debug | Release | Use Case |
|----------|-------------|-------|---------|----------|
| `resolve("models")` | `{basePath}/assets/models/` | `ProjectRoot/assets/models/` | `ExeDir/assets/models/` | Your 3D models |
| `resolve("textures")` | `{basePath}/assets/textures/` | `ProjectRoot/assets/textures/` | `ExeDir/assets/textures/` | Your textures |
| `resolve("shaders")` | `{basePath}/assets/shaders/` | `ProjectRoot/assets/shaders/` | `ExeDir/assets/shaders/` | Your custom shaders |
| `resolve("materials")` | `{basePath}/assets/materials/` | `ProjectRoot/assets/materials/` | `ExeDir/assets/materials/` | Material files |
| `resolve("scenes")` | `{basePath}/assets/scenes/` | `ProjectRoot/assets/scenes/` | `ExeDir/assets/scenes/` | Scene files |
| `resolve("configs")` | `{basePath}/configs/` | `ProjectRoot/configs/` | `ExeDir/configs/` | Config files |
| `resolve("logs")` | `{basePath}/logs/` | `ProjectRoot/logs/` | `ExeDir/logs/` | Log output |
| `getResource(file)` | `{libraryRoot}/resources/{file}` | `EngineRoot/resources/{file}` | `EngineRoot/resources/{file}` | Engine resources |

**Important Notes:**
- `basePath` changes between Debug/Release builds
- `libraryRoot` always points to engine directory
- Always use `getResource()` for engine resources, not `resolve("resources")`

**Key Distinction:**
- **`resolve(key)`** → Application assets in `{basePath}/assets/{key}/`
- **`getResource(file)`** → Engine resources in `{libraryRoot}/resources/{file}`

### Path Resolution Rules

| Key | Resolves To | Example |
|-----|-------------|---------|
| `"models"` | `{exe}/assets/models/` | Load your 3D models |
| `"textures"` | `{exe}/assets/textures/` | Load your textures |
| `"shaders"` | `{exe}/assets/shaders/` | Load custom shaders |
| `"materials"` | `{exe}/assets/materials/` | Load material files |
| `"scenes"` | `{exe}/assets/scenes/` | Load scene files |
| `"configs"` | `{exe}/configs/` | Configuration files |
| `"logs"` | `{exe}/logs/` | Log output |
| `getResource(file)` | `{engine}/resources/{file}` | Engine resources |

### Custom Path Overrides

Override default paths programmatically:
```cpp
// Override models directory to a custom location
PathProvider::overridePath("models", "C:/MyProject/CustomModels");

// Now resolves to C:/MyProject/CustomModels/
auto modelPath = PathProvider::resolve("models") / "model.obj";
```

### Example: Complete Asset Loading

```cpp
int main(int argc, char** argv)
{
    engine::GameEngine engine;
    engine.initialize(options);
    
    auto resourceManager = engine.getResourceManager();
    
    // Load model from assets/models/
    auto modelPath = PathProvider::resolve("models") / "robot.obj";
    auto maybeModel = resourceManager->m_modelManager->createModel(modelPath.string());
    
    // Load texture from assets/textures/
    auto texturePath = PathProvider::resolve("textures") / "robot_diffuse.png";
    auto maybeTexture = resourceManager->m_textureManager->createTextureFromFile(texturePath.string());
    
    // Load engine resource (from Vienna-WebGPU-Engine/resources/)
    auto floorTexture = PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg");
    auto maybeFloorTex = resourceManager->m_textureManager->createTextureFromFile(floorTexture);
    
    // ... rest of setup
}
```

---

## Loading and Rendering a Model

### Step 1: Load Model from Disk

```cpp
auto resourceManager = engine.getResourceManager();

// Load OBJ model (recommended format)
auto maybeModel = resourceManager->m_modelManager->createModel("my_model.obj");

if (!maybeModel.has_value()) {
    spdlog::error("Failed to load model");
    return -1;
}
```

### Step 2: Create Model Node and Add to Scene

```cpp
// Create a model render node
auto modelNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(
    maybeModel.value()
);

// Position the model
modelNode->getTransform().setLocalPosition(glm::vec3(0, 1, 0));
modelNode->getTransform().setLocalRotation(glm::quat(glm::radians(glm::vec3(0, 45, 0))));
modelNode->getTransform().setLocalScale(glm::vec3(2, 2, 2));

// Add to scene
rootNode->addChild(modelNode);
```

### Model Instancing

Multiple nodes can share the same model (GPU memory is shared):

```cpp
// First instance
auto instance1 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModel.value());
instance1->getTransform().setLocalPosition(glm::vec3(0, 1, 0));
rootNode->addChild(instance1);

// Second instance (shares GPU data)
auto instance2 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModel.value());
instance2->getTransform().setLocalPosition(glm::vec3(5, 1, 0));
rootNode->addChild(instance2);
```

### Supported Model Formats

- **OBJ** ✅ Stable - Recommended
- **GLTF/GLB** ⚠️ Work in progress - Not yet fully functional

---

## Working with the Scene

The engine uses a **scene graph** system with specialized node types.

### Scene Structure

```cpp
auto sceneManager = engine.getSceneManager();
auto mainScene = sceneManager->createScene("MyScene");
auto rootNode = mainScene->getRoot();

// Every scene has:
// - Root node (container for all scene objects)
// - Main camera (automatically created)
```

### Node Types

- **`ModelRenderNode`** - Renders a 3D model
- **`LightNode`** - Provides lighting
- **`UpdateNode`** - Custom update logic node
- **Generic `Node`** - Transform hierarchy only

### Transform Operations

```cpp
auto transform = node->getTransform();

// Local space (relative to parent)
transform.setLocalPosition(glm::vec3(0, 1, 0));
transform.setLocalRotation(glm::quat(glm::radians(glm::vec3(0, 45, 0))));
transform.setLocalScale(glm::vec3(2, 2, 2));

// World space (absolute)
transform.setWorldPosition(glm::vec3(5, 0, 0));

// Look at target
transform.lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
```

### Transform Hierarchy

Child transforms are relative to parent:

```cpp
auto parent = std::make_shared<engine::scene::nodes::Node>();
parent->getTransform().setLocalPosition(glm::vec3(5, 0, 0));

auto child = std::make_shared<engine::scene::nodes::ModelRenderNode>(model);
child->getTransform().setLocalPosition(glm::vec3(0, 1, 0));

parent->addChild(child);  // Child is now at world position (5, 1, 0)
rootNode->addChild(parent);
```

---

## Adding Lights

The engine supports multiple light types via `LightNode` and the light data structs in `engine::rendering`.
For complete examples (directional/point/spot, shadows, day-night cycle), see [examples/main_demo/main.cpp](../examples/main_demo/main.cpp).

```cpp
auto ambientLight = std::make_shared<engine::scene::nodes::LightNode>();

engine::rendering::AmbientLight ambientData{};
ambientData.color = glm::vec3(0.2f, 0.2f, 0.2f);
ambientData.intensity = 1.0f;

ambientLight->getLight().setData(ambientData);
rootNode->addChild(ambientLight->asNode());
```

---

## Camera Setup

### Access Main Camera

```cpp
auto mainCamera = mainScene->getMainCamera();

// Configure camera
mainCamera->setFov(45.0f);
mainCamera->setNearFar(0.1f, 100.0f);
mainCamera->setPerspective(true);
mainCamera->setBackgroundColor(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));

// Position camera
mainCamera->getTransform().setLocalPosition(glm::vec3(0, 2, 5));
mainCamera->getTransform().lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
```

### Custom Camera Controller

For a working controller, see [examples/main_demo/OrbitCamera.h](../examples/main_demo/OrbitCamera.h) and [examples/main_demo/OrbitCamera.cpp](../examples/main_demo/OrbitCamera.cpp).

---

## Input Handling

### Access Input via UpdateNode

Create an `UpdateNode` to handle input each frame:

```cpp
class MyInputHandler : public engine::scene::nodes::UpdateNode
{
public:
    void update(float deltaTime) override {
        auto input = engine()->input();
        if (!input)
            return;
        
        // Keyboard
        if (input->isKey(SDL_SCANCODE_W)) {
            // Move forward
        }
        if (input->isKeyDown(SDL_SCANCODE_SPACE)) {
            // Jump (triggered once per press)
        }
        
        // Mouse
        if (input->isMouse(SDL_BUTTON_LEFT)) {
            auto mousePos = input->getMousePosition();
            auto mouseDelta = input->getMouseDelta();
            // Handle mouse input
        }
        
        // Mouse wheel
        auto wheel = input->getMouseWheel();
        if (wheel.y != 0.0f) {
            // Zoom or scroll
        }
    }
};

auto inputHandler = std::make_shared<MyInputHandler>();
rootNode->addChild(inputHandler);
```

---

## ImGui Integration

### Add ImGui Windows

```cpp
auto imguiManager = engine.getImGuiManager();

// Add a simple window
imguiManager->addFrame([]() {
    ImGui::Begin("My Window");
    ImGui::Text("Hello from ImGui!");
    ImGui::End();
});
```

For a larger UI setup, see [examples/main_demo/MainDemoImGuiUI.cpp](../examples/main_demo/MainDemoImGuiUI.cpp).

---

## Next Steps

### Example Projects

Explore the complete example:

- **[main_demo](../examples/main_demo/)** - Complete demo with:
  - Model loading and instancing
  - Multiple light types with shadows
  - Orbit camera controller
  - Day-night cycle system
  - ImGui UI integration
  - PBR materials with textures

### Key Files to Study

- [examples/main_demo/OrbitCamera.h](../examples/main_demo/OrbitCamera.h) - Camera controller
- [examples/main_demo/DayNightCycle.h](../examples/main_demo/DayNightCycle.h) - Custom update node
- [examples/main_demo/MainDemoImGuiUI.h](../examples/main_demo/MainDemoImGuiUI.h) - ImGui integration

### Learn More

- **[Engine Architecture](EngineArchitecture.md)** - Detailed technical documentation
- **[Bind Group System](BindGroupSystem.md)** - Advanced rendering concepts
- **[Copilot Instructions](../.github/copilot-instructions.md)** - Comprehensive development guide

---

## Quick Reference Card

| Task | Code |
|------|------|
| Initialize engine | `engine.initialize(options)` |
| Create scene | `sceneManager->createScene("Name")` |
| Get root node | `mainScene->getRoot()` |
| Get camera | `mainScene->getMainCamera()` |
| Load model | `resourceManager->m_modelManager->createModel("file.obj")` |
| Create model node | `std::make_shared<ModelRenderNode>(model)` |
| Add to scene | `rootNode->addChild(node)` |
| Set position | `node->getTransform().setLocalPosition(pos)` |
| Create light | `std::make_shared<LightNode>()` |
| Load scene | `sceneManager->loadScene("Name")` |
| Run engine | `engine.run()` |

---

## Common Patterns

### Update Node Template

```cpp
class MyCustomNode : public engine::scene::nodes::UpdateNode
{
public:
    void update(float deltaTime) override {
        // Access engine systems
        auto input = engine()->input();
        auto gpu = engine()->gpu();
        auto resources = engine()->resources();
        
        // Your logic here
    }
};
```

---

## Need Help?

- Start with the [main_demo example](../examples/main_demo/main.cpp)
- Check [Engine Architecture](EngineArchitecture.md) for technical details
- Read inline documentation in header files
- Review shader code in `resources/*.wgsl`

If you get stuck, start from [examples/main_demo/main.cpp](../examples/main_demo/main.cpp).
