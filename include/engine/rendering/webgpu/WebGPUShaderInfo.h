#pragma once
#include <string>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

/**
 * @brief Encapsulates shader module and entry point(s) for a render pipeline.
 * 
 * Can represent either:
 * 1. A single-stage shader (vertex OR fragment)
 * 2. A combined shader module with both vertex and fragment stages
 */
struct WebGPUShaderInfo
{
	// ToDo: Protected members?
	wgpu::ShaderModule module;
	std::string entryPoint;

	// Optional: separate entry points for vertex and fragment when using same module
	std::string vertexEntryPoint;
	std::string fragmentEntryPoint;

	/**
	 * @brief Constructs a single-stage shader info.
	 * @param mod Shader module.
	 * @param entry Entry point function name.
	 */
	WebGPUShaderInfo(wgpu::ShaderModule mod, const std::string &entry) :
		module(mod), entryPoint(entry) {}

	/**
	 * @brief Constructs a combined shader info with separate vertex and fragment entry points.
	 * @param mod Shader module containing both vertex and fragment shaders.
	 * @param vertexEntry Vertex shader entry point.
	 * @param fragmentEntry Fragment shader entry point.
	 */
	WebGPUShaderInfo(wgpu::ShaderModule mod, const std::string &vertexEntry, const std::string &fragmentEntry) :
		module(mod), vertexEntryPoint(vertexEntry), fragmentEntryPoint(fragmentEntry) {}

	/**
	 * @brief Checks if this shader info has separate vertex/fragment entry points.
	 * @return True if using combined mode with separate entry points.
	 */
	bool hasSeparateEntryPoints() const 
	{ 
		return !vertexEntryPoint.empty() && !fragmentEntryPoint.empty(); 
	}

	/**
	 * @brief Gets the vertex entry point (supports both single and combined modes).
	 * @return The vertex entry point name.
	 */
	const std::string& getVertexEntryPoint() const 
	{ 
		return hasSeparateEntryPoints() ? vertexEntryPoint : entryPoint; 
	}

	/**
	 * @brief Gets the fragment entry point (supports both single and combined modes).
	 * @return The fragment entry point name.
	 */
	const std::string& getFragmentEntryPoint() const 
	{ 
		return hasSeparateEntryPoints() ? fragmentEntryPoint : entryPoint; 
	}
};

} // namespace engine::rendering::webgpu
