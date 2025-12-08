#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

// Forward declarations
struct SDL_Window;
namespace engine::rendering::webgpu
{
class WebGPUContext;
}

namespace engine::ui
{

/**
 * @class ImGuiManager
 * @brief Manages ImGui initialization, frame rendering, and cleanup
 * 
 * Handles all ImGui setup for SDL2 and WebGPU backends, and provides
 * a callback-based system for adding UI frames.
 */
class ImGuiManager
{
  public:
	/// UI frame callback type - users build their ImGui UI in this callback
	using UIFrameCallback = std::function<void()>;

	ImGuiManager() = default;
	~ImGuiManager();

	// Disable copy/move
	ImGuiManager(const ImGuiManager&) = delete;
	ImGuiManager& operator=(const ImGuiManager&) = delete;
	ImGuiManager(ImGuiManager&&) = delete;
	ImGuiManager& operator=(ImGuiManager&&) = delete;

	/**
	 * @brief Initialize ImGui with SDL2 and WebGPU backends
	 * @param window SDL window handle
	 * @param context WebGPU context for device and format information
	 * @return true if initialization succeeded, false otherwise
	 */
	bool initialize(SDL_Window* window, std::shared_ptr<engine::rendering::webgpu::WebGPUContext> context);

	/**
	 * @brief Shutdown and cleanup ImGui
	 */
	void shutdown();

	/**
	 * @brief Add a UI frame callback
	 * @param callback Function that builds ImGui UI
	 * 
	 * Multiple callbacks can be added and will be executed in order during each frame.
	 */
	void addFrame(UIFrameCallback callback);

	/**
	 * @brief Clear all registered frame callbacks
	 */
	void clearFrames();

	/**
	 * @brief Render all registered UI frames
	 * @param renderPass WebGPU render pass encoder to render into
	 * 
	 * This executes all registered callbacks and renders the final ImGui draw data.
	 */
	void render(wgpu::RenderPassEncoder renderPass);

	/**
	 * @brief Check if ImGui is initialized
	 * @return true if initialized, false otherwise
	 */
	bool isInitialized() const { return m_initialized; }

  private:
	bool m_initialized = false;
	std::vector<UIFrameCallback> m_frameCallbacks;
};

} // namespace engine::ui
