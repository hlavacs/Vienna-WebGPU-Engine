struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
}

;

@group(0) @binding(0)
var<uniform> uViewProj: mat4x4<f32>;

@group(0) @binding(1)
var<storage, read> uTransforms: array<mat4x4<f32>>;

const AXIS_LENGTH: f32 = 1.0;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VertexOut {
    let transform = uTransforms[instanceIndex];
    let base = transform[3].xyz;

    // Determine direction vector and color by axis
    var dir: vec3f;
    var color: vec3f;

    if vertexIndex < 2u {
        // X axis
        dir = transform[0].xyz;
        color = vec3f(1.0, 0.0, 0.0);
    }
    else if vertexIndex < 4u {
        // Y axis
        dir = transform[1].xyz;
        color = vec3f(0.0, 1.0, 0.0);
    }
    else {
        // Z axis
        dir = transform[2].xyz;
        color = vec3f(0.0, 0.0, 1.0);
    }

    // vertexIndex % 2 == 0 → base, == 1 → tip
    let offset = f32(vertexIndex % 2u) * AXIS_LENGTH * dir;
    let worldPos = base + offset;

    var out: VertexOut;
    out.position = uViewProj * vec4f(worldPos, 1.0);
    out.color = color;
    return out;
}

@fragment
fn fs_main(input: VertexOut) -> @location(0) vec4f {
    return vec4f(input.color, 1.0);
}
