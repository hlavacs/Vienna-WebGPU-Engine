struct VertexInput {
    @location(0) position: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) depth: f32, // pass depth to fragment
};

struct ShadowPass2DUniforms {
    lightViewProjectionMatrix: mat4x4f,
};

struct ObjectUniforms {
    modelMatrix: mat4x4f,
    normalMatrix: mat4x4f,
};

@group(0) @binding(0)
var<uniform> uShadow: ShadowPass2DUniforms;
@group(1) @binding(0)
var<uniform> uObject: ObjectUniforms;

@vertex
fn vs_shadow(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = uObject.modelMatrix * vec4f(in.position, 1.0);
    let clipPos = uShadow.lightViewProjectionMatrix * worldPos;
    let ndc = clipPos.xyz / clipPos.w;
    out.position = clipPos;
    let near: f32 = 0.1;
    let far: f32 = 100.0;
    let viewDepth = clipPos.z; // in view space
    let viewPos = uObject.modelMatrix * vec4f(in.position, 1.0);
    
    let ndcZ = clipPos.z / clipPos.w;
    out.depth = ndcZ * 0.5 + 0.5;
    return out;
}

@fragment
fn fs_shadow(in: VertexOutput) -> @location(0) vec4f {
    // Write unbiased depth as grayscale
    let d = in.depth;
    return vec4f(d, d, d, 1.0);
}
