#pragma once

#include <glm/glm.hpp>
#include <imgui.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "OrbitCamera.h"
#include "engine/GameEngine.h"

namespace demo
{

class MainDemoImGuiUI
{
  public:
	MainDemoImGuiUI(engine::GameEngine &engine, const std::shared_ptr<engine::scene::nodes::Node> &rootNode, const std::shared_ptr<demo::OrbitCameraController> &orbitCameraController);

	void render();

	void renderPerformanceWindow();

  private:
	  engine::GameEngine &m_engine;
	  std::shared_ptr<engine::scene::nodes::Node> m_rootNode;
	  std::shared_ptr<demo::OrbitCameraController> m_orbitCameraController;
	  std::shared_ptr<engine::scene::nodes::CameraNode> m_cameraNode;
	  OrbitCameraState &m_orbitState;

	  std::vector<std::shared_ptr<engine::scene::nodes::LightNode>> m_lightNodes;
	  std::map<size_t, glm::vec3> m_lightDirectionsUI; //< Seperate storage for Euler angles for ImGui because of instability when converting from quaternions every frame.
	  std::unordered_map<engine::rendering::TextureHandle, ImTextureID> m_imguiTextureCache;
	  void renderLightingAndCameraControls();
	  void renderMaterialProperties();
	  void renderLightsSection();
	  void renderCameraControlsSection();

	  ImTextureID getOrCreateImGuiTexture(engine::rendering::TextureHandle textureHandle); 
};

} // namespace demo
