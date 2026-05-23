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

	// Free-flying point lights demo (SeaKeep / second scene)
	bool m_flockEnabled = false;
	int m_flockAmount = 100;
	float m_flockAttraction = 1.0f;
	glm::vec3 m_flockCenter = glm::vec3(0.0f, 5.0f, 0.0f);
	std::vector<std::shared_ptr<engine::scene::nodes::LightNode>> m_flockLights;
	std::vector<glm::vec3> m_flockVelocities;
	std::vector<float> m_flockNoisePhases;

	void renderFlockControls();
	void spawnFlock(int amount);
	void clearFlock();
	void updateFlock(float deltaSeconds);

	ImTextureID getOrCreateImGuiTexture(engine::rendering::TextureHandle textureHandle);
};

} // namespace demo
