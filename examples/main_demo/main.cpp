/**
 * Vienna WebGPU Engine - Main Entry Point
 * Using GameEngine with SceneManager for declarative scene setup
 */

#include "engine/EngineMain.h"
// ^ This has to be on top to define SDL_MAIN_HANDLED ^
#include "OrbitCamera.h"
#include <set>

int main(int argc, char **argv)
{
	spdlog::info("Vienna WebGPU Engine Starting...");

	engine::GameEngineOptions options;
	options.windowWidth = 1152;
	options.windowHeight = 648;
	options.enableVSync = false;

	// Create and configure the engine
	engine::GameEngine engine;
	engine.initialize(options);

	// Get managers for setup
	auto sceneManager = engine.getSceneManager();
	auto resourceManager = engine.getResourceManager();
	auto context = engine.getContext();

	// Create main scene
	auto mainScene = sceneManager->createScene("Main");
	auto rootNode = mainScene->getRoot();

	// Setup camera
	auto cameraNode = mainScene->getActiveCamera();
	if (cameraNode)
	{
		cameraNode->setFov(45.0f);
		cameraNode->setAspect(static_cast<float>(options.windowWidth) / static_cast<float>(options.windowHeight));
		cameraNode->setNearFar(0.1f, 100.0f);
		cameraNode->setPerspective(true);
		cameraNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 5.0f));
		cameraNode->lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	// Initialize orbit camera state
	demo::OrbitCameraState orbitState;
	orbitState.distance = 5.0f;
	orbitState.azimuth = 0.0f;
	orbitState.elevation = 0.3f;

	// Create directional light
	auto lightNode = std::make_shared<engine::scene::entity::LightNode>();
	lightNode->setLightType(1); // Directional
	lightNode->setColor(glm::vec3(1.0f, 1.0f, 1.0f));
	lightNode->setIntensity(1.0f);

	// Set default direction angles (in degrees)
	float pitchDegrees = 140.0f;
	float yawDegrees = -30.0f;
	float rollDegrees = 0.0f;
	glm::quat rotation = glm::quat(glm::radians(glm::vec3(pitchDegrees, yawDegrees, rollDegrees)));
	lightNode->getTransform()->setLocalRotation(rotation);

	rootNode->addChild(std::static_pointer_cast<engine::scene::entity::Node>(lightNode));

	// Track all lights and their UI angles
	std::vector<std::shared_ptr<engine::scene::entity::LightNode>> lightNodes;
	lightNodes.push_back(lightNode);
	std::map<size_t, glm::vec3> lightDirectionsUI;
	lightDirectionsUI[0] = glm::vec3(pitchDegrees, yawDegrees, rollDegrees);

	// Load model (CPU-side only, GPU resources will be created by the renderer)
	engine::rendering::Model::Handle modelHandleFourareen;
	engine::rendering::Model::Handle modelHandleCylinder;
	auto maybeModelFourareen = resourceManager->m_modelManager->createModel("fourareen.obj");
	auto maybeModelCylinder = resourceManager->m_modelManager->createModel("cylinder.obj");
	if (!maybeModelFourareen.has_value())
	{
		spdlog::error("Failed to load fourareen.obj model");
		return -1;
	}
	if (!maybeModelCylinder.has_value())
	{
		spdlog::error("Failed to load cylinder.obj model");
		return -1;
	}
	spdlog::info("Loaded fourareen.obj model");
	spdlog::info("Loaded cylinder.obj model");
	modelHandleFourareen = maybeModelFourareen.value()->getHandle();
	modelHandleCylinder = maybeModelCylinder.value()->getHandle();

	auto modelNode = std::make_shared<engine::scene::entity::ModelRenderNode>(modelHandleFourareen);
	modelNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 0.0f, -2.0f));
	rootNode->addChild(modelNode);

	modelNode = std::make_shared<engine::scene::entity::ModelRenderNode>(modelHandleCylinder);
	modelNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	rootNode->addChild(modelNode);

	modelNode = std::make_shared<engine::scene::entity::ModelRenderNode>(modelHandleFourareen);
	modelNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 0.0f, 2.0f));
	rootNode->addChild(modelNode);

	// Load the scene (makes it active)
	sceneManager->loadScene("Main");

	// Create an UpdateNode to handle orbit camera input
	auto orbitCameraController = std::make_shared<demo::OrbitCameraController>(orbitState, cameraNode);
	rootNode->addChild(orbitCameraController);

	// Get ImGui manager and add UI callbacks
	auto imguiManager = engine.getImGuiManager();

	// Add UI callback for full scene controls
	imguiManager->addFrame([&]()
						   {
		// FPS and Performance window
		ImGui::Begin("Performance");
		ImGui::Text("FPS: %.1f", engine.getFPS());
		ImGui::Text("Frame Time: %.2f ms", engine.getFrameTime());
		ImGui::End();

		ImGui::Begin("Lighting & Camera Controls");

		// Shader reload button
		if (ImGui::Button("Reload Shaders (F5)"))
		{
			engine.getContext()->shaderRegistry().reloadAllShaders();
			// ToDo: Recreate pipelines
		}

		ImGui::SameLine();

		// Debug rendering toggle
		static bool showDebugRendering = false;
		static bool prevDebugState = false;
		ImGui::Checkbox("Debug Rendering", &showDebugRendering);

		// Enable/disable debug on nodes when checkbox state changes
		if (showDebugRendering != prevDebugState)
		{
			// Toggle debug on all light nodes
			for (auto &light : lightNodes)
			{
				if (light)
				{
					light->setDebugEnabled(showDebugRendering);
				}
			}

			// Toggle debug on model node if it exists
			if (rootNode)
			{
				// Find the model node in the scene graph
				auto children = rootNode->getChildren();
				for (auto &child : children)
				{
					// Check if it's a SpatialNode (ModelRenderNode inherits from SpatialNode)
					auto spatialNode = std::dynamic_pointer_cast<engine::scene::SpatialNode>(child);
					if (spatialNode)
					{
						spatialNode->setDebugEnabled(showDebugRendering);
					}
				}
			}

			prevDebugState = showDebugRendering;
		}

		ImGui::Separator();

		// Material properties section
		if (ImGui::CollapsingHeader("Material Properties") && rootNode)
		{
			auto children = rootNode->getChildrenOfType<engine::scene::entity::ModelRenderNode>();
			std::set<engine::rendering::MaterialHandle> materials;
			for (auto &child : children)
			{
				auto modelHandle = child->getModel();
				auto firstModelOpt = resourceManager->m_modelManager->get(modelHandle);
				if (firstModelOpt.has_value())
				{
					engine::rendering::Model::Ptr firstModel = firstModelOpt.value();
					auto submesh = firstModel->getSubmeshes();
					for (auto const &sm : submesh)
					{
						auto materialHandle = sm.material;
						materials.emplace(materialHandle);
					}
				}
			}
			for (const auto &materialHandle : materials)
			{
    			ImGui::PushID(materialHandle.id());
				auto materialOpt = resourceManager->m_materialManager->get(materialHandle);
				ImGui::Separator();
				if (!materialOpt.has_value())
				{
					ImGui::Text("Material not found in manager");
				}
				else
				{
					engine::rendering::Material::Ptr material = materialOpt.value();
					ImGui::Text("Material Handle: %s", material->getName().value_or("Unnamed").c_str());
					engine::rendering::Material::MaterialProperties materialProperties = material->getProperties();
					bool materialsChanged = false;

					materialsChanged |= ImGui::ColorEdit4("Diffuse (Kd)", materialProperties.diffuse);
					materialsChanged |= ImGui::ColorEdit4("Specular (Ks)", materialProperties.specular);
					materialsChanged |= ImGui::ColorEdit4("Emission (Ke)", materialProperties.emission);
					materialsChanged |= ImGui::ColorEdit4("Transmittance (Kt)", materialProperties.transmittance);
					materialsChanged |= ImGui::SliderFloat("Roughness", &materialProperties.roughness, 0.0f, 1.0f);
					materialsChanged |= ImGui::SliderFloat("Shininess", &materialProperties.shininess, 0.0f, 256.0f);
					materialsChanged |= ImGui::SliderFloat("Metallic", &materialProperties.metallic, 0.0f, 1.0f);
					materialsChanged |= ImGui::SliderFloat("IOR", &materialProperties.ior, 0.0f, 5.0f);

					if (materialsChanged)
					{
						material->setProperties(materialProperties);
					}
				}
    			ImGui::PopID();
			}
		}
		// Lights section
		if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Add light button
			if (ImGui::Button("Add Light"))
			{
				// Create a new LightNode
				auto newLight = std::make_shared<engine::scene::entity::LightNode>();
				uint32_t lightType = lightNodes.empty() ? 1 : 2; // 1=directional, 2=point
				newLight->setLightType(lightType);
				newLight->setColor(glm::vec3(1.0f, 1.0f, 1.0f));
				newLight->setIntensity(1.0f);

				if (lightType == 1) // Directional
				{
					float pitch = 140.0f, yaw = -30.0f, roll = 0.0f;
					glm::quat rot = glm::quat(glm::radians(glm::vec3(pitch, yaw, roll)));
					newLight->getTransform()->setLocalRotation(rot);
					lightDirectionsUI[lightNodes.size()] = glm::vec3(pitch, yaw, roll);
				}
				else // Point
				{
					newLight->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 0.0f));
				}

				rootNode->addChild(newLight);
				lightNodes.push_back(newLight);
				spdlog::info("Added light node");
			}

			// Light list
			for (size_t i = 0; i < lightNodes.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));

				auto &light = lightNodes[i];
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
					// Light type
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

						// Initialize UI angles if not present
						if (lightDirectionsUI.find(i) == lightDirectionsUI.end())
						{
							glm::quat rotation = transform->getRotation();
							glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));
							lightDirectionsUI[i] = eulerAngles;
						}

						glm::vec3 &angles = lightDirectionsUI[i];

						// Position control for point and spot lights
						if (light->getLightType() > 1)
						{
							if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
							{
								transform->setLocalPosition(position);
							}
						}

						// Direction control for directional and spot lights
						if (light->getLightType() == 1 || light->getLightType() == 3)
						{
							if (ImGui::DragFloat3("Direction (degrees)", glm::value_ptr(angles), 0.5f))
							{
								glm::quat rot = glm::quat(glm::radians(angles));
								transform->setLocalRotation(rot);
							}
						}

						// Spot angle for spot lights
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

				// Handle removal
				if (shouldRemove)
				{
					if (light->getParent())
						light->getParent()->removeChild(light);

					lightNodes.erase(lightNodes.begin() + i);
					lightDirectionsUI.erase(i);

					// Re-index remaining lights
					std::map<size_t, glm::vec3> newDirectionsUI;
					for (const auto &[idx, ang] : lightDirectionsUI)
					{
						if (idx > i)
							newDirectionsUI[idx - 1] = ang;
						else
							newDirectionsUI[idx] = ang;
					}
					lightDirectionsUI = newDirectionsUI;
					break;
				}
			}
		}

		// Camera controls section
		if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen) && cameraNode)
		{
			// Get current camera position
			glm::vec3 cameraPos = cameraNode->getTransform() ? cameraNode->getTransform()->getLocalPosition() : glm::vec3(0.0f);
			ImGui::Text("Position: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);

			// Display distance from origin
			float camDistance = glm::length(cameraPos);
			ImGui::Text("Distance from origin: %.2f", camDistance);

			// Camera orientation vectors
			if (auto transform = cameraNode->getTransform())
			{
				glm::vec3 forward = transform->forward();
				glm::vec3 up = transform->up();
				glm::vec3 right = transform->right();

				ImGui::Separator();
				ImGui::Text("Orientation Vectors:");
				ImGui::Text("Forward: (%.2f, %.2f, %.2f)", forward.x, forward.y, forward.z);
				ImGui::Text("Up: (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
				ImGui::Text("Right: (%.2f, %.2f, %.2f)", right.x, right.y, right.z);
				ImGui::Text("Azimuth/Elevation: (%.2f / %.2f)", orbitState.azimuth, orbitState.elevation);

				// Extract rotation as euler angles
				glm::quat rotation = transform->getRotation();
				glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));

				// Fix discontinuities in euler angle representation
				if (eulerAngles.x > 90.0f)
					eulerAngles.x -= 360.0f;
				if (eulerAngles.y > 180.0f)
					eulerAngles.y -= 360.0f;
				if (eulerAngles.z > 180.0f)
					eulerAngles.z -= 360.0f;

				ImGui::Text("Rotation (degrees): (%.1f, %.1f, %.1f)", eulerAngles.x, eulerAngles.y, eulerAngles.z);
			}

			ImGui::Separator();

			// Camera distance slider
			float zoomPercentage = (camDistance - 2.0f) / 8.0f * 100.0f;
			zoomPercentage = glm::clamp(zoomPercentage, 0.0f, 100.0f);

			if (ImGui::SliderFloat("Camera Distance", &zoomPercentage, 0.0f, 100.0f, "%.0f%%"))
			{
				float newDistance = (zoomPercentage / 100.0f) * 8.0f + 2.0f;
				orbitState.distance = newDistance;
				demo::updateOrbitCamera(orbitState, cameraNode);
			}

			// Camera reset controls
			if (ImGui::Button("Look At Origin"))
			{
				cameraNode->lookAt(glm::vec3(0.0f));
			}

			ImGui::SameLine();

			if (ImGui::Button("Reset Camera"))
			{
				cameraNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 5.0f));
				cameraNode->lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

				// Reset orbit state
				glm::vec3 camPos = cameraNode->getTransform()->getLocalPosition();
				glm::vec3 toCam = camPos - orbitState.targetPoint;
				orbitState.distance = glm::length(toCam);
				if (orbitState.distance > 1e-5f)
				{
					glm::vec3 dir = toCam / orbitState.distance;
					orbitState.elevation = std::asin(dir.y);
					orbitState.azimuth = std::atan2(dir.z, dir.x);
				}
			}
		}

		ImGui::End(); });

	// Run the engine (blocks until window closed)
	engine.run();

	spdlog::info("Engine shut down successfully");
	return 0;
}
