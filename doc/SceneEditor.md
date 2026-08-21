# Scene Editor

A dockable, live scene editor built on the engine (`examples/scene_editor/`). It
renders the scene to an off-screen texture inside a viewport panel, lets you
build and edit a scene graph, configure materials/shaders/engine settings, save
and load **projects**, and **compile a project into a standalone game**.

This guide describes how to use it and how it maps onto the engine APIs. It is a
companion to [GettingStarted](GettingStarted.md), [EngineArchitecture](EngineArchitecture.md)
and [NodeTypeSystem](NodeTypeSystem.md).

---

## Build and run

```bat
:: Windows
scripts\build-example.bat scene_editor Debug WGPU
examples\build\scene_editor\Windows\Debug\SceneEditor.exe
```

```bash
# Linux / macOS
scripts/build-example.sh scene_editor Debug WGPU
```

In Debug builds the engine resolves paths against the repo via the
`DEBUG_ROOT_DIR` / `ASSETS_ROOT_DIR` compile definitions, so the editor's
**Resources** root is `<repo>/resources` and its **Assets** root is
`examples/scene_editor/assets`.

If a build's final link fails with `LNK1168: cannot open SceneEditor.exe`, an
instance is still running - close it (or `taskkill /F /IM SceneEditor.exe`) and
rebuild.

---

## The interface

The editor uses an ImGui dockspace. Panels are toggled from the **Window** menu;
**Window → Reset Layout** restores the default arrangement and re-shows them all.

| Panel         | Purpose |
|---------------|---------|
| **Viewport**  | The rendered scene (off-screen texture). Camera navigation, gizmo, picking. |
| **Hierarchy** | The scene graph tree. Add / delete / reparent / rename nodes. |
| **Inspector** | Edit the selected node: name, enabled, transform, and type-specific data (light / camera / model + material). |
| **Assets**    | File browser over **Resources** (read-only) and **Assets** (editable), plus model import and shader creation. |
| **Settings**  | Engine settings (frame / physics / rendering / window / audio) + project name, startup scene, per-scene override. |
| **Log**       | Captured `spdlog` output: sortable, filterable, copyable. |

Each panel's close button (and the Window-menu checkbox) hides/shows it.

---

## Navigating the viewport

A **camera selector** sits at the top-right of the Viewport:

- **Editor (free-fly)** - the default. Hold **Right Mouse** to look, **W/A/S/D**
  to move, **Q/E** down/up, **Shift** to sprint. Input is active while the
  viewport is hovered.
- **Any scene `CameraNode`** - the viewport renders from that camera's point of
  view (its pose + projection are mirrored onto the editor camera). Switching
  back to *Editor (free-fly)* restores your previous free-fly view.

**Focus (F)** frames the editor camera on the selected node (free-fly only).

> Camera input reaches the editor even while the viewport is focused because the
> engine forwards input unless an ImGui text field is active (it gates on
> `io.WantTextInput`, not the broader capture flags).

---

## Selecting and transforming

- **Left-click** an object in the viewport to select it. A selected node's gizmo
  sits on top of it, so to select something *behind* the gizmo, click away from
  the gizmo first.
- The **gizmo toolbar** (top-left of the viewport) switches Move / Rotate /
  Scale, toggles **World/Local** space, and toggles **Snap** (with an inline
  grid/angle/step value).
- **Duplicate (Ctrl+D)** deep-clones the selected subtree (via
  `SceneSerializer::cloneNode`) as a sibling.

Gizmo shortcuts default to **Ctrl+1 / Ctrl+2 / Ctrl+3** (move/rotate/scale) and
**Ctrl+4** (toggle space); rebind them in **Edit → Keyboard Shortcuts**.

---

## The scene graph (Hierarchy)

- **+ Add** creates a node of any registered type (Node, Spatial, Camera, Light,
  Model, and any custom types registered by the project).
- Drag a node onto another to **reparent**; right-click for the context menu;
  double-click (or the inspector) to **rename**.
