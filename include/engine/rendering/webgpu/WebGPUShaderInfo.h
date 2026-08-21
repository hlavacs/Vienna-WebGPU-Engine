#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <webgpu/webgpu.hpp>

#include "engine/rendering/ShaderFeatureMask.h"
#include "engine/rendering/ShaderType.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering::webgpu
{

class WebGPUShaderFactory;

/**
 * @brief Pure shader metadata with manual reflection information.
 *
 * Contains:
 * - Shader module and entry points
 * - Bind group layouts
 *
 * No GPU buffers or bind groups are stored here.
 */
class WebGPUShaderInfo
{
	friend class WebGPUShaderFactory;

  public:
	WebGPUShaderInfo(
		std::string name,
		std::filesystem::path path,
		engine::rendering::ShaderType type,
		wgpu::ShaderModule mod,
		std::string vertexEntry,
		std::string fragmentEntry,
		engine::rendering::VertexLayout vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor,
		engine::rendering::ShaderFeature::Flag features = engine::rendering::ShaderFeature::Flag::None,
		bool enableDepth = true,
		bool cullBackFaces = true
	) : m_name(std::move(name)),
		m_path(std::move(path)),
		m_shaderType(type),
		m_module(mod),
		m_vertexEntryPoint(std::move(vertexEntry)),
		m_fragmentEntryPoint(std::move(fragmentEntry)),
		m_vertexLayout(vertexLayout),
		m_features(features),
		m_enableDepth(enableDepth),
		m_cullBackFaces(cullBackFaces)
	{
	}

	~WebGPUShaderInfo();
	/**
	 * @brief Gets the shader name.
	 * @return Shader name string.
	 */
	[[nodiscard]] const std::string &getName() const { return m_name; }
	/**
	 * @brief Gets the shader file path.
	 * @return Shader file path.
	 */
	[[nodiscard]] const std::filesystem::path &getPath() const { return m_path; }
	/**
	 * @brief Gets the WebGPU shader module.
	 * @return The WebGPU shader module.
	 */
	[[nodiscard]] wgpu::ShaderModule getModule() const { return m_module; }
	/**
	 * @brief Gets the vertex entry point name.
	 * @return Vertex entry point string.
	 */
	[[nodiscard]] const std::string &getVertexEntryPoint() const { return m_vertexEntryPoint; }
	/**
	 * @brief Gets the fragment entry point name.
	 * @return Fragment entry point string.
	 */
	[[nodiscard]] const std::string &getFragmentEntryPoint() const { return m_fragmentEntryPoint; }
	/**
	 * @brief Gets the shader type.
	 * @return Shader type enum.
	 */
	[[nodiscard]] const engine::rendering::ShaderType &getShaderType() const { return m_shaderType; }
	/**
	 * @brief Gets the vertex layout.
	 * @return Vertex layout enum.
	 */
	[[nodiscard]] const engine::rendering::VertexLayout &getVertexLayout() const { return m_vertexLayout; }
	/**
	 * @brief Gets the vertex layout.
	 * @return Vertex layout enum.
	 */
	[[nodiscard]] const engine::rendering::ShaderFeature::Flag &getShaderFeatures() const { return m_features; }
	/**
	 * @brief Wether depth testing is enabled.
	 * @return True if depth testing is enabled.
	 */
	[[nodiscard]] bool isDepthEnabled() const { return m_enableDepth; }

	/**
	 * @brief Depth comparison function used by the pipeline depth-stencil state.
	 * Defaults to @c Less (standard z-buffer). Set to @c LessEqual for shaders
	 * that emit fragments at the far plane (e.g. a skybox writing @c clip.xyww ).
	 */
	[[nodiscard]] wgpu::CompareFunction getDepthCompare() const { return m_depthCompare; }

	/**
	 * @brief Whether the fragment shader writes to the depth attachment.
	 * Defaults to @c true. Set to @c false for read-only depth use (skybox
	 * sampling the existing depth, post-processing reading depth, etc.).
	 */
	[[nodiscard]] bool isDepthWriteEnabled() const { return m_depthWriteEnabled; }

	/**
	 * @brief Wether the shader has a fragment stage.
	 * @return True if fragment stage exists.
	 */
	[[nodiscard]] bool hasFragmentStage() const { return !m_fragmentEntryPoint.empty(); }
	/**
	 * @brief Wether back-face culling is enabled.
	 * @return True if back-face culling is enabled.
	 */
	[[nodiscard]] bool isBackFaceCullingEnabled() const { return m_cullBackFaces; }

	/**
	 * @brief Check if shader info is valid.
	 * @return True if valid, false otherwise.
	 */
	[[nodiscard]] bool isValid() const;

	/**
	 * @brief Access bind group layouts for pipeline creation.
	 * @return Map of group index to layout info.
	 */
	[[nodiscard]] const std::unordered_map<uint64_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> &getBindGroupLayouts() const;

	/**
	 * @brief Access bind group layouts for pipeline creation.
	 * @return Vector of layout infos.
	 */
	[[nodiscard]] std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> getBindGroupLayoutVector() const;

	/**
	 * @brief Get a specific bind group layout by index.
	 * @param groupIndex The bind group index.
	 * @return Shared pointer to layout info or nullptr.
	 */
	[[nodiscard]] std::shared_ptr<WebGPUBindGroupLayoutInfo> getBindGroupLayout(uint32_t groupIndex) const;

	/**
	 * @brief Get bind group layout by type
	 */
	[[nodiscard]] std::shared_ptr<WebGPUBindGroupLayoutInfo> getBindGroupLayout(BindGroupType type) const;

	/**
	 * @brief Get bind group layout by name
	 */
	[[nodiscard]] std::shared_ptr<WebGPUBindGroupLayoutInfo> getBindGroupLayout(const std::string &name) const;

	/**
	 * @brief Gets the bind group index by name of the bind group.
	 * @return Optional bind group index. std::nullopt if not found.
	 */
	[[nodiscard]] std::optional<uint64_t> getBindGroupIndex(const std::string &name) const;
	/**
	 * @brief Check if a bind group exists by name
	 */
	[[nodiscard]] bool hasBindGroup(const std::string &name) const;

	/**
	 * @brief Check if a bind group exists by type
	 */
	[[nodiscard]] bool hasBindGroup(BindGroupType type) const;

	/**
	 * @brief Returns the explicit color attachment formats this shader writes.
	 *
	 * Used by @ref WebGPUPipelineFactory when the shader has more than one
	 * color target (e.g. a G-buffer with four attachments) or when the format
	 * differs from the renderer-provided default. An empty vector means
	 * "single attachment, use the format supplied at pipeline creation".
	 */
	[[nodiscard]] const std::vector<wgpu::TextureFormat> &getColorTargetFormats() const { return m_colorTargetFormats; }

  private:
	void setName(std::string name);
	void setPath(std::string path);
	void setVertexLayout(engine::rendering::VertexLayout layout);
	void setVertexEntryPoint(std::string entry);
	void setFragmentEntryPoint(std::string entry);
	void setShaderType(engine::rendering::ShaderType type);
	void setShaderFeatures(engine::rendering::ShaderFeature::Flag features);
	void setEnableDepth(bool enable);
	void setColorTargetFormats(std::vector<wgpu::TextureFormat> formats) { m_colorTargetFormats = std::move(formats); }
	void setDepthCompare(wgpu::CompareFunction compare) { m_depthCompare = compare; }
	void setDepthWriteEnabled(bool enabled) { m_depthWriteEnabled = enabled; }
	void addBindGroupLayout(uint32_t groupIndex, std::shared_ptr<WebGPUBindGroupLayoutInfo> layout);

	bool m_enableDepth;
	bool m_cullBackFaces;
	bool m_depthWriteEnabled = true;
	wgpu::CompareFunction m_depthCompare = wgpu::CompareFunction::Less;

	std::string m_name;
	std::filesystem::path m_path;
	wgpu::ShaderModule m_module = nullptr;
	std::string m_vertexEntryPoint;
	std::string m_fragmentEntryPoint;
	engine::rendering::ShaderType m_shaderType = engine::rendering::ShaderType::Lit;
	engine::rendering::VertexLayout m_vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor;
	engine::rendering::ShaderFeature::Flag m_features = engine::rendering::ShaderFeature::Flag::None;

	std::vector<wgpu::TextureFormat> m_colorTargetFormats;
	std::unordered_map<uint64_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> m_bindGroupLayouts;
	std::unordered_map<std::string, uint64_t> m_nameToIndex;
	std::unordered_map<BindGroupType, uint64_t> m_typeToIndex;
};

} // namespace engine::rendering::webgpu
