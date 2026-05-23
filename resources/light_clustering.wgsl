// Light Clustering Compute Shader
// Assigns lights to 3D cluster grid cells for deferred rendering
// Grid: 24×14×32 = 10,752 clusters (depth: 32 = clusters along Z)

// Constants
const CLUSTER_GRID_DIM_X: u32 = 24u;
const CLUSTER_GRID_DIM_Y: u32 = 14u;
const CLUSTER_GRID_DIM_Z: u32 = 32u;
const CLUSTER_GRID_TOTAL: u32 = CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y * CLUSTER_GRID_DIM_Z;
const MAX_LIGHTS_PER_CLUSTER: u32 = 256u;
const MAX_LIGHTS: u32 = 512u;

// Frame uniforms (matching FrameUniforms structure)
struct FrameUniforms {
	viewMatrix: mat4x4<f32>,
	projectionMatrix: mat4x4<f32>,
	viewProjectionMatrix: mat4x4<f32>,
	inverseProjectionMatrix: mat4x4<f32>,
	cameraPosition: vec3<f32>,
	_pad0: f32,
	cameraDirection: vec3<f32>,
	_pad1: f32,
	frustumPlanes: array<vec4<f32>, 6>,
	time: f32,
	deltaTime: f32,
	zNear: f32,
	zFar: f32,
	jitterX: f32,
	jitterY: f32,
	_pad2: f32,
}

// Light structure (matching LightStruct)
struct LightStruct {
	position: vec4<f32>,		// World position + radius (w)
	direction: vec4<f32>,		// Direction + intensity (w)
	color: vec4<f32>,			// RGB + type (w: 0=point, 1=directional, 2=spot)
	shadowMapIndex: i32,		// Which shadow map (if any)
	enabled: i32,				// Is light active
	_pad0: u32,
	_pad1: u32,
}

// Lights buffer header
struct LightsBuffer {
	count: u32,
	_pad: array<u32, 3>,
}

// Cluster assignment data: contains light indices for this cluster
struct ClusterLightList {
	count: u32,
	lightIndices: array<u32, MAX_LIGHTS_PER_CLUSTER>,
}

// Bind groups
@group(0) @binding(0) var<uniform> uFrame: FrameUniforms;
@group(0) @binding(1) var<storage, read> uLights: array<LightStruct>;
@group(0) @binding(2) var<uniform> uLightsHeader: LightsBuffer;
@group(0) @binding(3) var<storage, read_write> uClusterGrid: array<ClusterLightList>;

// Helper: Convert cluster grid 3D index to 1D
fn clusterIndex3DTo1D(x: u32, y: u32, z: u32) -> u32 {
	return z * (CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y) + y * CLUSTER_GRID_DIM_X + x;
}

// Helper: Convert 1D cluster index to 3D
fn clusterIndex1DTo3D(idx: u32) -> vec3<u32> {
	let z = idx / (CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y);
	let remainder = idx % (CLUSTER_GRID_DIM_X * CLUSTER_GRID_DIM_Y);
	let y = remainder / CLUSTER_GRID_DIM_X;
	let x = remainder % CLUSTER_GRID_DIM_X;
	return vec3<u32>(x, y, z);
}

// Helper: Convert normalized screen coordinates to cluster XY
fn screenToClusterXY(ndc: vec2<f32>) -> vec2<u32> {
	// NDC is [-1, 1], convert to [0, 1]
	let uv = (ndc + vec2<f32>(1.0)) * 0.5;
	// Clamp to valid range
	let clamped = clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0));
	// Convert to cluster grid coordinates
	let x = u32(clamped.x * f32(CLUSTER_GRID_DIM_X));
	let y = u32(clamped.y * f32(CLUSTER_GRID_DIM_Y));
	return vec2<u32>(min(x, CLUSTER_GRID_DIM_X - 1u), min(y, CLUSTER_GRID_DIM_Y - 1u));
}

