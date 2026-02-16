// Tutorial 4: Post-Processing Shader
// Applies screen-space effects to the rendered scene texture.

// Bind Group 0: Input texture from previous render pass
@group(0) @binding(0) var inputSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;

// Vertex shader output / Fragment shader input
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
}

/**
 * Vertex shader: Generate fullscreen triangle without vertex buffers.
 * 
 * Uses vertex index to procedurally generate positions:
 * - Vertex 0: (-1, -1) bottom-left  → UV (0, 0)
 * - Vertex 1: ( 3, -1) bottom-right → UV (1, 0) [off-screen]
 * - Vertex 2: (-1,  3) top-left     → UV (0, 1) [off-screen]
 * 
 * A single large triangle covers the entire screen more efficiently
 * than two triangles forming a quad (fewer vertices processed).
 */
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    
    // Bit manipulation to generate triangle coordinates
    // vertexIndex: 0 -> (0, 0), 1 -> (2, 0), 2 -> (0, 2)
    let x = f32((vertexIndex << 1u) & 2u);
    let y = f32(vertexIndex & 2u);
    
    // Convert to NDC: (0,0) -> (-1,1), (2,0) -> (3,1), (0,2) -> (-1,-3)
    output.position = vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    
    // Pass through texture coordinates (0 to 1 range)
    output.texCoord = vec2f(x, y);
    
    return output;
}

/**
 * Fragment shader: Apply vignette effect.
 * 
 * Darkens the edges of the screen for a cinematic look.
 * The effect is strongest at screen corners and fades toward the center.
 */
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Sample the rendered scene color
    let sceneColor = textureSample(inputTexture, inputSampler, input.texCoord);
    
    // Calculate distance from screen center (0.5, 0.5)
    let center = vec2f(0.5, 0.5);
    let dist = distance(input.texCoord, center);
    
    // Vignette parameters
    let vignetteIntensity = 0.85;  // How dark edges get (0.0 = no effect, 1.0 = black)
    let vignetteFalloff = 2.0;    // Transition sharpness (higher = sharper edge)
    
    // Calculate vignette factor (1.0 at center, approaches 0.0 at edges)
    // smoothstep creates a smooth S-curve interpolation
    let vignette = 1.0 - smoothstep(0.0, 1.0, dist * vignetteFalloff);
    
    // Mix between darkened (1.0 - intensity) and full brightness (1.0)
    let vignetteFactor = mix(1.0 - vignetteIntensity, 1.0, vignette);
    
    // Apply vignette by multiplying scene color
    let finalColor = sceneColor * vignetteFactor;
    
    return finalColor;
}
