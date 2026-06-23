#pragma once

#include <glm/glm.hpp>
#include <imgui.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <webgpu/webgpu.hpp>

#include "engine/GameEngine.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"

#include "DayNightCycle.h"
#include "OrbitCamera.h"

namespace demo
{

class MainDemoImGuiUI
{
  public:
	MainDemoImGuiUI(
		engine::GameEngine &engine,
		std::shared_ptr<DayNightCycle> dayNightCycle = nullptr,
		std::shared_ptr<demo::OrbitCameraController> orbitController = nullptr
	);
	~MainDemoImGuiUI();

	void render(const std::shared_ptr<engine::scene::SceneManager>& sceneManager);

	void renderPerformanceWindow();

	void renderShadowDebugWindow();

	/**
	 * @brief Pass-control debug window: per-pass enable/disable toggle and
	 *        a stage-preview dropdown that overrides what CompositePass
	 *        samples (HDR target / GBuffer slot / depth).
	 *
	 * The toggles flip `RenderPass::isEnabled()` flags live — the next
	 * frame skips disabled passes. Useful for A/B-ing which pass owns a
	 * given artifact and for visualizing intermediate render-graph stages
	 * without a separate viewer.
	 */
	void renderPassControlsWindow();

  private:
	engine::GameEngine& m_engine;
	std::shared_ptr<engine::scene::nodes::Node> m_rootNode;
	std::shared_ptr<engine::scene::nodes::CameraNode> m_cameraNode;
	std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> m_debugShadowCubeArray;
	std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> m_debugShadow2DArray;

	std::vector<std::shared_ptr<engine::scene::nodes::LightNode>> m_lightNodes;
	std::map<size_t, glm::vec3> m_lightDirectionsUI; //< Seperate storage for Euler angles for ImGui because of instability when converting from quaternions every frame.
	std::unordered_map<engine::rendering::TextureHandle, ImTextureID> m_imguiTextureCache;
	void renderLightingAndCameraControls();
	void renderMaterialProperties();
	void renderLightsSection();

	/// Top menu bar: Scene dropdown, View / Debug panel toggles, Reload Shaders.
	void renderMainMenuBar(const std::shared_ptr<engine::scene::SceneManager> &sceneManager);
	/// Day-night cycle controls window (hour / pause / cycle duration).
	void renderDayNightWindow();

	/// Register an ImGui settings handler so each panel's open/closed state is
	/// persisted to imgui.ini and restored on the next run.
	void registerSettingsPersistence();
	/// (ini-key -> visibility flag) table the settings handler reads/writes.
	std::vector<std::pair<const char *, bool *>> panelVisibilityTable();
	/// Apply one persisted "key=value" panel-visibility setting on load.
	void applyPanelVisibility(const char *key, bool value);

	// Day-night cycle this UI drives (owned by main; may be null for scenes
	// without one). The Day-Night window + its View-menu entry only appear
	// when set.
	std::shared_ptr<DayNightCycle> m_dayNightCycle;

	std::shared_ptr<demo::OrbitCameraController> m_orbitController;

	// Per-panel visibility, toggled from the menu bar (and each window's own
	// close button). Defaults: everyday panels on, debug-heavy ones off.
	bool m_showCameraLighting = true;
	bool m_showMaterials      = false;
	bool m_showLights         = true;
	bool m_showFlock          = false;
	bool m_showDayNight       = false; // scene-specific (SeaKeep); off by default
	bool m_showPerformance    = true;
	bool m_screenshotRequested = false;
	bool m_showPassControls   = false;
	bool m_showShadowDebug    = false;

	// Sponza-style scattered point lights. Defaults match the classic deferred-
	// rendering / clustered-shading demo: lots of small, bright, saturated
	// lights spread across the scene volume, bobbing gently rather than
	// flocking.
	bool m_flockEnabled = false;
	int m_flockAmount = 100;
	float m_flockIntensity = 50.0f;
	float m_flockRange = 6.0f;
	float m_flockMarkerScale = 0.15f;
	float m_flockBobAmplitude = 0.5f;
	float m_flockBobSpeed = 0.7f;
	glm::vec3 m_flockCenter = glm::vec3(0.0f, 5.0f, 0.0f);
	glm::vec3 m_flockExtent = glm::vec3(30.0f, 10.0f, 30.0f);
	std::vector<std::shared_ptr<engine::scene::nodes::LightNode>> m_flockLights;
	std::vector<glm::vec3> m_flockOrigins;     // per-light static base position
	std::vector<float> m_flockNoisePhases;     // per-light bob phase

	void renderFlockControls();
	void spawnFlock(int amount);
	void clearFlock();
	void updateFlock(float deltaSeconds);

	ImTextureID getOrCreateImGuiTexture(engine::rendering::TextureHandle textureHandle);

	// === Stage Preview: depth-blit pipeline ===
	// One-shot pipeline that turns the G-buffer's Depth32Float attachment
	// into an RGBA8Unorm preview texture the debug UI can ImGui::Image
	// without hitting the "filterable-Float vs Depth" sample-type mismatch.
	// Lazily built on first call to renderDepthPreviewBlit; released in
	// the destructor.
	bool ensureDepthPreviewPipeline();
	bool renderDepthPreviewBlit(
		const std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> &depthSource
	);

	wgpu::ShaderModule    m_depthPreviewShaderModule = nullptr;
	wgpu::BindGroupLayout m_depthPreviewBindGroupLayout = nullptr;
	wgpu::PipelineLayout  m_depthPreviewPipelineLayout = nullptr;
	wgpu::RenderPipeline  m_depthPreviewPipeline = nullptr;
	std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> m_depthPreviewTarget;
	// Track the source depth's identity so we can resize the preview target
	// when the GBuffer dimensions change (window resize, multi-camera with
	// different viewports). Same signature pattern the cached bind groups
	// use, scaled down to a single pointer comparison since we only have
	// one source.
	const engine::rendering::webgpu::WebGPUTexture *m_depthPreviewSourceFingerprint = nullptr;
	uint32_t m_depthPreviewSourceWidth  = 0;
	uint32_t m_depthPreviewSourceHeight = 0;
};

} // namespace demo
