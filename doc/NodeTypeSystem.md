# Node Type System

## Overview

Scene nodes are plain C++ classes, but scenes are saved as data (JSON). The node
type system bridges the two: a registry maps a stable type-name string to how a
node is constructed and how its data is read and written. This lets the editor
list every available node type (built-in and project-defined) and lets scenes
round-trip through files. When a saved scene references a type the current build
does not have, it loads anyway and preserves the data instead of failing.

**Key features:**
- **Registry-driven types**: one registration makes a node type creatable in the editor and serializable.
- **JSON-free node classes**: `Node` and its subclasses carry no serialization code. The (de)serialization hooks live in the registry, not on the node.
- **Graceful missing types**: an unknown type loads as a `PlaceholderNode` that preserves the original type name and data, re-emits them verbatim on save, and is flagged in the editor.
- **Portable behavior, the realistic way**: data always travels in the JSON; the C++ behavior only runs where the type is compiled in (the registry never carries executable code).

## Architecture

### Components

- **NodeTypeRegistry** (`engine/scene/NodeTypeRegistry.h`): `typeName -> NodeTypeInfo`. Holds the display name, a factory, and optional serialize/deserialize functions. Keeps a `std::type_index -> typeName` reverse map so the serializer can name a live node by its concrete type.
- **PlaceholderNode** (`engine/scene/nodes/PlaceholderNode.h`): stand-in for an unknown type. A `SpatialNode` that stores the missing type name and the original props as raw JSON text.
- **SceneSerializer** (`engine/scene/SceneSerializer.h`): walks the node tree, writes common fields itself, and delegates type-specific data to the registry.
- **registerBuiltinNodeTypes()**: registers the engine types (Node, Spatial, Camera, Light, Model). Called once during `GameEngine::initialize`, before any scene loads.

### Scene JSON format

Each node is one object:

```json
{
  "type": "Light",
  "name": "Sun",
  "enabled": true,
  "transform": { "position": [0,0,0], "euler": [50,-30,0], "scale": [1,1,1] },
  "props": { "lightType": "Directional", "color": [1,1,1], "intensity": 2.0 },
  "children": []
}
```

`type`, `name`, `enabled`, `transform`, and `children` are handled by the
serializer for every node. `props` is the type-specific block produced and
consumed by that type's registry hooks (light data, camera parameters, a model
path, a custom node's own members). A camera also gets a top-level `"main": true`
when it is the scene's main camera, since that is a scene-level fact a node
cannot know about itself.

Asset paths inside `props` (e.g. a model path) are stored relative to the scene
folder when the asset lives inside it, absolute otherwise, so a scene folder
stays portable across machines.

## Registering a custom node type

A project registers its node type once at startup (after the engine is
initialized, so the built-ins are present). The factory is explicit because not
every node is default-constructible.

```cpp
#include "engine/scene/NodeTypeRegistry.h"

engine::scene::NodeTypeRegistry::instance().registerType<EnemyNode>(
    "EnemyNode",  // stable key written to scene files - do not rename casually
    "Enemy",      // label shown in the editor's Add menu
    [] { return std::make_shared<EnemyNode>(); },                 // factory
    [](const engine::scene::nodes::Node &n, nlohmann::json &props,
       const engine::scene::NodeSerializeContext &) {
        const auto &e = dynamic_cast<const EnemyNode &>(n);
        props["hp"] = e.hp;
    },
    [](engine::scene::nodes::Node &n, const nlohmann::json &props,
       const engine::scene::NodeSerializeContext &) {
        auto &e = dynamic_cast<EnemyNode &>(n);
        e.hp = props.value("hp", 100);
    });
```

The `serialize`/`deserialize` arguments are optional - omit them for a node with
no data of its own. `NodeSerializeContext::sceneFolder` is available for types
that need to store paths relative to the scene (the built-in `Model` type uses
it). The cast from `Node&` must be `dynamic_cast`, not `static_cast`, because
`Node` is a virtual base.

After this call, the type appears in the editor's Add menu, round-trips through
scene files, and degrades to a preserving placeholder anywhere it is not
registered.

## Missing types and export portability

A node is **data** plus **behavior**. The data (transform, properties,
hierarchy) always travels in the JSON. The behavior is compiled C++ - it can
only run in a build that compiled that type. You cannot put runnable code in a
scene file.

So when a scene that uses `EnemyNode` is opened in a build that never registered
`EnemyNode`:

- It **loads**; the unknown node becomes a `PlaceholderNode`.
- The placeholder keeps the original type name, the full `props`, the transform,
  the name, and its place in the hierarchy. Its subtree is still reconstructed.
- On **save** it re-emits the original type and props verbatim, so opening the
  same scene in a build that *does* have `EnemyNode` restores it fully. No data
  is lost.
- The editor shows it as `(Missing) EnemyNode` and offers "Replace with Spatial
  Node" or "Delete" (data is only dropped on that explicit action).

This mirrors how Unity handles a missing script (the serialized data is kept and
reconnects when the script returns). To make the behavior itself portable to a
build that did not compile it, ship the type's C++ source so the target build
compiles it, or add a scripting layer - neither is part of this system.

## File map

- `include/engine/scene/NodeTypeRegistry.h`, `src/engine/scene/NodeTypeRegistry.cpp` - registry + built-in registrations.
- `include/engine/scene/nodes/PlaceholderNode.h` - missing-type stand-in.
- `include/engine/scene/SceneSerializer.h`, `src/engine/scene/SceneSerializer.cpp` - save/load.
- `src/engine/GameEngine.cpp` - calls `registerBuiltinNodeTypes()` during init.
