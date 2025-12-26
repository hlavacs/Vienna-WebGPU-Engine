#include "MainDemoImGuiUI.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace demo
{

MainDemoImGuiUI::MainDemoImGuiUI(
	engine::GameEngine &engine,
	const std::shared_ptr<engine::scene::entity::Node> &rootNode,
	const std::shared_ptr<demo::OrbitCameraController> &orbitCameraController
) : m_engine(engine),
	m_rootNode(rootNode),
	m_orbitCameraController(orbitCameraController),
	m_orbitState(orbitCameraController->getOrbitState())
{
	m_cameraNode = orbitCameraController->getCamera();

	for (const auto &child : m_rootNode->getChildrenOfType<engine::scene::entity::LightNode>())
	{
		m_lightNodes.push_back(child);
	};
}

void MainDemoImGuiUI::render()
{
	ImGui::Begin("Lighting & Camera Controls");
	renderLightingAndCameraControls();
	ImGui::Separator();
	renderMaterialProperties();
	renderLightsSection();
	renderCameraControlsSection();
	ImGui::End();
}

void MainDemoImGuiUI::renderPerformanceWindow()
{
	ImGui::Begin("Performance");
	ImGui::Text("FPS: %.1f", m_engine.getFPS());
	ImGui::Text("Frame Time: %.2f ms", m_engine.getFrameTime());
	ImGui::End();
}

void MainDemoImGuiUI::renderLightingAndCameraControls()
{
	// Shader reload button
	if (ImGui::Button("Reload Shaders (F5)"))
	{
		m_engine.getContext()->shaderRegistry().reloadAllShaders();
		m_engine.getContext()->pipelineManager().reloadAllPipelines();
		// ToDo: Recreate pipelines
	}
	ImGui::SameLine();
	// Debug rendering toggle
	static bool showDebugRendering = false;
	static bool prevDebugState = false;
	ImGui::Checkbox("Debug Rendering", &showDebugRendering);
	if (showDebugRendering != prevDebugState)
	{
		for (auto &light : m_lightNodes)
		{
			if (light)
				light->setDebugEnabled(showDebugRendering);
		}
		if (m_rootNode)
		{
			for (auto &child : m_rootNode->getChildren())
			{
				auto spatialNode = std::dynamic_pointer_cast<engine::scene::SpatialNode>(child);
				if (spatialNode)
					spatialNode->setDebugEnabled(showDebugRendering);
			}
		}
		prevDebugState = showDebugRendering;
	}
}

void MainDemoImGuiUI::renderMaterialProperties()
{
	float windowWidth = ImGui::GetWindowWidth();
	if (ImGui::CollapsingHeader("Material Properties") && m_rootNode)
	{
		auto children = m_rootNode->getChildrenOfType<engine::scene::entity::ModelRenderNode>();
		std::set<engine::rendering::MaterialHandle> materials;
		for (auto &child : children)
		{
			auto modelHandle = child->getModel();
			auto firstModelOpt = modelHandle.get();
			if (firstModelOpt.has_value())
			{
				auto firstModel = firstModelOpt.value();
				for (auto const &sm : firstModel->getSubmeshes())
				{
					materials.emplace(sm.material);
				}
			}
		}
		for (const auto &materialHandle : materials)
		{
			ImGui::PushID(materialHandle.id());
			auto materialOpt = materialHandle.get();
			ImGui::Separator();
			if (!materialOpt.has_value())
			{
				ImGui::Text("Material not found in manager");
			}
			else
			{
				auto material = materialOpt.value();
				ImGui::Text("Material Handle: %s", material->getName().value_or("Unnamed").c_str());
				auto materialProperties = material->getProperties<engine::rendering::PBRProperties>();
				bool materialsChanged = false;
				materialsChanged |= ImGui::ColorEdit4("Diffuse (Kd)", materialProperties.diffuse);
				materialsChanged |= ImGui::ColorEdit4("Emission (Ke)", materialProperties.emission);
				materialsChanged |= ImGui::ColorEdit4("Transmittance (Kt)", materialProperties.transmittance);
				materialsChanged |= ImGui::ColorEdit4("Ambient (Ka)", materialProperties.ambient);
				materialsChanged |= ImGui::SliderFloat("Roughness (Pr)", &materialProperties.roughness, 0.0f, 1.0f);
				materialsChanged |= ImGui::SliderFloat("Metallic (Pm)", &materialProperties.metallic, 0.0f, 1.0f);
				materialsChanged |= ImGui::SliderFloat("IOR (Ni)", &materialProperties.ior, 0.0f, 5.0f);
				if (materialsChanged)
				{
					material->setProperties(materialProperties);
				}
				for (const auto &[textureSlot, textureHandle] : material->getTextures())
				{
					ImGui::PushID(textureSlot.c_str());

					// Slot name (label)
					ImGui::TextUnformatted(textureSlot.c_str());

					if (textureHandle.valid())
					{
						auto textureOpt = textureHandle.get();
						if (textureOpt.has_value())
						{
							auto texture = textureOpt.value();
							ImTextureID imguiTex = GetOrCreateImGuiTexture(textureHandle);

							ImVec2 thumbSize(windowWidth - 64.0f, 32.0f);

							ImGui::Image(imguiTex, thumbSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));

							if (ImGui::IsItemHovered())
							{
								auto texturePathStr = texture->getFilePath();
								std::filesystem::path texturePath(texturePathStr);
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(texturePath.filename().string().c_str());
								ImGui::Separator();
								ImGui::Text("Size: %u x %u", texture->getWidth(), texture->getHeight());
								ImGui::TextWrapped("%s", texturePath.string().c_str());
								ImGui::EndTooltip();
							}
						}
						else
						{
							// Missing texture in manager
							ImGui::Dummy(ImVec2(windowWidth - 64.0f, 32.0f));
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted("Texture not found");
								ImGui::EndTooltip();
							}
						}
					}

					ImGui::PopID();
				}
			}
			ImGui::PopID();
		}
	}
}

