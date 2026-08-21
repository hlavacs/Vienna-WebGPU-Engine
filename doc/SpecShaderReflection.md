# Spec: Shader Reflection — SUPERSEDED

> The reflection-first direction is no longer the plan. The shader system is
> now **C++-struct-driven codegen** — C++ descriptors are the single source of
> truth, the engine emits WGSL struct + bind-group declarations into
> `resources/generated/`, hand-written shaders `#include` those headers, and
> hot reload is dependency-graph-based rather than reflective.
>
> See **[`doc/SpecShaderSystem.md`](SpecShaderSystem.md)** for the current
> authoritative reference.

## What survives from the reflection work

The WGSL lexer + parser (`src/engine/rendering/reflection/`) are not deleted.
Their role is demoted: after the codegen step has produced the final
expanded WGSL string, the parser sanity-checks it against the C++
descriptors (struct sizes / field offsets / canonical group placement). Any
mismatch fails shader load loudly. See `SpecShaderSystem.md` §13 for the
contract.

`examples/reflection_test/` is repurposed as a validator regression suite —
drop a WGSL file in, confirm the validator reports zero diagnostics when the
file matches its descriptors.

## What's archived

- The `//@bind_group`, `//@color_target`, `//@depth`, `//@cull` WGSL
  annotation conventions. Pipeline state now lives on `ShaderDescriptor`,
  not in comments.
- The known-struct fallback table inside the reflector. Codegen makes the
  fallback unreachable; the validator only ever sees fully-emitted bindings.
- The "drop a WGSL file + register a name" UX. The new UX is "drop a WGSL
  file that `#include`s the generated bindings + register a
  `ShaderDescriptor`."

## Phase status (historical)

Phases 0 and 1 of the reflection plan landed before the pivot:
the WGSL parser exists, annotations were recognised, and `reflection_test`
parsed all six engine WGSL files. None of that work is wasted — it backs
the validator (§13 in the new spec).
