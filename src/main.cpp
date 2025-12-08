/**
 * Vienna WebGPU Engine - Main Entry Point
 * Using GameEngine with SceneManager for declarative scene setup
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "engine/GameEngine.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/entity/LightNode.h"
#include "engine/scene/entity/ModelRenderNode.h"
#include "engine/scene/entity/UpdateNode.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/Material.h"
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

constexpr float PI = 3.14159265358979323846f;

// Camera orbit state
struct OrbitCameraState
{
	bool active = false;
	glm::vec2 startMouse = glm::vec2(0.0f);
	glm::vec2 previousDelta = glm::vec2(0.0f);
	glm::vec2 velocity = glm::vec2(0.0f);
	
	float azimuth = 0.0f;
	float elevation = 0.3f;
	float distance = 5.0f;
	
	glm::vec3 targetPoint = glm::vec3(0.0f);
	
	float sensitivity = 1.0f;
	float scrollSensitivity = 0.5f;
	float inertiaDecay = 0.92f;
};

void updateOrbitCamera(OrbitCameraState& drag, std::shared_ptr<engine::scene::CameraNode> camera)
{
	// Normalize azimuth to [0, 2π]
	drag.azimuth = fmod(drag.azimuth, 2.0f * PI);
	if (drag.azimuth < 0)
		drag.azimuth += 2.0f * PI;
	
	// Clamp elevation to avoid gimbal lock
	drag.elevation = glm::clamp(drag.elevation, -PI / 2.0f + 0.01f, PI / 2.0f - 0.01f);
	
	// Clamp distance
	drag.distance = glm::clamp(drag.distance, 0.5f, 20.0f);
	
	// Convert spherical coordinates to Cartesian
	float x = cos(drag.elevation) * cos(drag.azimuth);
	float y = sin(drag.elevation);
	float z = cos(drag.elevation) * sin(drag.azimuth);
	
	glm::vec3 position = drag.targetPoint + glm::vec3(x, y, z) * drag.distance;
	
	// Update camera position and look-at
	if (camera && camera->getTransform())
	{
		camera->getTransform()->setLocalPosition(position);
		camera->lookAt(drag.targetPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

void updateDragInertia(OrbitCameraState& drag, std::shared_ptr<engine::scene::CameraNode> camera, float deltaTime)
{
	if (!drag.active && glm::length(drag.velocity) > 1e-4f)
	{
		// Apply inertia
		drag.azimuth += drag.velocity.x * drag.sensitivity * deltaTime;
		drag.elevation += drag.velocity.y * drag.sensitivity * deltaTime;
		
		// Decay velocity
		drag.velocity *= drag.inertiaDecay;
		
		// Update camera position
		updateOrbitCamera(drag, camera);
	}
	else if (!drag.active)
	{
		// Stop completely when velocity is negligible
		drag.velocity = glm::vec2(0.0f);
	}
}

// Custom UpdateNode for orbit camera control
class OrbitCameraController : public engine::scene::entity::UpdateNode
{
public:
	OrbitCameraController(OrbitCameraState& state, std::shared_ptr<engine::scene::CameraNode> camera)
		: m_orbitState(state), m_camera(camera) {}
	
	void update(float deltaTime) override
	{
		auto input = engine()->input();
		if (!input) return;
		
		// Handle mouse drag for camera rotation
		if (input->isMouseButtonPressed(SDL_BUTTON_LEFT))
		{
			if (!m_orbitState.active)
			{
				// Start dragging
				m_orbitState.active = true;
				m_orbitState.startMouse = input->getMousePosition();
				m_orbitState.velocity = glm::vec2(0.0f);
			}
			else
			{
				// Continue dragging
				glm::vec2 delta = input->getMouseDelta();
				m_orbitState.azimuth -= delta.x * 0.005f;
				m_orbitState.elevation += delta.y * 0.005f;
				m_orbitState.velocity = delta * 0.005f;
				updateOrbitCamera(m_orbitState, m_camera);
			}
		}
		else if (m_orbitState.active)
		{
			// Stop dragging
			m_orbitState.active = false;
		}
		
		// Handle mouse wheel for zoom
		glm::vec2 wheel = input->getMouseWheel();
		if (wheel.y != 0.0f)
		{
			m_orbitState.distance -= wheel.y * m_orbitState.scrollSensitivity;
			updateOrbitCamera(m_orbitState, m_camera);
		}
		
		// Apply inertia when not dragging
		updateDragInertia(m_orbitState, m_camera, deltaTime);
	}

private:
	OrbitCameraState& m_orbitState;
	std::shared_ptr<engine::scene::CameraNode> m_camera;
};

int main(int argc, char **argv)
{
	SDL_SetMainReady(); // Tell SDL we're handling main ourselves
	spdlog::info("Vienna WebGPU Engine Starting...");

	// Create and configure the engine
	engine::GameEngine engine;
	
	engine::GameEngineOptions options;
	options.windowWidth = 1280;
	options.windowHeight = 720;
	options.enableVSync = true;
	options.showFrameStats = false; // FPS now shown in UI instead of console
	engine.setOptions(options);

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
	OrbitCameraState orbitState;
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
	engine::rendering::Model::Handle modelHandle;
	bool modelLoaded = false;
	auto maybeModel = resourceManager->m_modelManager->createModel("fourareen.obj");
	if (maybeModel.has_value())
	{
		spdlog::info("Loaded fourareen.obj model");
		modelHandle = maybeModel.value()->getHandle();
		modelLoaded = true;
		
		// Create a ModelRenderNode and add it to the scene
		// The renderer will create GPU resources when needed
		auto modelNode = std::make_shared<engine::scene::entity::ModelRenderNode>(modelHandle);
		rootNode->addChild(modelNode);
		spdlog::info("Added model to scene");
	}

	// Load the scene (makes it active)
	sceneManager->loadScene("Main");

	// Initialize engine early to access ImGuiManager and InputManager
	if (!engine.initialize())
	{
		spdlog::error("Failed to initialize engine!");
		return 1;
	}
	
	// Create an UpdateNode to handle orbit camera input
	auto orbitCameraController = std::make_shared<OrbitCameraController>(orbitState, cameraNode);
	rootNode->addChild(orbitCameraController);
	
	// Flag to control debug rendering
	bool enableDebugRendering = false;

	// Get ImGui manager and add UI callbacks
	auto imguiManager = engine.getImGuiManager();
	
	// Add UI callback for full scene controls
	imguiManager->addFrame([&]() {
		// FPS and Performance window
		ImGui::Begin("Performance");
		ImGui::Text("FPS: %.1f", engine.getFPS());
		ImGui::Text("Frame Time: %.2f ms", engine.getFrameTime());
		ImGui::End();
		
		ImGui::Begin("Lighting & Camera Controls");

		// Shader reload button
		if (ImGui::Button("Reload Shaders (F5)"))
		{
			// TODO: Add shader reload when renderer is accessible
		}

		ImGui::SameLine();
		
		// Debug rendering toggle
		ImGui::Checkbox("Debug Rendering", &enableDebugRendering);
		
		// Add debug transforms to the scene's render collector
		if (enableDebugRendering)
		{
			auto activeScene = sceneManager->getActiveScene();
			if (activeScene)
			{
				auto& collector = const_cast<engine::rendering::RenderCollector&>(activeScene->getRenderCollector());
				
				// Add model transform if loaded
				if (modelLoaded)
				{
					glm::mat4 modelTransform = glm::mat4(1.0f); // Identity at origin
					collector.addDebugTransform(modelTransform);
				}
				
				// Add light transforms
				for (const auto& light : lightNodes)
				{
					if (light && light->getTransform())
					{
						glm::mat4 lightTransform = light->getTransform()->getWorldMatrix();
						collector.addDebugTransform(lightTransform);
					}
				}
			}
		}
		ImGui::Separator();

		// Material properties section  
		if (ImGui::CollapsingHeader("Material Properties"))
		{
			// Try to get the first model's material
			if (!modelLoaded)
			{
				ImGui::Text("No model loaded");
			}
			else
			{
				auto firstModelOpt = resourceManager->m_modelManager->get(modelHandle);
				if (!firstModelOpt.has_value())
				{
					ImGui::Text("Model not found in manager");
				}
				else
				{
					engine::rendering::Model::Ptr firstModel = firstModelOpt.value();
					auto materialHandle = firstModel->getMaterial();
					auto materialOpt = resourceManager->m_materialManager->get(materialHandle);
					
					if (!materialOpt.has_value())
					{
						ImGui::Text("Material not found in manager");
					}
					else
					{
						engine::rendering::Material::Ptr material = materialOpt.value();
						engine::rendering::Material::MaterialProperties materialProperties = material->getProperties();
						bool materialsChanged = false;

						materialsChanged |= ImGui::ColorEdit3("Diffuse (Kd)", materialProperties.diffuse);
						materialsChanged |= ImGui::ColorEdit3("Specular (Ks)", materialProperties.specular);
						materialsChanged |= ImGui::SliderFloat("Roughness", &materialProperties.roughness, 0.0f, 1.0f);
						materialsChanged |= ImGui::SliderFloat("Metallic", &materialProperties.metallic, 0.0f, 1.0f);

						if (materialsChanged)
						{
							material->setProperties(materialProperties);
						}
					}
				}
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
							if (ImGui::SliderFloat("Cone Angle", &spotAngle, 0.1f, 2.0f))
							{
								light->setSpotAngle(spotAngle);
							}

							float spotSoftness = light->getLightData().spot_softness;
							if (ImGui::SliderFloat("Edge Softness", &spotSoftness, 0.0f, 0.95f, "%.2f"))
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
				updateOrbitCamera(orbitState, cameraNode);
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

		ImGui::End();
	});

	// Run the engine (blocks until window closed)
	engine.run();

	spdlog::info("Engine shut down successfully");
	return 0;
}