void MainDemoImGuiUI::renderLightsSection()
{
	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("Add Light"))
		{
			auto newLight = std::make_shared<engine::scene::entity::LightNode>();
			uint32_t lightType = m_lightNodes.empty() ? 1 : 2;
			newLight->setLightType(lightType);
			newLight->setColor(glm::vec3(1.0f));
			newLight->setIntensity(1.0f);
			if (lightType == 1)
			{
				float pitch = 140.0f, yaw = -30.0f, roll = 0.0f;
				glm::quat rot = glm::quat(glm::radians(glm::vec3(pitch, yaw, roll)));
				newLight->getTransform()->setLocalRotation(rot);
				m_lightDirectionsUI[m_lightNodes.size()] = glm::vec3(pitch, yaw, roll);
			}
			else
			{
				newLight->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 0.0f));
			}
			m_rootNode->addChild(newLight);
			m_lightNodes.push_back(newLight);
			spdlog::info("Added light node");
		}
		for (size_t i = 0; i < m_lightNodes.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			auto &light = m_lightNodes[i];
			if (!light)
			{
				ImGui::PopID();
				continue;
			}
			const char *lightTypeNames[] = {"Ambient", "Directional", "Point", "Spot"};
			bool open = ImGui::TreeNodeEx(("Light " + std::to_string(i)).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::SameLine(ImGui::GetWindowWidth() - 70);
			bool shouldRemove = false;
			if (ImGui::SmallButton("Remove"))
			{
				shouldRemove = true;
			}
			if (open)
			{
				int currentType = static_cast<int>(light->getLightType());
				if (ImGui::Combo("Type", &currentType, lightTypeNames, 4))
				{
					light->setLightType(static_cast<uint32_t>(currentType));
				}
				glm::vec3 color = light->getColor();
				if (ImGui::ColorEdit3("Color", glm::value_ptr(color)))
				{
					light->setColor(color);
				}
				float intensity = light->getIntensity();
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 5.0f))
				{
					light->setIntensity(intensity);
				}
				auto transform = light->getTransform();
				if (transform)
				{
					glm::vec3 position = transform->getLocalPosition();
					if (m_lightDirectionsUI.find(i) == m_lightDirectionsUI.end())
					{
						glm::quat rotation = transform->getRotation();
						glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));
						m_lightDirectionsUI[i] = eulerAngles;
					}
					glm::vec3 &angles = m_lightDirectionsUI[i];
					if (light->getLightType() > 1)
					{
						if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
						{
							transform->setLocalPosition(position);
						}
					}
					if (light->getLightType() == 1 || light->getLightType() == 3)
					{
						if (ImGui::DragFloat3("Direction (degrees)", glm::value_ptr(angles), 0.5f))
						{
							glm::quat rot = glm::quat(glm::radians(angles));
							transform->setLocalRotation(rot);
						}
					}
					if (light->getLightType() == 3)
					{
						float spotAngle = light->getLightData().spot_angle;
						float spotAngleDegrees = glm::degrees(spotAngle);
						if (ImGui::SliderFloat("Cone Angle (degrees)", &spotAngleDegrees, 1.0f, 120.0f))
						{
							light->setSpotAngle(glm::radians(spotAngleDegrees));
						}
						float spotSoftness = light->getLightData().spot_softness;
						if (ImGui::SliderFloat("Edge Softness", &spotSoftness, 0.0f, 0.99f, "%.2f"))
						{
							light->setSpotSoftness(spotSoftness);
						}
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			if (shouldRemove)
			{
				if (light->getParent())
					light->getParent()->removeChild(light);
				m_lightNodes.erase(m_lightNodes.begin() + i);
				m_lightDirectionsUI.erase(i);
				std::map<size_t, glm::vec3> newDirectionsUI;
				for (const auto &[idx, ang] : m_lightDirectionsUI)
				{
					if (idx > i)
						newDirectionsUI[idx - 1] = ang;
					else
						newDirectionsUI[idx] = ang;
				}
				m_lightDirectionsUI = newDirectionsUI;
				break;
			}
		}
	}
}

