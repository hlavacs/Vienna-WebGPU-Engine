// Mipmap generation shader - blits from source texture to render target with linear filtering

@group(0) @binding(0) var srcTexture: texture_2d<f32>;
@group(0) @binding(1) var srcSampler: sampler;

struct VertexOutput {
	@builtin(position) position: vec4<f32>,
	@location(0) uv: vec2<f32>,
}

// Fullscreen triangle vertex shader
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
	var output: VertexOutput;
	
	// Generate fullscreen triangle
	// vertexIndex: 0 -> (-1, -1), 1 -> (3, -1), 2 -> (-1, 3)
	let x = f32((vertexIndex << 1u) & 2u) * 2.0 - 1.0;
	let y = f32(vertexIndex & 2u) * 2.0 - 1.0;
	
	output.position = vec4<f32>(x, y, 0.0, 1.0);
	output.uv = vec2<f32>(x * 0.5 + 0.5, 1.0 - (y * 0.5 + 0.5));
	
	return output;
}

// Fragment shader - samples source texture with linear filtering
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
	return textureSample(srcTexture, srcSampler, input.uv);
}
