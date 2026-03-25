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

	ImTextureID getOrCreateImGuiTexture(engine::rendering::TextureHandle textureHandle);
};

} // namespace demo