void MainDemoImGuiUI::renderCameraControlsSection()
{
	if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen) && m_cameraNode)
	{
		glm::vec3 cameraPos = m_cameraNode->getTransform() ? m_cameraNode->getTransform()->getLocalPosition() : glm::vec3(0.0f);
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
		float camDistance = glm::length(cameraPos);
		ImGui::Text("Distance from origin: %.2f", camDistance);
		if (auto transform = m_cameraNode->getTransform())
		{
			glm::vec3 forward = transform->forward();
			glm::vec3 up = transform->up();
			glm::vec3 right = transform->right();
			ImGui::Separator();
			ImGui::Text("Orientation Vectors:");
			ImGui::Text("Forward: (%.2f, %.2f, %.2f)", forward.x, forward.y, forward.z);
			ImGui::Text("Up: (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
			ImGui::Text("Right: (%.2f, %.2f, %.2f)", right.x, right.y, right.z);
			ImGui::Text("Azimuth/Elevation: (%.2f / %.2f)", m_orbitState.azimuth, m_orbitState.elevation);
			glm::quat rotation = transform->getRotation();
			glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));
			if (eulerAngles.x > 90.0f)
				eulerAngles.x -= 360.0f;
			if (eulerAngles.y > 180.0f)
				eulerAngles.y -= 360.0f;
			if (eulerAngles.z > 180.0f)
				eulerAngles.z -= 360.0f;
			ImGui::Text("Rotation (degrees): (%.1f, %.1f, %.1f)", eulerAngles.x, eulerAngles.y, eulerAngles.z);
		}
		ImGui::Separator();
		float zoomPercentage = (camDistance - 2.0f) / 8.0f * 100.0f;
		zoomPercentage = glm::clamp(zoomPercentage, 0.0f, 100.0f);
		if (ImGui::SliderFloat("Camera Distance", &zoomPercentage, 0.0f, 100.0f, "%.0f%%"))
		{
			float newDistance = (zoomPercentage / 100.0f) * 8.0f + 2.0f;
			m_orbitState.distance = newDistance;
			demo::updateOrbitCamera(m_orbitState, m_cameraNode);
		}
		if (ImGui::Button("Look At Origin"))
		{
			m_cameraNode->lookAt(glm::vec3(0.0f));
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Camera"))
		{
			m_cameraNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 5.0f));
			m_cameraNode->lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			glm::vec3 camPos = m_cameraNode->getTransform()->getLocalPosition();
			glm::vec3 toCam = camPos - m_orbitState.targetPoint;
			m_orbitState.distance = glm::length(toCam);
			if (m_orbitState.distance > 1e-5f)
			{
				glm::vec3 dir = toCam / m_orbitState.distance;
				m_orbitState.elevation = std::asin(dir.y);
				m_orbitState.azimuth = std::atan2(dir.z, dir.x);
			}
		}
	}
}

} // namespace demo
