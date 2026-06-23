# Shader System v1 — codegen-driven, C++ structs as source of truth

> **As-built note (supersedes the contradicting parts of the draft below).**
> The implemented system is a hybrid of this doc and the older reflection
> direction:
> - **Engine structs (Frame/Scene/Material/Object) are codegen + `#include`** as
>   described here. The C++ `StructDescriptor`s emit `resources/shaders/core/*.wgsl`;
>   shaders pull them in by `engine://core/...`.
> - **Bind-group layouts are built by reflecting the include-expanded WGSL**
>   (`WgslReflector`), not from a hand-written builder and not from a separate
>   C++ layout table. So runtime reflection *is* on the shader-load path - the
>   "no runtime reflection" rule below applies only to struct *layout* (which
>   still comes from the descriptors via the validator), not to bind-group
>   discovery.
> - **Shaders are registered with a declarative `ShaderDescriptor`**
>   (`WebGPUShaderFactory::buildFromDescriptor`) that carries only what WGSL
>   cannot: pipeline state (depth/cull/color formats), custom-group name/reuse,
>   and material texture slot/fallback overlays. There is no `EngineBindGroup`
>   builder chain anymore.
> - **Custom groups** live at `@group(4..7)` (wgpu-native's 8-group cap), not the
>   "20+" mentioned later in this doc.
> - **The validator** (`ShaderValidator`) runs on the expanded WGSL and is
>   **fatal in Debug/CI, compiled out in Release** (gated by
>   `ENGINE_SHADER_VALIDATION`).

**Status:** implemented (hybrid - see note above) · supersedes `doc/SpecShaderReflection.md`

This doc is the single authoritative reference for how shaders, bind groups,
includes, and hot reload work in this engine going forward. The previous
"reflection-first" plan is archived (`SpecShaderReflection.md` now redirects
here); the WGSL parser built for it survives in a smaller role — a post-codegen
**validator** that sanity-checks the emitted WGSL against the C++ descriptor.

---

## 1. Core Idea

Shaders are compiled assets built from:

- **WGSL source files** authored by hand (logic only).
- **Engine-generated WGSL** emitted from C++ struct descriptors (interfaces).
- **Includes** to compose generated headers + math/utility helpers.
- **Bind-group layouts** derived from the same C++ descriptors that drive
  codegen — never re-parsed from the WGSL.

**No runtime reflection** sits on the critical path. The WGSL parser exists
but only as an *optional* sanity check after codegen has emitted text (§13).

## 2. File System

All paths resolve through `engine::core::PathProvider`:

- `resources/` — root for engine + user assets.
- `resources/shaders/` — hand-written WGSL files (vertex / fragment / compute
  logic). The current flat layout under `resources/*.wgsl` migrates here over
  time; both locations work during the transition.
- `resources/generated/` — engine-emitted WGSL fragments (one per registered
  struct or bind-group descriptor). Checked into git so debug builds can read
  them through the existing `PathProvider::getResource` flow without a
  pre-build codegen step. Regenerated on engine start when stale.

## 3. C++ Structs and Bind-Group Layouts

### 3.1 Source of truth

A **C++ struct** plus a tiny **`GpuStructTraits<T>` specialisation** is the
single source of truth for any GPU-side struct. Everything downstream —
generated WGSL, bind-group layout, buffer size, hot-reload dependency graph —
derives from it.

The C++ struct itself supplies field **types** and **offsets** via home-grown
aggregate reflection (`include/engine/rendering/shaders/AggregateReflect.h`,
~120 LoC of brace-init template tricks, no deps). The traits spec only carries
what C++17 reflection genuinely can't recover — the WGSL struct name and the
per-field WGSL names — since field names won't be derivable from C++ alone
until C++26 reflection lands reliably on MSVC.

```cpp
struct FrameUniforms              // existing C++ struct, unchanged
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 viewProjectionMatrix;
    glm::vec3 cameraWorldPosition;
    float     time;
};

template <> struct GpuStructTraits<FrameUniforms>     // new: the only per-struct boilerplate
{
    static constexpr const char *wgslName = "FrameUniforms";
    static constexpr std::array<const char *, 5> fieldNames = {
        "view_matrix", "projection_matrix", "view_projection_matrix",
        "camera_world_position", "time",
    };
};
// Descriptor is then `gpuStructDescriptorOf<FrameUniforms>()` from anywhere.
```

`gpuStructDescriptorOf<T>()` lazy-builds a cached `StructDescriptor`:

- Field **count** is asserted at compile time to match `fieldNames.size()`. A
  struct change that doesn't get mirrored in the traits spec fails the build
  instead of producing a wrong descriptor.
- Field **types** come from `aggregate_reflect::FieldTypeAt<I, T>` (via
  structured-binding tuple) and map through `wgslTypeOf<T>` to a `WgslType`.
- Field **offsets** come from `aggregate_reflect::fieldOffsetAt<I, T>()` (a
  static-instance address subtraction, computed once and cached).
- `StructDescriptor::build<T>(...)` then runs runtime sanity checks (`sizeof(T)`,
  per-field alignment, total size is a multiple of 16). All fail loud at
  engine startup; there's no quiet way for the C++ struct and the descriptor
  to drift.

### 3.2 Rule

Bind-group layouts and WGSL struct definitions are derived from descriptors.
Hand-writing either is forbidden. If a hand-written WGSL file declares a
struct or bind-group that the engine also generates, the codegen pass refuses
to load the shader.

### 3.3 Generated output

The codegen emits two kinds of artefact per descriptor:

1. The **WGSL struct** definition.
2. The **bind-group declaration** at its canonical `@group(N) @binding(M)`.

Generated `resources/generated/frame_uniforms.wgsl`:

```wgsl
// AUTO-GENERATED from engine::rendering::FrameUniforms. Do not edit.
struct FrameUniforms {
    view_matrix: mat4x4<f32>,
    projection_matrix: mat4x4<f32>,
    view_projection_matrix: mat4x4<f32>,
    camera_world_position: vec3<f32>,
    _pad_camera_world_position: f32,  // std140 padding inserted explicitly
    time: f32,
}

// Engine bind group "Frame" -> @group(0)
@group(0) @binding(0)
var<uniform> u_frame: FrameUniforms;
```

Padding is emitted **explicitly** with a `_pad_` prefix so the generated WGSL
size is always equal to `sizeof(StructDescriptor)`. No reliance on the WGSL
compiler's implicit padding rules; the bytes the engine writes from C++ land
at the right offset in the shader's view of memory.

## 4. Bind Group Convention

Already documented in `SKILL.md` and unchanged by this spec — replicated here
because it's part of the contract codegen enforces.

| `@group` | Role     | Reuse        | Anchor type                              |
|----------|----------|--------------|------------------------------------------|
| 0        | Frame    | PerCamera    | `FrameUniforms`                          |
| 1        | Scene    | PerCamera    | `LightsBuffer` + `ShadowUniform` + `EnvironmentUniforms` + `ClusterCell*` (each at its own `@binding(N)`) |
| 2        | Material | PerMaterial  | `MaterialUniforms` / `PBRProperties`     |
| 3        | Object   | PerObject    | `ObjectUniforms` or `array<ObjectInstance>` |
| 4..19    | reserved | —            | future engine slots                      |
| 20+      | Custom   | pass-defined | per-pass scratch (GBuffer textures, post-process inputs, cluster compute writeback) |

Codegen owns the `@group` index for engine groups. Custom groups (pass-side)
must be declared via a separate `CustomBindGroup` descriptor that pins a name
+ index ≥ 20; the engine generates their WGSL the same way, and the validator
errors on any hand-written `@group(< 20)` in shader source.

## 5. ShaderDescriptor

```cpp
struct ShaderDescriptor
{
    std::string                       name;                  // "PBR_Lit_Shader"
    std::string                       path;                  // "shaders/pbr_lit.wgsl"

    // Which engine bind groups this shader consumes. The engine emits the
    // generated bind-group headers for these and the shader #includes them.
    std::vector<EngineBindGroup>      engineGroups;          // Frame/Scene/Material/Object

    // Pass-specific resources. Each describes its own bindings and gets
    // emitted into resources/generated/<name>_<group>.wgsl.
    std::vector<CustomBindGroupDesc>  customGroups;

    PipelineState                     state;                 // depth/cull/blend/topology/color formats
};
```

`EngineBindGroup` is an enum (`Frame`, `Scene`, `Material`, `Object`) — the
shader opts in to which groups it actually uses. The runtime still binds all
four for every draw (per the convention), but the codegen only emits the
includes the shader requested, keeping unused symbols out of the WGSL.

## 6. WGSL Authoring Model

### 6.1 Includes

Hand-written shaders use `#include`:

```wgsl
#include "generated/frame_uniforms.wgsl"        // emits FrameUniforms + @group(0)
#include "generated/material_pbr.wgsl"          // emits PBRProperties + @group(2) bindings
#include "shaders/math/pbr_helpers.wgsl"        // pure functions, no bindings

@vertex
fn vs_main(...) -> VertexOut { ... use u_frame ... }
```

The include resolver is a textual preprocess pass that runs before WGSL
compilation:

- Paths are resolved against `resources/` (`PathProvider::getResource`).
- Recursive includes are followed; each file's full path is fed into the
  shader's dependency graph (§9).
- Duplicate includes (the same path included twice within one final WGSL)
  are deduplicated by path — `#pragma once` semantics, no opt-in needed,
  because every generated file is intended to appear at most once.
- The lexer already silently consumes `#`-prefixed lines, so reflection
  diagnostics still work on unexpanded WGSL during authoring.

### 6.2 Bind references

After include expansion the symbol `u_frame`, `u_material`, etc. simply
**exists** in the file. The author writes the same code they'd write in any
WGSL shader:

```wgsl
let clip = u_frame.projection_matrix * u_frame.view_matrix * world_pos;
```

C++ side still looks up the binding **by name** through the existing
`BindGroupBinder` flow — the codegen registers each emitted binding's logical
name → group/index pair so `shaderInfo->getBindGroupIndex("Frame_BindGroup")`
keeps working without the binder having to know about codegen at all.

## 7. Shader Compilation Pipeline

1. Load `ShaderDescriptor` from the registry (or build one inline for
   pass-specific shaders).
2. For each engine bind group referenced + each custom group, emit (or read
   the cached) generated WGSL into `resources/generated/`. Codegen is
   idempotent — same descriptor → same bytes.
3. Read the shader's hand-written WGSL from `resources/shaders/`.
4. Run the include resolver to produce the **final WGSL string**. Track every
   file path touched.
5. Run the validator (§13) on the final WGSL: parse bind groups + struct
   layouts, assert they match the C++ descriptors. Diagnostics fail loud.
6. Compile the final WGSL to a WebGPU shader module.
7. Build the bind-group layouts from the descriptors (NOT from the WGSL).
8. Build the pipeline using `state` from the descriptor.
9. Cache the result as a `ShaderPackage`.

## 8. ShaderPackage

```cpp
struct ShaderPackage
{
    std::shared_ptr<webgpu::WebGPUPipeline>          pipeline;
    std::vector<std::shared_ptr<BindGroupLayoutInfo>> layouts;       // by group index

    std::string                                       finalWgsl;     // post-include, post-codegen
    std::vector<std::filesystem::path>                dependencies;  // every file touched
    uint64_t                                          dependencyHash;
};
```

Owned by `ShaderRegistry` / `WebGPUPipelineManager`. Lookup is by descriptor
name; equality of `dependencyHash` decides whether a recompile is needed.

## 9. Hot Reload

Hot reload becomes a dependency-graph traversal — no reflection involved.

```
file changed (foo.wgsl)
  -> walk reverse-dependency edges: which ShaderPackages list foo.wgsl as a dep?
     -> for each: invalidate ShaderPackage
        -> rerun §7 from step 3
        -> swap the new pipeline + layouts into the cache atomically
```

Changing a C++ `StructDescriptor` registration triggers regeneration of the
matching generated file → which trips the file watcher → which invalidates
every dependent shader. No special "C++ changed" path; the codegen output is
the propagation surface.

## 10. Dependency Tracking

Each `ShaderPackage` records every file touched during include expansion:

- Hand-written WGSL files (logic + helpers).
- Generated WGSL files (from descriptors).
- Recursively-included files within either.

The watcher key is the union of these paths. Adding `#include "x.wgsl"` to a
shader automatically picks up `x.wgsl` as a new watched dependency on next
reload — no manual registration needed.

## 11. Key Rules

- C++ structs + their `StructDescriptor`s are the only source of truth for
  GPU-side struct layout and bind-group layout. WGSL never authoritatively
  declares either.
- Hand-written WGSL contains **logic only** (vertex/fragment/compute bodies,
  helper functions). It MUST `#include` the generated bindings; it MUST NOT
  redeclare a struct or `@group/@binding` that the engine also emits.
- Bind-group layouts come from descriptors, not from `pipeline.getBindGroupLayout(N)`.
- The validator (§13) is opt-out only for release builds where the cost of
  re-parsing every shader is unwanted.
- Generated files live in `resources/generated/` and are checked in. Treat
  them as build artefacts during code review (don't hand-edit; diff is signal).

## 12. Summary

- `resources/` is the only filesystem root.
- C++ struct + descriptor define every GPU interface.
- Engine emits WGSL + bind-group layouts from descriptors.
- Hand-written WGSL imports the generated headers and only writes logic.
- Hot reload is dependency-driven, not reflective.
- WGSL parser exists as a post-codegen validator (next section).

---

## 13. Validation (post-codegen)

The Phase 0+1 work in `src/engine/rendering/reflection/` is repurposed:

- The lexer + parser still produce a `ShaderReflection` from any WGSL string.
- After step 6 of §7, the engine runs the reflector on the **final, expanded**
  WGSL and compares:
  - struct layouts → must match `StructDescriptor` byte-for-byte (size, field
    offsets, types);
  - bind-group declarations → must match the descriptors' `@group(N) @binding(M)`
    sites + the WGSL var name;
  - canonical-group convention (engine roles at the right index; custom groups
    ≥ 20).
- Mismatch → `spdlog::error` with a structured diff and shader load fails.

This catches:
- Codegen bugs (engine emitted text that doesn't match the C++ descriptor).
- Hand-written shaders that redeclared a generated symbol with a different
  layout.
- WGSL author typos that change a binding's resource type (`var<uniform>` vs
  `var<storage>`).

In release builds the validator can be compiled out (it's a pure post-check;
the runtime never reads from it). In Debug / CI it always runs.

The `examples/reflection_test/` smoke test stays — its role shifts to
"validator regression test" — drop a WGSL file in, confirm the validator
produces zero diagnostics when the file matches its descriptors.

## 14. Implementation Roadmap

**Phase A: Codegen prototype (current draft).**
- `engine::rendering::shaders::StructDescriptor` types + `WgslType` enum.
- `ShaderCodegen::emitStruct(...)`, `emitBindGroupBinding(...)`,
  `emitGeneratedFile(...)` — pure-text emitters with no engine deps.
- One real descriptor wired: `FrameUniforms`.
- A small standalone test that runs the emitter and diffs the output against
  the WGSL hand-written in `skybox.wgsl` for the `FrameUniforms` block.
- No engine integration yet. Exit criterion: the emitter produces text that
  compiles when pasted into one of the existing shaders.

**Phase B: Include resolver.**
- Textual `#include` resolver that recurses through `resources/`.
- Plug into `WebGPUShaderFactory::createOrReload` *before* the WGSL string is
  handed to wgpu.
- Migrate ONE shader (skybox or fullscreen_quad — smallest blast radius) to
  drop its hand-written `FrameUniforms` and `#include "generated/frame_uniforms.wgsl"`
  instead. Demo still renders identically.

**Phase C: ShaderDescriptor + generated bind-groups.**
- Introduce `ShaderDescriptor`, `EngineBindGroup`, `CustomBindGroupDesc`.
- All four engine groups (Frame, Scene, Material, Object) get descriptors and
  generated files.
- `ShaderRegistry::createXShader()` chains are rewritten as `ShaderDescriptor`
  literals. Hand-built bind-group layouts disappear.

**Phase D: Validator wiring.**
- Plug the existing reflector into the post-codegen step.
- All existing engine shaders pass.
- Reflection annotations (`//@bind_group`, `//@color_target`, `//@depth`,
  `//@cull`) are removed from WGSL files — those facts now come from
  `ShaderDescriptor::state` and the codegen, not from comments.

**Phase E: Cleanup.**
- Delete the now-unused builder methods on `WebGPUShaderFactory`
  (`addBindGroup`, `addUniform`, `addColorTarget` ...).
- Update SKILL.md to point at this spec.
- Mark task #10 (canonical bind-group migration) done — every shader is on
  the convention by virtue of using generated bindings.

## 15. Migration from Phase 0+1 Reflection Work

What's kept:
- `WgslLexer` — needed for the include resolver's tokenisation pass and for
  the validator.
- `WgslReflector` (struct + bind-group parsing) — becomes the validator
  (§13).
- The canonical-group constants in `ShaderReflection.h` (`CANONICAL_GROUP_FRAME`
  etc.) — moved to a shared header used by codegen too.
- `examples/reflection_test/` — repurposed as a validator regression test.

What's archived / removed:
- `//@bind_group`, `//@color_target`, `//@depth`, `//@cull` annotations in
  WGSL — Phase D.
- The `ShaderRegistry::createXShader()` builder chains — Phase C.
- The known-struct fallback table inside the reflector — codegen makes the
  fallback unreachable; the validator only ever sees fully-emitted bindings.

What replaces what:
- "WGSL reflection runs at load and produces engine state" →
  "C++ descriptor produces WGSL + engine state; reflector is a post-check."
- "Drop in a WGSL file + register a name" →
  "Drop in a WGSL file that `#include`s the generated bindings the engine
  already knows about; register a `ShaderDescriptor`."
- "Material setters by name from reflection" →
  "Material setters by name from the same `StructDescriptor` the codegen
  used to emit the WGSL." (Same UX; different source.)
