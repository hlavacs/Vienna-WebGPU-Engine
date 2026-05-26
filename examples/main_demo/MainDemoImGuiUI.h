#pragma once

#include <glm/glm.hpp>
#include <imgui.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "engine/GameEngine.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"

namespace demo
{

class MainDemoImGuiUI
{
  public:
	MainDemoImGuiUI(engine::GameEngine &engine);

	void render(const std::shared_ptr<engine::scene::SceneManager>& sceneManager);

	void renderPerformanceWindow();

	void renderShadowDebugWindow();

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
};

} // namespace demo