// Helper: Convert view-space depth to cluster Z
// z is in [0, 1] from NDC depth (0 = near plane, 1 = far plane)
fn depthToClusterZ(depth01: f32) -> u32 {
	// Logarithmic distribution of clusters in depth
	let logZ = log(mix(0.1, 1.0, depth01)); // Use log scale for better resolution near camera
	let normalized = (logZ - log(0.1)) / (log(1.0) - log(0.1));
	let z = u32(clamp(normalized, 0.0, 1.0) * f32(CLUSTER_GRID_DIM_Z - 1u));
	return z;
}

// Main compute kernel
// Each workgroup processes one light
@compute @workgroup_size(256, 1, 1)
fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
	let lightIdx = global_id.x;
	
	// Bounds check
	if (lightIdx >= uLightsHeader.count || lightIdx >= MAX_LIGHTS) {
		return;
	}
	
	let light = uLights[lightIdx];
	
	// Skip disabled lights
	if (light.enabled == 0) {
		return;
	}
	
	let lightType = u32(light.color.w);
	let lightRadius = light.position.w;
	
	if (lightType == 0u) {
		// Point light: assign to all clusters within radius
		let lightPosWorld = light.position.xyz;
		
		// Convert to view space for clustering
		let lightPosView = (uFrame.viewMatrix * vec4<f32>(lightPosWorld, 1.0)).xyz;
		
		// Project to NDC
		let lightPosNDC = (uFrame.projectionMatrix * vec4<f32>(lightPosView, 1.0));
		let lightPosNDCDiv = lightPosNDC.xyz / lightPosNDC.w;
		
		// Get center cluster
		let centerXY = screenToClusterXY(lightPosNDC.xy);
		let centerZ = depthToClusterZ((lightPosNDCDiv.z + 1.0) * 0.5);
		
		// Determine cluster range affected by light radius
		// Estimate screen-space radius (simplified - doesn't account for perspective distortion perfectly)
		let radiusNDCApprox = lightRadius / abs(lightPosView.z) * (uFrame.projectionMatrix[0][0]);
		let radiusClusterCells = u32(max(1u, u32(radiusNDCApprox * f32(CLUSTER_GRID_DIM_X))));
		
		// Iterate through affected clusters
		let minX = max(0i32, i32(centerXY.x) - i32(radiusClusterCells));
		let maxX = min(i32(CLUSTER_GRID_DIM_X - 1u), i32(centerXY.x) + i32(radiusClusterCells));
		let minY = max(0i32, i32(centerXY.y) - i32(radiusClusterCells));
		let maxY = min(i32(CLUSTER_GRID_DIM_Y - 1u), i32(centerXY.y) + i32(radiusClusterCells));
		let minZ = max(0i32, i32(centerZ) - i32(radiusClusterCells));
		let maxZ = min(i32(CLUSTER_GRID_DIM_Z - 1u), i32(centerZ) + i32(radiusClusterCells));
		
		for (var x: i32 = minX; x <= maxX; x++) {
			for (var y: i32 = minY; y <= maxY; y++) {
				for (var z: i32 = minZ; z <= maxZ; z++) {
					let clusterIdx = clusterIndex3DTo1D(u32(x), u32(y), u32(z));
					
					// Atomically add light to cluster
					let count = atomicLoad(&uClusterGrid[clusterIdx].count);
					if (count < MAX_LIGHTS_PER_CLUSTER) {
						let newIdx = atomicAdd(&uClusterGrid[clusterIdx].count, 1u);
						if (newIdx < MAX_LIGHTS_PER_CLUSTER) {
							uClusterGrid[clusterIdx].lightIndices[newIdx] = lightIdx;
						}
					}
				}
			}
		}
	} else if (lightType == 1u) {
		// Directional light: affects all clusters
		for (var i: u32 = 0u; i < CLUSTER_GRID_TOTAL; i++) {
			let count = atomicLoad(&uClusterGrid[i].count);
			if (count < MAX_LIGHTS_PER_CLUSTER) {
				let newIdx = atomicAdd(&uClusterGrid[i].count, 1u);
				if (newIdx < MAX_LIGHTS_PER_CLUSTER) {
					uClusterGrid[i].lightIndices[newIdx] = lightIdx;
				}
			}
		}
	}
	// Spot lights (type 2) can be added similarly, treating them like points for now
}
