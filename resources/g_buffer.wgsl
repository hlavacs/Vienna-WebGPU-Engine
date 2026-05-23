// G-Buffer Shader for Deferred Rendering
// Outputs: Position, Normal, Albedo, Material properties

// Vertex input
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) tangent: vec4<f32>,
}

// Vertex output
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) worldNormal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) worldTangent: vec3<f32>,
    @location(4) worldBitangent: vec3<f32>,
}

// Uniforms
struct FrameUniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>,
    viewProjectionMatrix: mat4x4<f32>,
    cameraWorldPosition: vec3<f32>,
    time: f32,
}

struct ObjectUniforms {
    modelMatrix: mat4x4<f32>,
    modelInvTranspose: mat4x4<f32>,
}

// Material uniforms
struct PBRProperties {
    diffuse: vec4<f32>,
    emission: vec4<f32>,
    transmittance: vec4<f32>,
    ambient: vec4<f32>,
    roughness: f32,
    metallic: f32,
    ior: f32,
    normalTextureScale: f32,
}

// Bind groups
@group(0) @binding(0)
var<uniform> u_frame: FrameUniforms;

@group(1) @binding(0)
var<uniform> u_object: ObjectUniforms;

@group(2) @binding(0)
var<uniform> u_material: PBRProperties;
@group(2) @binding(1)
var texture_sampler: sampler;
@group(2) @binding(2)
var base_color_texture: texture_2d<f32>;
@group(2) @binding(3)
var normal_texture: texture_2d<f32>;
@group(2) @binding(4)
var roughness_texture: texture_2d<f32>;
@group(2) @binding(5)
var metallic_texture: texture_2d<f32>;
@group(2) @binding(6)
var ao_texture: texture_2d<f32>;

// Fragment output (4 render targets)
struct FragmentOutput {
    @location(0) position: vec4<f32>,
    @location(1) normal: vec4<f32>,
    @location(2) albedo: vec4<f32>,
    @location(3) material: vec4<f32>,
}

// Vertex shader
@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    
    let worldPos = (u_object.modelMatrix * vec4<f32>(input.position, 1.0)).xyz;
    output.worldPos = worldPos;
    
    let viewPos = u_frame.viewMatrix * vec4<f32>(worldPos, 1.0);
    output.position = u_frame.projectionMatrix * viewPos;
    
    output.worldNormal = normalize((u_object.modelInvTranspose * vec4<f32>(input.normal, 0.0)).xyz);
    output.texCoord = input.texCoord;
    
    // Tangent-space basis for normal mapping
    output.worldTangent = normalize((u_object.modelMatrix * vec4<f32>(input.tangent.xyz, 0.0)).xyz);
    output.worldBitangent = cross(output.worldNormal, output.worldTangent) * input.tangent.w;
    
    return output;
}

// Fragment shader - outputs to G-buffers
@fragment
fn fs_main(input: VertexOutput) -> FragmentOutput {
    var output: FragmentOutput;
    
    // Sample textures
    let baseColor = textureSample(base_color_texture, texture_sampler, input.texCoord);
    let alpha = baseColor.a * u_material.diffuse.a;
    if (alpha < 0.5) {
        discard;
    }
    let normalSample = textureSample(normal_texture, texture_sampler, input.texCoord);
    let roughnessSample = textureSample(roughness_texture, texture_sampler, input.texCoord);
    let metallicSample = textureSample(metallic_texture, texture_sampler, input.texCoord);
    let aoSample = textureSample(ao_texture, texture_sampler, input.texCoord);
    
    // Decode normal map (tangent space)
    let normalTS = normalize(normalSample.rgb * 2.0 - 1.0) * u_material.normalTextureScale;
    
    // Convert to world space
    let TBN = mat3x3<f32>(
        input.worldTangent,
        input.worldBitangent,
        input.worldNormal
    );
    let worldNormal = normalize(TBN * normalTS);
    
    // Get material properties
    let roughness = roughnessSample.r * u_material.roughness;
    let metallic = metallicSample.r * u_material.metallic;
    let ao = aoSample.r;
    
    // Calculate depth from view space for clustering
    let viewPos = u_frame.viewMatrix * vec4<f32>(input.worldPos, 1.0);
    let depth = -viewPos.z; // Positive depth in view space
    
    // Output G-buffers
    output.position = vec4<f32>(input.worldPos, depth);
    output.normal = vec4<f32>(worldNormal, depth);
    output.albedo = vec4<f32>(baseColor.rgb * u_material.diffuse.rgb, alpha);
    output.material = vec4<f32>(roughness, metallic, ao, 1.0);
    
    return output;
}
