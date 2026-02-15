// Debug primitive types
const PRIM_LINE: u32 = 0u;
const PRIM_DISK: u32 = 1u;
const PRIM_AABB: u32 = 2u;
const PRIM_ARROW: u32 = 3u;

// Frame uniforms (matches FrameUniforms.h)
struct FrameUniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>,
    viewProjectionMatrix: mat4x4<f32>,
    cameraPosition: vec3<f32>,
    time: f32,
}

// Debug primitive structure (matches DebugCollector.h - 80 bytes total)
struct DebugPrimitive {
    padding1: vec3<f32>,
    primitiveType: u32,
    color: vec4<f32>,
    // Union data - 48 bytes (3 vec4s)
    data0: vec4<f32>,
    data1: vec4<f32>,
    data2: vec4<f32>,
}

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
}

@group(0) @binding(0)
var<uniform> uFrame: FrameUniforms;

@group(1) @binding(0)
var<storage, read> uDebugPrimitives: array<DebugPrimitive>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VertexOut {
    let primitive = uDebugPrimitives[instanceIndex];

    var worldPos: vec3<f32>;
    var color = primitive.color;

    if primitive.primitiveType == PRIM_LINE {
        // data0.xyz = start, data1.xyz = end
        let start = primitive.data0.xyz;
        let end = primitive.data1.xyz;

        if vertexIndex == 0u {
            worldPos = start;
        }
        else {
            worldPos = end;
        }
    }
    else if primitive.primitiveType == PRIM_DISK {
        // data0.xyz = center, data1.xyz = radii
        // Radii indicates which axes to use for the circle
        // e.g., radii = (r, 0, r) means circle in XZ plane
        //       radii = (r, r, 0) means circle in XY plane
        //       radii = (0, r, r) means circle in YZ plane
        let center = primitive.data0.xyz;
        let radii = primitive.data1.xyz;
        let segments = 32u;
        let angle = f32(vertexIndex) / f32(segments) * 6.28318530718; // 2*PI
        
        let cosA = cos(angle);
        let sinA = sin(angle);
        
        // Generate circle based on which radii are non-zero
        let offset = vec3<f32>(
            cosA * radii.x + sinA * radii.x * 0.0,  // X component
            cosA * radii.y + sinA * radii.y * 0.0,  // Y component
            sinA * radii.z + cosA * radii.z * 0.0   // Z component
        );
        
        // More explicit plane selection
        var circleOffset: vec3<f32>;
        if radii.x > 0.0 && radii.z > 0.0 && radii.y == 0.0 {
            // XZ plane (horizontal)
            circleOffset = vec3<f32>(cosA * radii.x, 0.0, sinA * radii.z);
        } else if radii.x > 0.0 && radii.y > 0.0 && radii.z == 0.0 {
            // XY plane (vertical, facing forward)
            circleOffset = vec3<f32>(cosA * radii.x, sinA * radii.y, 0.0);
        } else if radii.y > 0.0 && radii.z > 0.0 && radii.x == 0.0 {
            // YZ plane (vertical, facing right)
            circleOffset = vec3<f32>(0.0, cosA * radii.y, sinA * radii.z);
        } else {
            // Fallback to XZ plane
            circleOffset = vec3<f32>(cosA * radii.x, 0.0, sinA * radii.z);
        }
        
        worldPos = center + circleOffset;
    }
    else if primitive.primitiveType == PRIM_AABB {
        // data0.xyz = min, data1.xyz = max
        // Draw wireframe box (12 edges, 24 vertices for LineList)
        let minPos = primitive.data0.xyz;
        let maxPos = primitive.data1.xyz;

        // Define 8 corners of the box
        var corners: array<vec3<f32>, 8>;
        corners[0] = vec3<f32>(minPos.x, minPos.y, minPos.z);
        corners[1] = vec3<f32>(maxPos.x, minPos.y, minPos.z);
        corners[2] = vec3<f32>(maxPos.x, maxPos.y, minPos.z);
        corners[3] = vec3<f32>(minPos.x, maxPos.y, minPos.z);
        corners[4] = vec3<f32>(minPos.x, minPos.y, maxPos.z);
        corners[5] = vec3<f32>(maxPos.x, minPos.y, maxPos.z);
        corners[6] = vec3<f32>(maxPos.x, maxPos.y, maxPos.z);
        corners[7] = vec3<f32>(minPos.x, maxPos.y, maxPos.z);

        let idx = vertexIndex % 24u;
        if idx < 8u {
            // bottom face edges
            let edgeStart = idx;
            let edgeEnd = (idx + 1u) % 4u;
            worldPos = corners[edgeStart];
        }
        else if idx < 16u {
            // top face edges
            let edgeStart = (idx % 8u) % 4u + 4u;
            let edgeEnd = ((edgeStart - 4u + 1u) % 4u) + 4u;
            worldPos = corners[edgeStart];
        }
        else {
            // vertical edges connecting top & bottom
            let i = idx - 16u;
            let edgeStart = i % 4u;
            let edgeEnd = edgeStart + 4u;
            worldPos = corners[edgeStart];
        }

    }
    else if primitive.primitiveType == PRIM_ARROW {
        // data0.xyz = from, data1.xyz = to, data2.x = headSize
        let startPos = primitive.data0.xyz;
        let endPos = primitive.data1.xyz;
        let headLength = primitive.data2.x;

        let dir = normalize(endPos - startPos);
        
        // Create perpendicular vectors for arrow head
        var right: vec3<f32>;
        if abs(dir.y) < 0.999 {
            right = normalize(cross(dir, vec3<f32>(0.0, 1.0, 0.0)));
        } else {
            right = normalize(cross(dir, vec3<f32>(1.0, 0.0, 0.0)));
        }
        let up = normalize(cross(right, dir));
        
        let headBase = endPos - dir * headLength;
        let headWidth = headLength * 0.5;

        // Arrow consists of:
        // - Main shaft (2 vertices)
        // - 4 head lines (8 vertices)
        if vertexIndex < 2u {
            // Main shaft
            if vertexIndex == 0u {
                worldPos = startPos;
            } else {
                worldPos = endPos;
            }
        } else {
            // Arrow head (4 lines from tip to base in cross pattern)
            let headVertex = vertexIndex - 2u;
            let headLine = headVertex / 2u;
            let vertInLine = headVertex % 2u;
            
            if vertInLine == 0u {
                // Tip of arrow
                worldPos = endPos;
            } else {
                // Base of arrow head
                if headLine == 0u {
                    worldPos = headBase + right * headWidth;
                } else if headLine == 1u {
                    worldPos = headBase - right * headWidth;
                } else if headLine == 2u {
                    worldPos = headBase + up * headWidth;
                } else {
                    worldPos = headBase - up * headWidth;
                }
            }
        }
    }

    var out: VertexOut;
    out.position = uFrame.viewProjectionMatrix * vec4<f32>(worldPos, 1.0);
    out.color = color;
    return out;
}

@fragment
fn fs_main(input: VertexOut) -> @location(0) vec4<f32> {
    return input.color;
}
