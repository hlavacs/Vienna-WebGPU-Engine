struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0)
var depthTexture: texture_depth_2d_array;

@group(0) @binding(1)
var depthSampler: sampler_comparison;

@group(0) @binding(2)
var<uniform> layer: u32;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var out: VertexOutput;
    
    // Generate full-screen quad
    let x = f32((vertexIndex & 1u) << 2u) - 1.0;
    let y = f32((vertexIndex & 2u) << 1u) - 1.0;
    
    out.position = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f(x * 0.5 + 0.5, -y * 0.5 + 0.5);
    
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample depth value (0.0 = near, 1.0 = far)
    let depth = textureSample(depthTexture, depthSampler, in.uv, i32(layer), 0.5);
    
    // Convert to grayscale: white = near (small depth), black = far (large depth)
    // Note: Inverted for intuitive visualization
    let gray = 1.0 - depth;
    
    return vec4f(gray, gray, gray, 1.0);
}
