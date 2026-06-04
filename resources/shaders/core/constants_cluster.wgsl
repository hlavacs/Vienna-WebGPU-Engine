// AUTO-GENERATED from ClusterManager (C++ constants). Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

const CLUSTER_GRID_DIM_X: u32 = 24u;
const CLUSTER_GRID_DIM_Y: u32 = 14u;
const CLUSTER_GRID_DIM_Z: u32 = 32u;
const CLUSTER_GRID_TOTAL: u32 = CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y * CLUSTER_GRID_DIM_Z;
const MAX_LIGHTS_PER_CLUSTER: u32 = 256u;

// View-space z extents for the log-Z cluster mapping. Kept here so
// compute + composition stay in lockstep; if a CameraNode ever needs
// per-camera extents this becomes a per-pipeline override.
const CLUSTER_Z_NEAR: f32 = 0.1;
const CLUSTER_Z_FAR:  f32 = 1000.0;
