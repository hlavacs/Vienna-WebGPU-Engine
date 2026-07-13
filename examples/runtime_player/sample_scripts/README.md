# Project scripts (custom C++ behaviour)

Scripts are **compiled-in C++**, not a runtime scripting language. Put `.cpp`
files that define custom node types in a **`scripts/` folder next to your
`.vproj`**. When you run **Build Game**, every `.cpp` there is compiled straight
into the game executable.

## How it works

- The editor sets `PROJECT_SCRIPTS_DIR` to your `scripts/` folder; the runtime
  player's CMake globs and compiles it (a plain player build with no scripts is
  unaffected).
- Each script **self-registers** with one line - `REGISTER_NODE(YourType);` at
  file scope (see `Rotator.cpp`). No central list to edit. For fully custom
  (de)serialization, call `NodeTypeRegistry::instance().registerScript<T>(name,
  serializeFn, deserializeFn)` from a static initializer instead.
- Save Project records your scripts in the `.vproj` (a `scripts` list).
- A scene node with `"type": "<YourType>"` gets that behaviour in the built game.
- In the **editor** (which does not compile your scripts) such a node is a
  `PlaceholderNode`: its data round-trips untouched and it comes alive once built.

## Fields (Unity-like)

Expose public fields with a `reflect()` override - one line each:

```cpp
void reflect(engine::reflection::Reflector &r) override {
    r("axis", axis);
    r("degreesPerSecond", degreesPerSecond);
}
```

That single list drives everything: the fields are **saved** to the scene,
**restored** on load (a missing key keeps the member's default - your "start
value"), and shown as **editable rows in the inspector**. No separate save/load/UI
code. Supported types: `float`, `int`, `bool`, `glm::vec2/3/4`, `std::string`.

(In the editor a project-script node is a `PlaceholderNode` since the type is not
compiled there - its fields are still editable as raw values and reach the built
game, where the real type + defaults live.)

## Conventions

- Behaviour = a `Node` subclass. Add `SpatialNode` for a transform and/or
  `UpdateNode` for a per-frame `update(float dt)` (both virtually inherit `Node`).
- Give the type a stable string name (the scene references it by that name); do
  not rename casually.

`Rotator.cpp` in this folder is a complete, copy-ready example.
