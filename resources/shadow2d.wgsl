struct VertexInput {
    @location(0) position: vec3f,
}

;

struct VertexOutput {
    @builtin(position) position: vec4f,
}

;

struct ShadowUniforms {
    lightViewProjectionMatrix: mat4x4f
}

;

@group(0) @binding(0)
var<uniform> uShadow: ShadowUniforms;

@vertex
fn vs_shadow(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    // Directly transform to light clip space
    out.position = uShadow.lightViewProjectionMatrix * vec4f(in.position, 1.0);
    return out;
}

@fragment
fn fs_shadow() {
    // Depth is automatically written; no color output needed
}
