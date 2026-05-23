#include "MainDemoImGuiUI.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include "engine/resources/ResourceManager.h"

namespace demo
{

MainDemoImGuiUI::MainDemoImGuiUI(
	engine::GameEngine &engine
) : m_engine(engine)
{
	m_rootNode = engine.getSceneManager()->getActiveScene()->getRoot();
	for (const auto &child : m_rootNode->getChildrenOfType<engine::scene::nodes::LightNode>())
	{
		m_lightNodes.push_back(child);
	};

	auto renderer = m_engine.getRenderer().lock();
	m_debugShadowCubeArray = renderer->getShadowPass().DEBUG_SHADOW_CUBE_ARRAY;
	m_debugShadow2DArray = renderer->getShadowPass().DEBUG_SHADOW_2D_ARRAY;
}

void MainDemoImGuiUI::render(const std::shared_ptr<engine::scene::SceneManager> &sceneManager)
{
	auto rootNode = m_engine.getSceneManager()->getActiveScene()->getRoot();
	if (m_rootNode != rootNode)
	{
		m_rootNode = rootNode;
		m_lightNodes.clear();
		for (const auto &child : m_rootNode->getChildrenOfType<engine::scene::nodes::LightNode>())
		{
			m_lightNodes.push_back(child);
		};
	}
	ImGui::Begin("Lighting & Camera Controls");

	renderLightingAndCameraControls();
	ImGui::Separator();
	renderMaterialProperties();
	renderLightsSection();
	ImGui::End();
}

void MainDemoImGuiUI::renderPerformanceWindow()
{
	ImGui::Begin("Performance");
	ImGui::Text("FPS: %.1f", m_engine.getFPS());
	ImGui::Text("Frame Time: %.2f ms", m_engine.getFrameTime());
	ImGui::End();
}

void MainDemoImGuiUI::renderShadowDebugWindow()
{
	auto renderer = m_engine.getRenderer().lock();
	if (!renderer || !renderer->getShadowPass().isDebugMode())
		return;

	ImGui::Begin("Shadow Map Debug");

	const int thumbSize = 128;
	const int columns = 3;

	// --- Cube array debug ---
	if (m_debugShadowCubeArray)
	{
		if (ImGui::CollapsingHeader("Cube Shadow Maps"))
		{
			const int totalLayers = m_debugShadowCubeArray->getTextureViewDescriptor().arrayLayerCount;
			const int numCubes = totalLayers / 6;

			for (int cubeIndex = 0; cubeIndex < numCubes; ++cubeIndex)
			{
				if (ImGui::CollapsingHeader(("Cube " + std::to_string(cubeIndex)).c_str()))
				{
					ImGui::Text("Cube Index: %d", cubeIndex);
					ImGui::Separator();

					ImGui::Columns(columns, nullptr, false);

					for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
					{
						int layerIndex = cubeIndex * 6 + faceIndex;
						ImTextureID faceImguiId = m_debugShadowCubeArray->getTextureView(layerIndex);

						ImGui::Text("Face %d", faceIndex);
						ImGui::Image(faceImguiId, ImVec2((float)thumbSize, (float)thumbSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
						ImGui::NextColumn();
					}

					ImGui::Columns(1);
					ImGui::Separator();
				}
			}
		}
	}
	else
	{
		ImGui::Text("No cube shadow array texture available.");
	}

	// --- 2D array debug ---
	if (m_debugShadow2DArray)
	{
		if (ImGui::CollapsingHeader("2D Shadow Maps"))
		{
			const int totalLayers = m_debugShadow2DArray->getTextureViewDescriptor().arrayLayerCount;
			ImGui::Columns(columns, nullptr, false);

			for (int layerIndex = 0; layerIndex < totalLayers; ++layerIndex)
			{
				ImTextureID texId = m_debugShadow2DArray->getTextureView(layerIndex);

				ImGui::Text("Layer %d", layerIndex);
				ImGui::Image(texId, ImVec2((float)thumbSize, (float)thumbSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
				ImGui::NextColumn();
			}

			ImGui::Columns(1);
			ImGui::Separator();
		}
	}
	else
	{
		ImGui::Text("No 2D shadow array texture available.");
	}

	ImGui::End();
}

void MainDemoImGuiUI::renderLightingAndCameraControls()
{
	// Shader reload button
	if (ImGui::Button("Reload Shaders (F5)"))
	{
		m_engine.getContext()->shaderRegistry().reloadAllShaders();
		m_engine.getContext()->pipelineManager().reloadAllPipelines();
	}
	ImGui::SameLine();
	// Debug rendering toggle
	static bool showDebugRendering = false;
	static bool showDebugShadowMaps = false;
	static bool prevDebugState = false;
	ImGui::Checkbox("Debug Rendering", &showDebugRendering);
	ImGui::Checkbox("Debug Shadow Maps", &showDebugShadowMaps);
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
				if (child->isSpatial())
				{
					child->setDebugEnabled(showDebugRendering);
				}
			}
		}
		prevDebugState = showDebugRendering;
	}
	m_engine.getRenderer().lock()->getShadowPass().setDebugMode(showDebugShadowMaps);
}

void MainDemoImGuiUI::renderMaterialProperties()
{
	float windowWidth = ImGui::GetWindowWidth();
	if (ImGui::CollapsingHeader("Material Properties") && m_rootNode)
	{
		auto children = m_rootNode->getChildrenOfType<engine::scene::nodes::ModelRenderNode>();
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
				continue;
			}
			auto material = materialOpt.value();
			auto text = std::string("Material Handle: ") + material->getName().value_or("Unnamed");
			ImGui::Indent();
			if (ImGui::CollapsingHeader(text.c_str()))
			{
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
							ImTextureID imguiTex = getOrCreateImGuiTexture(textureHandle);

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
			ImGui::Unindent();
			ImGui::PopID();
		}
	}
}

void MainDemoImGuiUI::renderLightsSection()
{
	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Display light count with limit
		ImGui::Text("Lights: %zu / 512", m_lightNodes.size());
		ImGui::SameLine();
		if (m_lightNodes.size() >= 512)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(MAX)");
		}

