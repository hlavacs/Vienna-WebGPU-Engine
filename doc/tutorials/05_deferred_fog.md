# Tutorial 05: Fog in the Deferred Renderer

> **Status:** Planned - this tutorial is not written yet.

This tutorial will add **distance and height fog** to the engine as a real, reusable
feature - and use it to teach how the deferred pipeline actually works.

**Planned topics:**
- What the G-buffer stores and why the engine reconstructs world position from depth
  (`inverseViewProjectionMatrix` in the Frame uniforms, 32-byte MRT budget)
- How the composition pass consumes per-pixel deferred data
- Extending `EnvironmentUniforms` (Scene group) with fog parameters: color, density,
  height falloff
- Implementing the fog blend in `Composition_Deferred.wgsl`, including the horizon /
  skybox interaction at far depth
- Plumbing the parameters CPU-side so fog is configurable per scene

**Until then:**
- Tutorial 04 teaches the render-pass side of the engine: [04_postprocessing.md](04_postprocessing.md)
- The deferred pass order lives in `Renderer::buildPerCameraGraph()`
  (Shadow -> GBuffer -> ClusterCompute -> Composition -> Skybox -> ForwardTransparency -> Debug)
