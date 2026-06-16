// Depth visualisation blit — one-shot pass that turns the G-buffer's
// Depth32Float attachment into a sampler-friendly RGBA8Unorm preview
// texture for the debug UI.
//
// Why this exists: ImGui's wgpu backend uses a single bind-group layout for
// every ImGui::Image draw, declared as `sample_type = Float { filterable:
// true }`. A Depth32Float view has `sample_type = Depth`, which is a
// distinct sample type — not just an unfilterable Float — so it can never
// pass that binding-layout check. The renderer side fixes this by
// blitting the depth into an Unorm intermediate that ImGui can sample
// without complaint.
//
// @standalone-shader — built via a one-shot pipeline in
// MainDemoImGuiUI.cpp; opts out of the engine's
// Frame/Scene/Material/Object 0..3 convention and binds its lone depth
// input at @group(0) @binding(0).

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0)       uv:       vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
    // Standard fullscreen-triangle trick. uv is (0..1).
    let uv = vec2<f32>(
        f32((vid << 1u) & 2u),
        f32(vid & 2u)
    );
    var out: VertexOutput;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    out.uv = uv;
    return out;
}

@group(0) @binding(0) var depthTex: texture_depth_2d;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Integer texel fetch via textureLoad — no sampler needed (a depth
    // texture has no compatible sampler in this shader's layout), which is
    // the whole point of this pass.
    let dims = vec2<f32>(textureDimensions(depthTex, 0));
    // ImGui samples preview thumbnails y-down; the source depth is y-down too
    // (clip.xy in [-1, 1] maps to screen [0, h] with y growing downward),
    // so we keep uv as-is.
    let coord = vec2<i32>(in.uv * dims);
    let depth = textureLoad(depthTex, coord, 0);

    // Engine convention: standard depth (not reverse-Z). depth in [0, 1]
    // with 0 = near, 1 = far. A typical scene packs most fragments into
    // the 0.95..1.0 range due to perspective Z, so a plain `1 - depth`
    // remap reads almost entirely black. Stretch the high-depth range
    // out by raising depth to a large power before inverting: at
    // depth=0.99 the output is ~0.92 (bright), at depth=0.999 it's
    // ~0.22 (mid), at depth=1.0 (sky / no geometry) it's 0 (black).
    // Pure debug-visualisation choice — not metrically linear depth.
    let g = clamp(1.0 - pow(depth, 256.0), 0.0, 1.0);
    return vec4<f32>(g, g, g, 1.0);
}
