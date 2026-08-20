# Vienna-WebGPU-Engine — TODO

Living roadmap. Items are roughly ordered by dependency, not priority. See
`.claude/skills/vienna-webgpu-engine/SKILL.md` for the rules and gotchas that
guide how these should be implemented.

## Done (recent)

- HDR gamma fix — `fullscreen_quad.wgsl` now applies ACES Filmic with exposure (1.6
  default), driven by `RenderTarget::hdr`. Reinhard removed (it crushed midtones to
  ~0.5 making everything look washed-out). No double-tonemap; sRGB surface format
  handles the linear → sRGB encode on write.
- `ForwardTransparencyPass` — true alpha-blended geometry rendered after deferred
  composition + skybox, against gbuffer depth read-only, sorted back-to-front per
  camera. Driven by the Transparent material flag, which `WebGPUPipelineFactory`
  also uses to enable SrcAlpha blending and disable depth writes.
- `AlphaMode` enum + per-material `alphaCutoff` — `PBRProperties` now carries
  `alphaMode (u32)` and `alphaCutoff (f32)`, mirrored in `PBR_Lit_Shader.wgsl`
  and `g_buffer.wgsl`. Shaders branch on `alphaMode` instead of inferring intent
  from the cutoff alone. MaterialManager maps GLTF `alphaMode` 1:1.
- GBuffer hot-reload fix — removed `GBufferPass::m_pipelineCache` (held stale
  `shared_ptr<WebGPUPipeline>` that survived shader reload). Lookups now go
  through `WebGPUPipelineManager` every frame; its cache is the single source
  of truth.
- GBuffer resize fix — `CompositionPass::ensureGBufferBindGroup` fingerprints
  the first color texture's raw pointer and rebuilds the cached bind group when
  it changes. Previously only `Renderer::onResize` invalidated it, so per-frame
  resizes (multi-camera, viewport changes) left the cached bind group sampling
  destroyed views.
- Fake glass refraction removed from `PBR_Lit_Shader.wgsl` — it folded an extra
  `mix(glass_color, final_color, alpha)` on top of lit color for every BLEND
  fragment, double-counting lighting and making water / glass look bloomed.
  Forward env irradiance is also now capped at `min(raw, vec3(0.3))` to match
  `deferred_composition.wgsl` so translucent surfaces don't stack a brighter
  lighting model on the deferred-lit pixels below.
- Clustered lighting compute path (replaces the scan-all fallback). Two
  dispatches per camera: `cs_clear` zeros every cluster's count + seeds the
  fixed offset slot, `cs_assign` projects each light's bounding sphere into
  view space and atomicAdds its index into every cluster it overlaps.
  `light_clustering.wgsl` mirrors the engine's `FrameUniforms` / `LightStruct`
  exactly; the compute pipeline owns its own bind-group layout (atomic
  view of the cluster grid) while the composition pass keeps its read-only
  view of the same buffers.
- Hot-reload preserves every builder-populated field — earlier
  `WebGPUShaderFactory::reloadShader` only copied bind-group layouts, so
  reloading the GBUFFER shader silently dropped its 5 color-target formats
  and the pipeline rebuilt with a single attachment. Now also copies
  `colorTargetFormats`, `depthCompare`, `depthWriteEnabled`.
- GBuffer resize fingerprint: `CompositionPass::ensureGBufferBindGroup` rebuilds
  when the first color texture's raw pointer changes, not only on
  `setGBuffer(differentPtr)`. Per-frame in-frame resizes (multi-camera,
  viewport changes) never trigger `Renderer::onResize`, so the prior
  pointer-only check left the cached bind group sampling destroyed views.

## Roadmap (near future)

### 1. Shader system v1 — codegen-driven (C++ structs are source of truth)
Replace the WGSL-reflection-first direction. C++ structs + `StructDescriptor`
runtime descriptions are the single source of truth; the engine emits the
matching WGSL struct + canonical `@group(N) @binding(M)` declarations into
`resources/generated/`; hand-written shaders `#include` those headers and
contain logic only. Bind-group layouts come from the C++ descriptor, not
from the WGSL. Hot reload becomes dependency-graph based. The Phase 0+1
WGSL parser is demoted to a **post-codegen validator** that fails loud on
codegen / authored-WGSL drift. Full spec at `doc/SpecShaderSystem.md`;
five-phase rollout (A: codegen prototype → E: cleanup) listed there.

### 2. Full deferred clustered lighting (replace fallback scan-all)
`light_clustering.wgsl` exists but is unloaded. `ClusterManager::assignLights`
is a placeholder that leaves the cluster grid zero-initialised, so the
composition shader falls back to "iterate all 512 lights per pixel." Wire the
compute pass, sync `LightStruct`/`FrameUniforms` between WGSL and C++, drop
the fallback. Big perf win at high light counts.

### 3. Reflection support (env probes / SSR)
Specular IBL today reuses the diffuse equirect sample with no roughness-based
mip prefiltering and no screen-space fallback. Low-roughness materials look
diffuse instead of mirror-like. Add either (or both):
- Pre-convolved environment cubemap mip chain, indexed by roughness for both
  deferred composition and PBR forward.
- Screen-space reflections sampled off the lit HDR target.

### 4. Proper transmittance (KHR_materials_transmission)
Replaces the fake glass refraction that was removed. Needs: refracted-direction
sample of the lit HDR target (screen-space) and/or the env cubemap, IOR-correct
Fresnel, transmittance tint, rough refraction via mip-LOD selection. Unblocks
real glass / water / ice.

### 5. Factory overhaul
The factory inventory in SKILL.md has grown ad-hoc. Pass over every
`WebGPUContext` factory (texture / depthTexture / buffer / bindGroup / pipeline
/ renderPass / sampler / shader / material) and:
- deduplicate overloads
- unify naming (`createX` vs `getX` vs `createXWrapped` is inconsistent)
- remove dead methods
- accept typed flag enums where they don't have to be OR'd at the call site
  (current `WGPUTextureUsageFlags` raw-uint dance only exists because the
  wgpu C++ enums don't compose).
Smaller, more discoverable surface area before factory caching lands on top.

### 6. Per-factory cache + uniform hot reload — *blocked by #5*
`WebGPUPipelineManager` has a keyed cache + pending-reload pass. The other
factories (texture, buffer, bindGroup, renderPass) do not, so resources leak
on resize and shader hot-reload doesn't invalidate them. Apply the same
pattern across the board. Also: local pipeline caches inside individual passes
are explicitly forbidden — `WebGPUPipelineManager` is the only place caching
happens. (See SKILL.md "no local pipeline caches" rule.)

### 7. Shader graph (data-driven material authoring) — *blocked by #1*
Today every material binds a single hand-written WGSL shader (PBR, GBUFFER,
…). A node-graph authoring system would emit WGSL from a serialised graph,
derive bind groups via shader reflection (#1), and let new effects (vertex
displacement, custom BRDFs, post FX) ship without touching engine C++.
