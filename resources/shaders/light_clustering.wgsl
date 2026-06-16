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

#include "engine://core/constants_cluster.wgsl"

const MAX_LIGHTS: u32 = 5120u;

const LIGHT_TYPE_AMBIENT: u32 = 0u;
const LIGHT_TYPE_DIRECTIONAL: u32 = 1u;
const LIGHT_TYPE_POINT: u32 = 2u;
const LIGHT_TYPE_SPOT: u32 = 3u;

#include "engine://core/frame_uniforms.wgsl"

#include "engine://core/lights_buffer.wgsl"

// Atomic counter view of ClusterLightList used during compute. Same memory
// as the read-only ClusterLightList that composition reads — just a
// different WGSL view (atomic write vs. plain read).
struct ClusterCellAtomic {
    offset: u32,
    count: atomic<u32>,
}

// Compute-side Scene view at @group(1). Same SLOT as the render Scene group
// but a separate bind-group layout instance because:
//   - cluster grid is read_write storage with atomic counters (the render
//     pipeline reads from the same memory but plain `array<ClusterLightList>`);
//   - the binding set is a small subset of the 10-binding render Scene.
@group(1) @binding(0) var<storage, read> u_lights: LightsBuffer;
@group(1) @binding(1) var<storage, read_write> uClusterGrid: array<ClusterCellAtomic>;
@group(1) @binding(2) var<storage, read_write> uClusterIndices: array<u32>;

// View-space log-Z mapping. MUST match getClusterIndex() in
// deferred_composition.wgsl exactly or lights land in different Z slabs than
// the fragment lookups read. CLUSTER_Z_NEAR/FAR come from the codegen include
// above so the value lives in one place (C++ ClusterManager constants).

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
    let clip = u_frame.viewProjectionMatrix * vec4<f32>(worldPos, 1.0);
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

// Phase 1: per-cluster reset + screen-wide light append. One thread per cluster
// cell. Seeds the fixed offset, then appends every ambient / directional light
// (which affect ALL clusters) into THIS cluster's own slots.
//
// Doing the global append here — one thread per cluster, each owning its own
// cluster — replaces the old path where one thread per ambient/directional
// light looped all 10,752 clusters issuing contended atomicAdds. That serialized
// a handful of threads across the entire grid and dominated the compute cost
// (~75% of frame GPU time at high point-light counts). Now it's fully parallel
// with no cross-thread contention: each cluster thread writes only its own
// count + slots. Point / spot lights are appended afterwards in cs_assign.
@compute @workgroup_size(64)
fn cs_clear(@builtin(global_invocation_id) gid: vec3<u32>) {
    let cidx = gid.x;
    if (cidx >= CLUSTER_GRID_TOTAL) {
        return;
    }
    let base = cidx * MAX_LIGHTS_PER_CLUSTER;
    uClusterGrid[cidx].offset = base;

    var count: u32 = 0u;
    let lightTotal = min(u_lights.count, MAX_LIGHTS);
    for (var i: u32 = 0u; i < lightTotal; i = i + 1u) {
        let t = u_lights.lights[i].light_type;
        if (t == LIGHT_TYPE_AMBIENT || t == LIGHT_TYPE_DIRECTIONAL) {
            if (count < MAX_LIGHTS_PER_CLUSTER) {
                uClusterIndices[base + count] = i;
                count = count + 1u;
            }
        }
    }
    // Non-atomic store is safe: this thread is the sole writer of this cluster's
    // count until cs_assign's atomicAdds run in the next (separate) dispatch.
    atomicStore(&uClusterGrid[cidx].count, count);
}

// Phase 2: per-light, append the light's index to every cluster it overlaps.
// One thread per light.
@compute @workgroup_size(64)
fn cs_assign(@builtin(global_invocation_id) gid: vec3<u32>) {
    let lightIdx = gid.x;
    if (lightIdx >= u_lights.count || lightIdx >= MAX_LIGHTS) {
        return;
    }

    let light = u_lights.lights[lightIdx];
    let lightType = light.light_type;

    // Ambient + directional are screen-wide and were already appended to every
    // cluster in cs_clear (per-cluster, no contention). Nothing to do here.
    if (lightType == LIGHT_TYPE_AMBIENT || lightType == LIGHT_TYPE_DIRECTIONAL) {
        return;
    }

    // Point / spot: bounding-sphere-in-NDC approximation. transform[3].xyz is
    // the world-space position; range is the radius. View-space depth picks
    // the cluster Z slab, NDC radius picks the XY cluster rectangle.
    let worldPos = light.transform[3].xyz;
    let viewPos = (u_frame.viewMatrix * vec4<f32>(worldPos, 1.0)).xyz;
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
    let ndcRadius = light.range / max(abs(viewPos.z), 0.001) * u_frame.projectionMatrix[0][0];
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
