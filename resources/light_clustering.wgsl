// Light clustering compute shader.
//
// Layout MUST match the composition shader's reads in deferred_composition.wgsl:
//   uClusterGrid    : array<{offset: u32, count: u32}>, one per cluster
//   uClusterIndices : flat u32 pool, each cluster owns MAX_LIGHTS_PER_CLUSTER
//                     contiguous slots starting at offset = clusterIdx * 256.
//
// The composition shader treats `count` as a plain u32; we view it as
// atomic<u32> here so multiple threads can append to the same cluster safely.
// Same buffer memory; just a different WGSL view.

const CLUSTER_GRID_DIM_X: u32 = 24u;
const CLUSTER_GRID_DIM_Y: u32 = 14u;
const CLUSTER_GRID_DIM_Z: u32 = 32u;
const CLUSTER_GRID_TOTAL: u32 = CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y * CLUSTER_GRID_DIM_Z;
const MAX_LIGHTS_PER_CLUSTER: u32 = 256u;
const MAX_LIGHTS: u32 = 5120u;

const LIGHT_TYPE_AMBIENT: u32 = 0u;
const LIGHT_TYPE_DIRECTIONAL: u32 = 1u;
const LIGHT_TYPE_POINT: u32 = 2u;
const LIGHT_TYPE_SPOT: u32 = 3u;

// Mirrors engine::rendering::FrameUniforms exactly.
struct FrameUniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>,
    viewProjectionMatrix: mat4x4<f32>,
    cameraWorldPosition: vec3<f32>,
    time: f32,
}

// Mirrors engine::rendering::LightStruct exactly (transform + scalars, 112 B).
struct LightStruct {
    transform: mat4x4<f32>,
    color: vec3<f32>,
    intensity: f32,
    lightType: u32,
    spotAngle: f32,
    spotSoftness: f32,
    range: f32,
    shadowIndex: u32,
    shadowCount: u32,
    _pad1: f32,
    _pad2: f32,
}

// Mirrors LightsBuffer in deferred_composition.wgsl (header + runtime array).
struct LightsBuffer {
    count: u32,
    _pad: array<u32, 3>,
    lights: array<LightStruct>,
}

struct ClusterCellAtomic {
    offset: u32,
    count: atomic<u32>,
}

@group(0) @binding(0) var<uniform> uFrame: FrameUniforms;
@group(0) @binding(1) var<storage, read> uLights: LightsBuffer;
@group(0) @binding(2) var<storage, read_write> uClusterGrid: array<ClusterCellAtomic>;
@group(0) @binding(3) var<storage, read_write> uClusterIndices: array<u32>;

// View-space log-Z mapping. MUST match getClusterIndex() in
// deferred_composition.wgsl exactly or lights land in different Z slabs than
// the fragment lookups read. Constants must match too.
const CLUSTER_Z_NEAR: f32 = 0.1;
const CLUSTER_Z_FAR:  f32 = 1000.0;

fn cluster3dTo1d(x: u32, y: u32, z: u32) -> u32 {
    return z * (CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y) + y * CLUSTER_GRID_DIM_X + x;
}

fn viewDepthToClusterZ(viewDepth: f32) -> u32 {
    let clamped = clamp(viewDepth, CLUSTER_Z_NEAR, CLUSTER_Z_FAR);
    let normalized = log(clamped / CLUSTER_Z_NEAR) / log(CLUSTER_Z_FAR / CLUSTER_Z_NEAR);
    return u32(clamp(normalized, 0.0, 0.999999) * f32(CLUSTER_GRID_DIM_Z));
}

// Project a world-space point to NDC, returning xy in [-1, 1] and z in [0, 1].
// w-divide guards against the point being at / behind the camera.
struct NdcPos {
    xy: vec2<f32>,
    depth01: f32,
    valid: bool,
}

fn worldToNdc(worldPos: vec3<f32>) -> NdcPos {
    var out: NdcPos;
    let clip = uFrame.viewProjectionMatrix * vec4<f32>(worldPos, 1.0);
    if (clip.w <= 0.0001) {
        out.xy = vec2<f32>(0.0);
        out.depth01 = 0.0;
        out.valid = false;
        return out;
    }
    let ndc = clip.xyz / clip.w;
    out.xy = ndc.xy;
    out.depth01 = (ndc.z + 1.0) * 0.5;
    out.valid = true;
    return out;
}

fn ndcXyToClusterXY(ndcXy: vec2<f32>) -> vec2<u32> {
    let uv = clamp((ndcXy + vec2<f32>(1.0)) * 0.5, vec2<f32>(0.0), vec2<f32>(1.0));
    let x = min(u32(uv.x * f32(CLUSTER_GRID_DIM_X)), CLUSTER_GRID_DIM_X - 1u);
    let y = min(u32(uv.y * f32(CLUSTER_GRID_DIM_Y)), CLUSTER_GRID_DIM_Y - 1u);
    return vec2<u32>(x, y);
}

