// Cluster index math shared by the deferred composition (opaque) and the
// forward PBR (transparent) paths, so both map a fragment to the SAME froxel
// and therefore see the same per-cluster light list. MUST match
// light_clustering.wgsl: `viewDepth` is VIEW-SPACE depth in world units (not
// NDC depth) and is distributed across the Z slabs with a log-Z mapping; `uv`
// is the screen UV in the compute shader's NDC convention (+Y up).
//
// Pure index math (no scene/light types), so it's safe to share in lib. The
// cluster *bindings* (u_cluster_grid, u_cluster_light_indices) live in the
// canonical Scene group and are looped by the consuming shaders directly.

#include "engine://core/constants_cluster.wgsl"

// Upper bound on the global light count — guards the per-cluster light
// loop against a corrupt/overflowed index. Mirrors light_clustering.wgsl's
// own MAX_LIGHTS on the compute side (kept in sync by hand).
const MAX_LIGHTS: u32 = 5120u;

fn getClusterIndex(uv: vec2<f32>, viewDepth: f32) -> u32 {
	let gridDimX = CLUSTER_GRID_DIM_X;
	let gridDimY = CLUSTER_GRID_DIM_Y;
	let gridDimZ = CLUSTER_GRID_DIM_Z;

	let x = u32(clamp(uv.x, 0.0, 0.999999) * f32(gridDimX));
	let y = u32(clamp(uv.y, 0.0, 0.999999) * f32(gridDimY));

	let clamped = clamp(viewDepth, CLUSTER_Z_NEAR, CLUSTER_Z_FAR);
	let normalized = log(clamped / CLUSTER_Z_NEAR) / log(CLUSTER_Z_FAR / CLUSTER_Z_NEAR);
	let z = u32(clamp(normalized, 0.0, 0.999999) * f32(gridDimZ));

	return z * (gridDimX * gridDimY) + y * gridDimX + x;
}