		// Add Light button with limit check
		const bool canAddLight = m_lightNodes.size() < 512;
		if (!canAddLight) ImGui::BeginDisabled();
		
		if (ImGui::Button("Add Light"))
		{
			if (m_lightNodes.size() < 512)
			{
				auto newLight = std::make_shared<engine::scene::nodes::LightNode>();
				// Default to directional light if first, point light otherwise
				if (m_lightNodes.empty())
				{
					engine::rendering::DirectionalLight directionalData;
					directionalData.color = glm::vec3(1.0f);
					directionalData.intensity = 1.0f;
					newLight->getLight().setData(directionalData);

					float pitch = 140.0f, yaw = -30.0f, roll = 0.0f;
					glm::quat rot = glm::quat(glm::radians(glm::vec3(pitch, yaw, roll)));
					newLight->getTransform().setLocalRotation(rot);
					m_lightDirectionsUI[m_lightNodes.size()] = glm::vec3(pitch, yaw, roll);
				}
				else
				{
					engine::rendering::PointLight pointData;
					pointData.color = glm::vec3(1.0f);
					pointData.intensity = 1.0f;
					newLight->getLight().setData(pointData);
					// Place new point lights near scene center by default
					newLight->getTransform().setLocalPosition(glm::vec3(0.0f, 5.0f, 0.0f));
				}
				m_rootNode->addChild(newLight);
				m_lightNodes.push_back(newLight);
				spdlog::info("Added light node ({} / 512)", m_lightNodes.size());
			}
			else
			{
				spdlog::warn("Maximum light count (512) reached");
			}
		}
		
		if (!canAddLight) ImGui::EndDisabled();
		
