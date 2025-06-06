# 🛠️ Engine To-Do List

### ✅ **Project Infrastructure**

- [x] Create a build system
  
  - [x] Consider Conan for dependency management
  
  - [x] Support Multiple Builds
    
    - [x] Windows (Debug/Release)
    
    - [x] Emscripten (Debug/Release)

- [ ] Restructure build system to use `.bat` (Windows) and `.sh` (Linux/macOS) wrapper scripts for **interchangeable IDE** support
  
  - [x] VSCode tasks should only reference these wrapper files, not direct commands
  - [x] Decide which scripting format makes most sense:
    - `.bat` / `.ps1` for Windows
    - `.sh` for Linux/macOS
    - Python for portability?
  - [ ] Build for Linux and macOS

- [x] Define a `external/` directory for external libs (GLM, stb, etc.)

- [x] Setup Example Project for Windows

- [x] Setup Example Project for Emscripten

---

### 🧱 **Core Engine Structure**

- [ ] Define `Engine` entry point (init, run, shutdown)
- [ ] Add `Application` wrapper with main loop and `onFrame`, `onResize`, etc. callbacks
- [ ] Implement time management (deltaTime, totalTime)
- [ ] Implement logging system

---

### 🎨 **Rendering**

- [ ] Set up WebGPU context and swapchain
- [ ] Basic renderer pipeline (color + depth)
- [ ] Abstract `Shader` class with safe uniforms API
  - [ ] Load WGSL shaders from file
  - [ ] Allow user to change shader values safely via C++ API
- [ ] Implement `Material` abstraction
  - [ ] Supports multiple shader types (basic, PBR, etc.)
  - [ ] Expose common properties (base color, roughness, metallic, etc.)
- [ ] Implement `Mesh` abstraction
  - [ ] VBO/IBO support
- [ ] Basic lighting (Directional)
- [ ] Support shadows (depth map)
- [ ] Add support for transparency
- [ ] Add post-processing stage (bloom, tone mapping)

---

### 🧍‍♂️ **Scene & Entity Management**

- [ ] Entity-Component-System (or pseudo-ECS) design
- [ ] Scene graph with transforms
- [ ] SceneManager
  - [ ] Load/Save scenes from `.scene` file
  - [ ] Register and resolve asset references (by name or ID)
  - [ ] Optional: pack scene + all assets into one bundle
- [ ] AssetManager
  - [ ] Handle textures, materials, meshes, shaders
  - [ ] Reference-counted or shared pointers

---

### 📦 **Assets & File Formats**

- [ ] OBJ loader for static mesh support
- [ ] Support PNG/JPEG textures
- [ ] Add glTF loader (long-term)
- [ ] Default material/shader templates
- [ ] Create file watcher for hot-reload

---

### 🧪 **Tooling & Debugging**

- [x] Simple in-app UI for dev tools (e.g., ImGui)
- [ ] Frame statistics overlay (FPS, memory, draw calls)
- [ ] Resource reloading
- [ ] Add debug view modes (normals, depth, wireframe)

---

### 🧰 **Utilities**

- [x] Math library integration (GLM or custom)
- [ ] UUID generator for assets/entities
- [ ] Serialization system 

---

### 🔮 Future

- [ ] Physics Engine Integration (start planning structure)
  - [ ] Collision system (AABB, spheres, mesh colliders)
  - [ ] Rigidbody support
  - [ ] Physics material system
- [ ] Animation system
- [ ] Editor GUI (level editor, asset browser)


