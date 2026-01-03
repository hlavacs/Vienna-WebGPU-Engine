// Fullscreen quad shader for compositing camera textures

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    
    // Generate fullscreen triangle coordinates
    // Triangle covers entire screen: (-1,-1) to (3,3)
    let x = f32((vertexIndex & 1u) << 2u) - 1.0;
    let y = f32((vertexIndex & 2u) << 1u) - 1.0;
    
    output.position = vec4f(x, y, 0.0, 1.0);
    output.texCoord = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    
    return output;
}

@group(0) @binding(0) var cameraTexture: texture_2d<f32>;
@group(0) @binding(1) var cameraSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(cameraTexture, cameraSampler, input.texCoord);
}
