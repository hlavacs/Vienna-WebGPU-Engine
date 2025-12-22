#pragma once

#include "OrbitCamera.h"
#include "engine/GameEngine.h"
#include "engine/NodeSystem.h"

#include <glm/glm.hpp>
#include <imgui.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace demo
{

class MainDemoImGuiUI
{
  public:
	MainDemoImGuiUI(engine::GameEngine &engine, const std::shared_ptr<engine::scene::entity::Node> &rootNode, const std::shared_ptr<demo::OrbitCameraController> &orbitCameraController);

	void render();

	void renderPerformanceWindow();

  private:
	engine::GameEngine &m_engine;
	std::shared_ptr<engine::scene::entity::Node> m_rootNode;
	std::shared_ptr<demo::OrbitCameraController> m_orbitCameraController;
	std::shared_ptr<engine::scene::CameraNode> m_cameraNode;
	std::vector<std::shared_ptr<engine::scene::entity::LightNode>> m_lightNodes;
	std::map<size_t, glm::vec3> m_lightDirectionsUI;
	std::unordered_map<engine::rendering::TextureHandle, ImTextureID> m_imguiTextureCache;
	OrbitCameraState &m_orbitState;

	void renderLightingAndCameraControls();
	void renderMaterialProperties();
	void renderLightsSection();
	void renderCameraControlsSection();

	ImTextureID GetOrCreateImGuiTexture(
		engine::rendering::TextureHandle textureHandle
	)
	{
		auto it = m_imguiTextureCache.find(textureHandle);
		if (it != m_imguiTextureCache.end())
			return it->second;

		auto textureOpt = textureHandle.get();
		if (!textureOpt.has_value())
			return nullptr;

		auto gpuTexture = m_engine.getContext()->textureFactory().createFromHandle(textureHandle);
		auto textureView = gpuTexture->getTextureView();
		ImTextureID imguiId = (ImTextureID)textureView;

		m_imguiTextureCache[textureHandle] = imguiId;
		return imguiId;
	}
};

} // namespace demo