- Node types come from the engine's **`NodeTypeRegistry`**. A scene that
  references a type the current build does not have loads as a **`PlaceholderNode`**:
  the original type name and all its data are preserved and round-trip on save,
  and the inspector flags it as `(Missing)` with a Replace/Delete option. See
  [NodeTypeSystem](NodeTypeSystem.md).

The editor camera and its controller are flagged **non-serializable**, so they
never appear in saved scenes.

---

## Materials and textures

The Inspector's **Material** section (for a `ModelRenderNode`) reads the material
on the model's first submesh:

- Pick the **shader** from the registry; edit **PBR** (base color / metallic /
  roughness) or **Unlit** (color) properties.
- **Texture slots are discovered from the shader** - each `MaterialTexture`
  binding in the shader's `@group(Material)` layout shows a row. So the rows
  match whatever the selected shader actually declares (diffuse, normal,
  roughness, ...), not a fixed list.
- Assign a texture per slot by **Browse...**, by **dragging a file** from the
  Assets browser onto the slot, or by typing/pasting a path in the
  *assign-by-path* row. **Clear** removes it.

If you assign a texture that lives **outside** the asset/resource roots, a
**Copy Into Assets** dialog offers to copy it into `assets/textures/` and use the
portable engine path, keeping the project self-contained.

Material edits propagate automatically: `Material::setTexture/setShader/...` bump
a version that the per-material GPU bind group syncs from next frame.

---

## Shaders

**Assets → New Shader** starts a shader from a known-good template (a Material or
Post-process base, with the engine includes + lighting already filled in), writes
it under `resources/shaders/`, builds it via the reflection-driven
`ShaderDescriptor` path, and registers it so it appears in the material shader
picker.

---

## The asset browser

Two roots:

- **Resources (read-only)** - engine resources (`<repo>/resources`): shaders,
  default meshes, etc. You can open them but not delete/rename them.
- **Assets (project)** - the project's editable assets.

Per file: **double-click** opens it (Resources → your editor; Assets → the OS
file browser), and right-click gives **Open / Rename / Delete** plus *Show in
Explorer* and *Copy asset/resource path*. **Open with** selects the editor used
(*OS default app* or *VS Code*).

Listings are **cached**; **Reload** re-reads now and **Auto-refresh** polls every
~2 s so externally-added files appear on their own. Drag a file from either tree
onto an input field or material slot to assign it.

### Paths and engine tokens

Asset references are stored as portable **engine path tokens**:

- `resource://<rel>` - under the engine resource root (provided on every machine).
- `asset://<rel>` - under the project asset root.
- otherwise an absolute path.

The inspector shows the relative engine path; **right-click** it to copy as
**asset/resource path**, **relative path**, or **absolute path**. Resolution is
case/separator-robust on Windows (`PathProvider::toEnginePath` / `resolveEnginePath`).

---

## Scenes vs Projects

A **Scene** (`scene.json`) is a node tree. A **Project** (`project.json`) ties
together the engine settings, the list of scenes, and the startup scene.

**Scene** (File menu):
- **New Scene (Ctrl+N)**, **Open... (Ctrl+O)**, **Open Recent**, **Save /
  Save As (Ctrl+S)** - prompts to discard unsaved changes where relevant.
- **Export Self-Contained...** copies every referenced *project* asset into an
  `assets/` folder next to the `.json` and rewrites references to scene-relative
  paths, producing a folder you can move between machines as-is. (`resource://`
  references stay tokens - the engine provides them everywhere.)

**Project** (Project menu): **New / Open / Save Project**, and **Build Game...**.

### Settings and per-scene overrides

The **Settings** panel edits the **project's** engine settings; **Apply** pushes
them to the running engine and **stores them on the project**. Tick **Override
engine settings for the current scene** to store the values on the active scene
instead. The effective settings a scene runs with are:

```
effective = project settings  (+ that scene's override, if present)
```

`GameEngineOptions` is JSON-serializable (`engine::settings::toJson/fromJson`);
the runtime-changeable fields (vsync, frame cap, MSAA, window, ...) apply live via
`GameEngine::setOptions`.

---

## Build Game (compile to a standalone game)