		if (!canAddLight)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Max lights reached");
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
					// Change light type by creating new light data
					switch (currentType)
					{
					case 0: // Ambient
					{
						engine::rendering::AmbientLight ambientData;
						ambientData.color = light->getColor();
						ambientData.intensity = light->getIntensity();
						light->getLight().setData(ambientData);
						break;
					}
					case 1: // Directional
					{
						engine::rendering::DirectionalLight directionalData;
						directionalData.color = light->getColor();
						directionalData.intensity = light->getIntensity();
						light->getLight().setData(directionalData);
						break;
					}
					case 2: // Point
					{
						engine::rendering::PointLight pointData;
						pointData.color = light->getColor();
						pointData.intensity = light->getIntensity();
						light->getLight().setData(pointData);
						break;
					}
					case 3: // Spot
					{
						engine::rendering::SpotLight spotData;
						spotData.color = light->getColor();
						spotData.intensity = light->getIntensity();
						light->getLight().setData(spotData);
						break;
					}
					}
				}
				glm::vec3 color = light->getColor();
				if (ImGui::ColorEdit3("Color", glm::value_ptr(color)))
				{
					light->setColor(color);
				}
				float intensity = light->getIntensity();
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 100.0f))
				{
					light->setIntensity(intensity);
				}
				auto &lightTransform = light->getTransform();
				glm::vec3 position = lightTransform.getLocalPosition();
				if (m_lightDirectionsUI.find(i) == m_lightDirectionsUI.end())
				{
					// We store Euler angles separately for ImGui.
					// Converting from quaternion every frame is unstable because
					// Euler representations are not unique and can cause angle jumps
					// and slider jitter in the UI.
					glm::quat rotation = lightTransform.getRotation();
					glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));
					m_lightDirectionsUI[i] = eulerAngles;
				}
				glm::vec3 &angles = m_lightDirectionsUI[i];
				if (!light->getLight().isAmbient() && !light->getLight().isDirectional())
				{
					if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
					{
						lightTransform.setLocalPosition(position);
					}
				}
				if (light->getLight().isDirectional() || light->getLight().isSpot())
				{
					if (ImGui::DragFloat3("Direction (degrees)", glm::value_ptr(angles), 0.5f))
					{
						glm::quat rot = glm::quat(glm::radians(angles));
						lightTransform.setLocalRotation(rot);
					}
				}
				if (light->getLight().isSpot())
				{
					// Access spot light data directly
					auto &spotData = light->getLight().asSpot();
					float spotAngleDegrees = glm::degrees(spotData.spotAngle) * 2.0f; // Full cone angle
					if (ImGui::SliderFloat("Cone Angle (degrees)", &spotAngleDegrees, 1.0f, 180.0f))
					{
						spotData.spotAngle = glm::radians(spotAngleDegrees / 2.0f);
					}
					float spotSoftness = spotData.spotSoftness;
					if (ImGui::SliderFloat("Edge Softness", &spotSoftness, 0.0f, 0.99f, "%.2f"))
					{
						spotData.spotSoftness = spotSoftness;
					}
				}

				// Shadow casting controls (for directional, point, and spot lights)
				if (!light->getLight().isAmbient())
				{
					bool castShadows = light->getCastShadows();
					if (ImGui::Checkbox("Cast Shadows", &castShadows))
					{
						light->setCastShadows(castShadows);
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

	// Free-flying point lights controls & update (SeaKeep-style)
	renderFlockControls();
}

void MainDemoImGuiUI::renderFlockControls()
{
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Free-Flying Point Lights"))
	{
		ImGui::Checkbox("Enable Flock", &m_flockEnabled);
		ImGui::SliderInt("Amount", &m_flockAmount, 0, 1000);
		ImGui::SliderFloat("Attraction", &m_flockAttraction, 0.0f, 10.0f);
		ImGui::InputFloat3("Center", glm::value_ptr(m_flockCenter));

		if (ImGui::Button("Spawn/Apply"))
		{
			// Adjust existing flock to requested amount
			if (m_flockAmount > 0)
				spawnFlock(m_flockAmount);
			else
				clearFlock();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Flock"))
		{
			clearFlock();
			m_flockEnabled = false;
		}

		// Update positions every frame when enabled
		if (m_flockEnabled && !m_flockLights.empty())
		{
			float dt = m_engine.getFrameTime() * 0.001f; // ms -> seconds
			updateFlock(dt);
		}
	}
}

void MainDemoImGuiUI::spawnFlock(int amount)
{
	// Clamp amount to reasonable maximum
	amount = std::min(1000, std::max(0, amount));

	// If we already have lights, adjust up/down
	const int current = static_cast<int>(m_flockLights.size());
	if (amount == current)
		return;

	if (amount < current)
	{
		// Remove extras
		for (int i = amount; i < current; ++i)
		{
			auto node = m_flockLights[i];
			if (node && node->getParent())
				node->getParent()->removeChild(node);
		}
		m_flockLights.resize(amount);
		m_flockVelocities.resize(amount);
		return;
	}

	// Add missing lights
		spdlog::info("Spawning flock: current={}, target={}", current, amount);
		for (int i = current; i < amount; ++i)
	{
		auto newLight = std::make_shared<engine::scene::nodes::LightNode>();
		engine::rendering::PointLight pointData;
			// Give each flock light a noticeable intensity and varied color
			float r = 0.5f + static_cast<float>(rand()) / RAND_MAX * 0.5f;
			float g = 0.5f + static_cast<float>(rand()) / RAND_MAX * 0.5f;
			float b = 0.5f + static_cast<float>(rand()) / RAND_MAX * 0.5f;
			pointData.color = glm::vec3(r, g, b);
			pointData.intensity = 100.0f; // make bright enough to be visible
		pointData.castShadows = false; // No shadows
		newLight->getLight().setData(pointData);

		// Randomize initial position around center
		float radius = 10.0f;
		float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159265f;
		float h = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f;
		glm::vec3 pos = m_flockCenter + glm::vec3(cos(angle) * radius, h, sin(angle) * radius);
		newLight->getTransform().setLocalPosition(pos);

		// Add to scene root
		if (m_rootNode)
			m_rootNode->addChild(newLight);

		// Enable debug marker so the light has a visible particle-like visualization
		newLight->setDebugEnabled(true);

		// Attach a tiny sphere model as a visible marker (if available).
		// Create a unique model instance and a matching material so the sphere matches the light color.
		auto resourceManager = m_engine.getResourceManager();
		if (resourceManager)
		{
			// Load base model once via manager but create a uniquely named instance so we can assign a per-instance material
			std::string instanceName = std::string("sphere_inst_") + std::to_string(newLight->getId());
			auto spherePath = engine::core::PathProvider::getResource("sphere.obj");
			auto maybeModelInst = resourceManager->m_modelManager->createModel(spherePath, std::optional<std::string>(instanceName));
			if (maybeModelInst.has_value())
			{
				// Create a simple PBR material colored like the light (emissive + diffuse)
				engine::rendering::PBRProperties props{};
				props.diffuse[0] = pointData.color.r;
				props.diffuse[1] = pointData.color.g;
				props.diffuse[2] = pointData.color.b;
				props.diffuse[3] = 1.0f;
				// Make the marker visibly emissive to better match the light
				props.emission[0] = pointData.color.r;
				props.emission[1] = pointData.color.g;
				props.emission[2] = pointData.color.b;
				props.emission[3] = 1.0f;
				props.roughness = 1.0f;
				props.metallic = 0.0f;

				auto maybeMat = resourceManager->m_materialManager->createPBRMaterial(std::string("SphereMat_") + instanceName, props, {});
				if (maybeMat.has_value())
				{
					auto matHandle = maybeMat.value()->getHandle();
					// Assign material to all submeshes of this model instance
					for (auto &sm : maybeModelInst.value()->getSubmeshes())
						sm.material = matHandle;
				}

				// Add model node as child, then set its local scale
				newLight->addChild(std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelInst.value()));
				auto children = newLight->getChildren();
				if (!children.empty())
				{
					auto lastChild = std::dynamic_pointer_cast<engine::scene::nodes::ModelRenderNode>(children.back());
					if (lastChild)
						lastChild->getTransform().setLocalScale(glm::vec3(0.03f));
				}
			}
		}

		// Also expose to main light list/UI so they are visible and collectible
		m_flockLights.push_back(newLight);
		m_lightNodes.push_back(newLight);
		// Initial random velocity (small)
		glm::vec3 vel((static_cast<float>(rand()) / RAND_MAX - 0.5f) * 1.0f,
				  (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.5f,
				  (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 1.0f);
		m_flockVelocities.push_back(vel);
		// Per-light noise phase for smooth jitter
		float phase = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159265f;
		m_flockNoisePhases.push_back(phase);
		spdlog::debug("Spawned flock light {} at {} {} {}", i, pos.x, pos.y, pos.z);
	}
	// Ensure flock movement is active after spawn
	m_flockEnabled = true;
	spdlog::info("Flock spawn complete. total flock lights={}", m_flockLights.size());
}

void MainDemoImGuiUI::clearFlock()
{
	for (auto &node : m_flockLights)
	{
		if (node && node->getParent())
			node->getParent()->removeChild(node);
	}
	m_flockLights.clear();
	m_flockVelocities.clear();
	m_flockNoisePhases.clear();
}

void MainDemoImGuiUI::updateFlock(float deltaSeconds)
{
	const float damping = 0.995f;
	const float maxSpeed = 8.0f;
	const float noiseFreq = 0.6f;
	const float noiseAmp = 0.8f;
	const float neighborRadius = 2.5f;
	for (size_t i = 0; i < m_flockLights.size(); ++i)
	{
		auto &node = m_flockLights[i];
		if (!node)
			continue;
		glm::vec3 pos = node->getTransform().getLocalPosition();

		// Per-light smooth noise
		float phase = (i < m_flockNoisePhases.size()) ? m_flockNoisePhases[i] : 0.0f;
		glm::vec3 noiseVec(
			sinf(phase),
			sinf(phase * 1.37f + 1.0f),
			sinf(phase * 1.73f + 2.0f)
		);
		phase += deltaSeconds * noiseFreq;
		if (i < m_flockNoisePhases.size())
			m_flockNoisePhases[i] = phase;
		glm::vec3 jitter = noiseVec * noiseAmp * 0.3f;

		// Boids-like behavior: separation, alignment, cohesion
		glm::vec3 separation(0.0f);
		glm::vec3 alignment(0.0f);
		glm::vec3 cohesion(0.0f);
		int neighborCount = 0;
		for (size_t j = 0; j < m_flockLights.size(); ++j)
		{
			if (i == j) continue;
			auto &other = m_flockLights[j];
			if (!other) continue;
			glm::vec3 otherPos = other->getTransform().getLocalPosition();
			glm::vec3 diff = pos - otherPos;
			float dist = glm::length(diff);
			if (dist < 0.0001f) continue;
			if (dist < neighborRadius)
			{
				// separation (repel)
				separation += glm::normalize(diff) / dist;
				// alignment (match velocity)
				alignment += m_flockVelocities[j];
				// cohesion (move towards average)
				cohesion += otherPos;
				neighborCount++;
			}
		}
		if (neighborCount > 0)
		{
			alignment /= static_cast<float>(neighborCount);
			cohesion = (cohesion / static_cast<float>(neighborCount)) - pos;
		}

		glm::vec3 vel = m_flockVelocities[i];
		glm::vec3 steer(0.0f);
		// weights tuned for stable motion
		steer += separation * 1.5f;
		steer += (alignment - vel) * 0.5f;
		steer += cohesion * 0.6f;
		// gentle pull to global center to keep flock localized, but weak
		glm::vec3 toCenter = m_flockCenter - pos;
		steer += toCenter * (m_flockAttraction * 0.01f);
		// add smooth noise
		steer += jitter;

		vel += steer * deltaSeconds;
		// damping and clamp
		vel *= damping;
		float speed = glm::length(vel);
		if (speed > maxSpeed) vel = glm::normalize(vel) * maxSpeed;

		pos += vel * deltaSeconds;
		node->getTransform().setLocalPosition(pos);
		m_flockVelocities[i] = vel;
	}
}

ImTextureID MainDemoImGuiUI::getOrCreateImGuiTexture(engine::rendering::TextureHandle textureHandle)
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

} // namespace demo