// Phase 1: zero-out each cluster's count and seed its fixed offset slot.
// One thread per cluster cell.
@compute @workgroup_size(64)
fn cs_clear(@builtin(global_invocation_id) gid: vec3<u32>) {
    let cidx = gid.x;
    if (cidx >= CLUSTER_GRID_TOTAL) {
        return;
    }
    uClusterGrid[cidx].offset = cidx * MAX_LIGHTS_PER_CLUSTER;
    atomicStore(&uClusterGrid[cidx].count, 0u);
}

// Phase 2: per-light, append the light's index to every cluster it overlaps.
// One thread per light.
@compute @workgroup_size(64)
fn cs_assign(@builtin(global_invocation_id) gid: vec3<u32>) {
    let lightIdx = gid.x;
    if (lightIdx >= uLights.count || lightIdx >= MAX_LIGHTS) {
        return;
    }

    let light = uLights.lights[lightIdx];
    let lightType = light.lightType;

    // Ambient + directional are screen-wide: append to every cluster so the
    // composition shader (which no longer has a scan-all fallback) still sees
    // them.
    if (lightType == LIGHT_TYPE_AMBIENT || lightType == LIGHT_TYPE_DIRECTIONAL) {
        for (var i: u32 = 0u; i < CLUSTER_GRID_TOTAL; i = i + 1u) {
            let slot = atomicAdd(&uClusterGrid[i].count, 1u);
            if (slot < MAX_LIGHTS_PER_CLUSTER) {
                uClusterIndices[i * MAX_LIGHTS_PER_CLUSTER + slot] = lightIdx;
            }
        }
        return;
    }

    // Point / spot: bounding-sphere-in-NDC approximation. transform[3].xyz is
    // the world-space position; range is the radius. View-space depth picks
    // the cluster Z slab, NDC radius picks the XY cluster rectangle.
    let worldPos = light.transform[3].xyz;
    let viewPos = (uFrame.viewMatrix * vec4<f32>(worldPos, 1.0)).xyz;
    let viewDepth = -viewPos.z; // engine convention: -Z is forward

    // Skip lights fully behind the camera (cheap early-out before the more
    // expensive cluster iteration).
    if (viewDepth + light.range <= 0.0) {
        return;
    }

    let centerNdc = worldToNdc(worldPos);
    if (!centerNdc.valid) {
        return;
    }

    let centerXY = ndcXyToClusterXY(centerNdc.xy);

    // Depth slab in view space directly - matches the composition shader's
    // viewDepth-based lookup. No NDC roundtrip; perspective concentrates 99%
    // of NDC depth in [0.99, 1.0] and collapses every light into the last
    // slab if we go through clip space.
    let nearViewZ = max(viewDepth - light.range, CLUSTER_Z_NEAR);
    let farViewZ  = max(viewDepth + light.range, CLUSTER_Z_NEAR);
    let zMinCluster = viewDepthToClusterZ(nearViewZ);
    let zMaxCluster = viewDepthToClusterZ(farViewZ);

    // Screen-space radius in NDC: range / |z| * focal_length. The focal-length
    // proxy is projectionMatrix[0][0] (2 * near / (right - left)) for a
    // standard perspective. Conservative on the high side.
    let ndcRadius = light.range / max(abs(viewPos.z), 0.001) * uFrame.projectionMatrix[0][0];
    let cellsX = max(1u, u32(ceil(ndcRadius * 0.5 * f32(CLUSTER_GRID_DIM_X))));
    let cellsY = max(1u, u32(ceil(ndcRadius * 0.5 * f32(CLUSTER_GRID_DIM_Y))));

    let xMin = u32(max(0, i32(centerXY.x) - i32(cellsX)));
    let xMax = min(CLUSTER_GRID_DIM_X - 1u, centerXY.x + cellsX);
    let yMin = u32(max(0, i32(centerXY.y) - i32(cellsY)));
    let yMax = min(CLUSTER_GRID_DIM_Y - 1u, centerXY.y + cellsY);
    let zMin = zMinCluster;
    let zMax = zMaxCluster;

    for (var z: u32 = zMin; z <= zMax; z = z + 1u) {
        for (var y: u32 = yMin; y <= yMax; y = y + 1u) {
            for (var x: u32 = xMin; x <= xMax; x = x + 1u) {
                let cidx = cluster3dTo1d(x, y, z);
                let slot = atomicAdd(&uClusterGrid[cidx].count, 1u);
                if (slot < MAX_LIGHTS_PER_CLUSTER) {
                    uClusterIndices[cidx * MAX_LIGHTS_PER_CLUSTER + slot] = lightIdx;
                }
            }
        }
    }
}