**Project → Build Game...** compiles the current project into a runnable game:

1. Saves the project (`project.json` + the current scene).
2. On a **background thread**, builds the **runtime player** in Release
   (`scripts/build-example.bat runtime_player Release WGPU`), **streaming the
   build output into the Log** (lines prefixed `[build]`).
3. Stages the engine `resources/` + the project's `assets/` + `project.json`
   next to the produced `RuntimePlayer.exe`.

The result is a self-contained folder (`examples/build/runtime_player/Windows/Release/`)
whose `RuntimePlayer.exe` loads `assets/project.json`, applies the settings, and
runs the startup scene - no editor.

**Requirements / caveats:**
- Currently **Windows-only** (uses `cmd` + the `.bat` build script + `_popen`);
  it needs the Visual Studio toolchain, which the script's `launch-vsdevcmd`
  locates. The first build compiles the whole engine in Release (slow); later
  builds are incremental.
- The startup scene **must contain a camera**. The editor camera is excluded from
  saves, so add your own `CameraNode` or the game renders nothing.

---

## The Log panel

Captures all `spdlog` output through an editor sink. The table is **sortable**
(Time / Level / Module / Message), filterable by **minimum level** and **text**,
**Copy**-able (whole filtered view, or a single line via right-click), and
auto-scrolls when at the bottom. **Clear** empties it.

On an unhandled crash the editor writes a recovery scene to
`crash_recovery/scene.json` so work is not lost.

---

## Keyboard shortcuts

| Action | Shortcut |
|--------|----------|
| New scene | Ctrl+N |
| Open scene | Ctrl+O |
| Save scene | Ctrl+S |
| Duplicate node | Ctrl+D |
| Focus selected | F |
| Gizmo: move / rotate / scale | Ctrl+1 / Ctrl+2 / Ctrl+3 (rebindable) |
| Gizmo: toggle World/Local | Ctrl+4 (rebindable) |
| Camera look / move / up-down / sprint | Right Mouse / WASD / Q-E / Shift |

(See **Edit → Keyboard Shortcuts** for the live reference and to rebind the gizmo
keys.)

---

## Under the hood

The editor is engine-thin: it drives public engine APIs and adds no
serialization logic of its own.

| Concern | Engine API |
|---------|-----------|
| Scene save/load, clone, export | `engine::scene::SceneSerializer` (`save`, `load`, `exportSelfContained`, `cloneNode`) |
| Node types + missing-type fallback | `engine::scene::NodeTypeRegistry`, `PlaceholderNode` |
| Project file | `engine::Project` + `engine::ProjectSerializer` |
| Engine settings (de)serialize | `engine::settings::toJson/fromJson`, `GameEngine::getOptions/setOptions` |
| Portable paths | `engine::core::PathProvider` engine path tokens |
| Off-screen viewport | `CameraNode::setOffscreenOnly/setRenderSize`, `Renderer::getCameraOutputTexture` |
| Standalone game | `examples/runtime_player` |

Editor source (`examples/scene_editor/`):

- `main.cpp` - engine init, editor camera (`setupEditorCamera`), default scene, crash save.
- `SceneEditorUI.{h,cpp}` - all panels, menus, gizmo, picking, project/build flows.
- `EditorCameraController.{h,cpp}` - free-fly + look-through camera.
- `EditorState.h` - shared editor state. `EditorLog.h` - the log sink + store.
- `ImGuizmo.{h,cpp}`, `tinyfiledialogs.{c,h}` - vendored helpers (gizmo, native dialogs).

Native helpers are confined to the example: `tinyfiledialogs` (file/save dialogs,
message boxes) is only compiled/linked into `SceneEditor`/`RuntimePlayer`, not the
engine library.

---

## Known limitations

- **No undo/redo yet.** Edits are immediate; use Save before risky changes.
- **Build Game is Windows-only** for now.
- **Per-scene settings override** stores a full snapshot of the settings (the
  underlying merge supports partial overrides, but the editor writes all keys).
- A saved scene has **no main camera** (the editor camera is excluded), so add a
  `CameraNode` before building a game from it.
