// This file is based on the "Learn WebGPU for C++" tutorial by Elie Michel (https://github.com/eliemichel/LearnWebGPU).
// Significant modifications, refactoring, and extensions have been made for this project.
// Original code © 2022-2024 Elie Michel, MIT License.

/**
 * TODO:
 * 1. Facotry schlauer machen in bindgroup verkettungen
2. Überlegung wie ich das mache wo materials ihrhe sachen rien schrieben oder ob der renderer das ausliest oder wie wo was 
 */

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/polar_coordinates.hpp>

#include <SDL.h>
#include <sdl2webgpu.h>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>
#include <imgui.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "engine/Application.h"
#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/scene/CameraNode.h"
#include "engine/scene/entity/LightNode.h"
#include "engine/scene/entity/ModelRenderNode.h"
#include "engine/scene/entity/Node.h"

using namespace wgpu;
using engine::rendering::Vertex;

constexpr float PI = 3.14159265358979323846f;

namespace engine
{
using engine::rendering::BindGroupLayoutIndex;

Application::Application()
{
#ifdef DEBUG_ROOT_DIR
	engine::core::PathProvider::initialize(DEBUG_ROOT_DIR, ASSETS_ROOT_DIR);
#else
	PathProvider::initialize();
#endif
	spdlog::info("EXE Root: {}", engine::core::PathProvider::getExecutableRoot().string());
	spdlog::info("LIB Root: {}", engine::core::PathProvider::getLibraryRoot().string());
	m_resourceManager = std::make_shared<engine::resources::ResourceManager>(engine::core::PathProvider::getResourceRoot());
	m_context = std::make_shared<engine::rendering::webgpu::WebGPUContext>();

	// Initialize the UI angles map
	m_lightDirectionsUI.clear();
}

///////////////////////////////////////////////////////////////////////////////
// Public methods

bool Application::onInit()
{
	// Create SDL window first
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0)
	{
		std::cerr << "Could not initialize SDL2: " << SDL_GetError() << std::endl;
		return false;
	}
	Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
	int width = 640, height = 480;
	m_window = SDL_CreateWindow("Learn WebGPU", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, windowFlags);
	if (!m_window)
	{
		std::cerr << "Could not open window!" << std::endl;
		return false;
	}
	// Now initialize context with window
	m_context->initialize(m_window);
	
	// Create the scene and camera
	m_scene = std::make_shared<engine::scene::Scene>();
	
	// Get window size for camera aspect ratio
	SDL_GetWindowSize(m_window, &m_currentWidth, &m_currentHeight);
	if (!initRenderer())
		return false;
	if (!initGeometry())
		return false;
	if (!initUniforms())
		return false;
	if (!initGui())
		return false;
	return true;
}

void Application::onFrame()
{
	// Calculate delta time for node updates
	static Uint64 lastFrameTime = SDL_GetTicks64();
	Uint64 currentTime = SDL_GetTicks64();
	float deltaTime = (currentTime - lastFrameTime) / 1000.0f;
	lastFrameTime = currentTime;
	
	processSDLEvents(deltaTime);
	updateDragInertia(deltaTime);

	// Process frame lifecycle using the Scene system
	if (m_scene)
	{
		m_scene->onFrame(deltaTime);
	}

	// Update frame uniforms from active camera
	auto cam = m_scene ? m_scene->getActiveCamera() : nullptr;
	if (cam)
	{
		m_frameUniforms.viewMatrix = cam->getViewMatrix();
		m_frameUniforms.projectionMatrix = cam->getProjectionMatrix();
		m_frameUniforms.cameraWorldPosition = cam->getPosition();
	}
	m_frameUniforms.time = static_cast<float>(static_cast<double>(SDL_GetTicks64() / 1000.0));

	// Use Renderer to render the frame
	if (m_renderer && m_scene)
	{
		// Update renderer's frame uniforms
		m_renderer->updateFrameUniforms(m_frameUniforms);
		
		// Render the frame with UI callback - Renderer owns the frame bind group
		m_renderer->renderFrame(
			m_scene->getRenderCollector(),
			[this](wgpu::RenderPassEncoder renderPass) {
				this->updateGui(renderPass);
			}
		);
		
		// Post-render cleanup phase
		m_scene->postRender();
	}

#ifdef WEBGPU_BACKEND_DAWN
	// Check for pending error callbacks
	m_context->getDevice().tick();
#endif
}

void Application::processSDLEvents(float deltaTime)
{

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		ImGui_ImplSDL2_ProcessEvent(&event);

		ImGuiIO &io = ImGui::GetIO();
		if (io.WantCaptureMouse || io.WantCaptureKeyboard)
		{
			continue;
		}
		switch (event.type)
		{
		case SDL_QUIT:
			m_shouldClose = true;
			break;

		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOW_FULLSCREEN)
			{
				onResize(); // No need to fetch width/height unless you want to
			}
			break;

		case SDL_MOUSEMOTION:
			onMouseMove(event.motion.x, event.motion.y, deltaTime);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			onMouseButton(
				event.button.button,
				event.button.state == SDL_PRESSED ? 1 : 0,
				event.button.x,
				event.button.y
			);
			break;

		case SDL_MOUSEWHEEL:
			onScroll(event.wheel.x, event.wheel.y, deltaTime);
			break;

		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_F5)
			{
				// F5 key pressed - reload shaders via Renderer
				if (m_renderer)
				{
					m_renderer->pipelineManager().reloadAllPipelines();
				}
			}
			else if (event.key.keysym.sym == SDLK_UP)
			{
				m_drag.elevation += 0.05f;
			}
			else if (event.key.keysym.sym == SDLK_DOWN)
			{
				m_drag.elevation -= 0.05f;
			}
			else if (event.key.keysym.sym == SDLK_LEFT)
			{
				m_drag.azimuth += 0.05f;
			}
			else if (event.key.keysym.sym == SDLK_RIGHT)
			{
				m_drag.azimuth -= 0.05f;
			}
			updateOrbitCamera();
			// Fall through to handle other key events
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
			// handle key event
			break;
		}
	}
}

void Application::onFinish()
{
	terminateGui();
	terminateUniforms();
	terminateGeometry();

	// Note: Bind groups are now managed by the Renderer

	m_context->bindGroupFactory().cleanup();
	terminateRenderer();
}

bool Application::isRunning()
{
	return !m_shouldClose;
}

void Application::onResize()
{
	int width, height;
	SDL_GL_GetDrawableSize(m_window, &width, &height);
	
	// Update stored dimensions
	m_currentWidth = width;
	m_currentHeight = height;
	
	m_context->surfaceManager().updateIfNeeded(width, height);
	m_renderer->onResize(width, height);
	
	auto cam = m_scene ? m_scene->getActiveCamera() : nullptr;
	if (cam)
	{
		// Update aspect ratio based on current window size
		float aspect = m_currentWidth / static_cast<float>(m_currentHeight);
		cam->setAspect(aspect);
	}
}

void Application::onMouseMove(double xpos, double ypos, float deltaTime)
{
	if (m_drag.active)
	{
		// Calculate delta from starting position
		glm::vec2 currentMouse = glm::vec2((float)xpos, (float)ypos);
		glm::vec2 delta = (currentMouse - m_drag.startMouse) * m_drag.sensitivity * deltaTime;
		// Use sensitivity parameter for orbit speed
		m_drag.azimuth -= delta.x;
		m_drag.elevation += delta.y;
		m_drag.elevation = glm::clamp(m_drag.elevation, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);

		// Update camera position based on new orbital parameters
		updateOrbitCamera();
		m_drag.startMouse = currentMouse;

		// Inertia calculation for when the user releases the mouse
		m_drag.velocity = delta - m_drag.previousDelta;
		m_drag.previousDelta = delta;
	}
}
void Application::onMouseButton(int button, bool pressed, int x, int y)
{
	if (pressed && button == SDL_BUTTON_LEFT)
	{
		m_drag.active = true;
		int xpos, ypos;
		SDL_GetMouseState(&xpos, &ypos);
		// Store without inversion to match onMouseMove
		m_drag.startMouse = glm::vec2((float)xpos, (float)ypos);
		m_drag.previousDelta = glm::vec2(0.0f);
	}
	else if (!pressed && button == SDL_BUTTON_LEFT)
	{
		m_drag.active = false;
	}
}

void Application::onScroll(double /* xoffset */, double yoffset, float deltaTime)
{
	// Change the orbit distance - negative yoffset means zoom in
	m_drag.distance -= static_cast<float>(yoffset) * m_drag.scrollSensitivity * deltaTime;

	// Update camera position based on new distance
	updateOrbitCamera();
}

///////////////////////////////////////////////////////////////////////////////
// Private methods

#ifdef __EMSCRIPTEN__
EM_JS(void, setCanvasNativeSize, (int width, int height), {
	Module.setCanvasSize(width, height, true);
});
#endif


bool Application::initRenderer()
{
	// Create the renderer
	m_renderer = std::make_unique<engine::rendering::Renderer>(m_context);

	if (!m_renderer->initialize())
	{
		spdlog::error("Failed to initialize Renderer!");
		return false;
	}
	
	return true;
}

void Application::terminateRenderer()
{
	m_renderer.reset();
}

bool Application::initGeometry()
{
	// List of model files to load
	std::vector<std::string> modelPaths = {
		engine::core::PathProvider::getResource("fourareen.obj").string(),
		// Add more model paths here as needed
	};

	m_webgpuModels.clear();
	if (!m_resourceManager || !m_resourceManager->m_modelManager)
	{
		spdlog::error("ResourceManager or ModelManager not available!");
		return false;
	}

	// Create WebGPU models and add them as ModelRenderNodes to the scene
	engine::rendering::webgpu::WebGPUModelFactory modelFactory(*m_context);
	for (const auto &modelPath : modelPaths)
	{
		auto modelOpt = m_resourceManager->m_modelManager->createModel(modelPath);
		if (!modelOpt || !*modelOpt)
		{
			spdlog::error("Could not load model: {}", modelPath);
			continue;
		}

		auto webgpuModel = modelFactory.createFrom(*(modelOpt.value()));
		if (webgpuModel)
		{
			m_webgpuModels.push_back(webgpuModel);
			m_material = webgpuModel->getMaterial();

			// Create a ModelRenderNode and add it to the scene
			auto modelNode = std::make_shared<engine::scene::entity::ModelRenderNode>(
				modelOpt.value()->getHandle()
			);

			auto rootNode = m_scene->getRoot();
			if (rootNode)
			{
				rootNode->addChild(modelNode);
				spdlog::info("Added ModelRenderNode to scene for: {}", modelPath);
			}
		}
		else
		{
			spdlog::error("Could not create WebGPUModel for: {}", modelPath);
		}
	}

	return !m_webgpuModels.empty();
}

void Application::terminateGeometry()
{
	// Clear WebGPUModels collection first
	m_webgpuModels.clear();
}

void Application::resetCamera()
{
	auto cam = m_scene ? m_scene->getActiveCamera() : nullptr;
	if (!cam || !cam->getTransform())
		return;

	glm::vec3 cameraPosition = glm::vec3(0.0f, 2.0f, -m_drag.distance);
	cam->getTransform()->setLocalPosition(cameraPosition);
	cam->lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	// Set orbit parameters from current position
	glm::vec3 camPos = cam->getTransform()->getLocalPosition();
	glm::vec3 toCam = camPos - m_drag.targetPoint;
	m_drag.distance = glm::length(toCam);
	if (m_drag.distance > 1e-5f)
	{
		glm::vec3 dir = toCam / m_drag.distance;
		m_drag.elevation = std::asin(dir.y);
		m_drag.azimuth = std::atan2(dir.z, dir.x);
	}
}

bool Application::initUniforms()
{
	if (!m_scene)
	{
		spdlog::error("Cannot initialize uniforms: scene is null");
		return false;
	}

	// Note: Frame uniforms and bind groups are now managed by the Renderer
	// This function kept for backwards compatibility

	// Create root node if it doesn't exist
	if (!m_scene->getRoot())
	{
		auto rootNode = std::make_shared<engine::scene::entity::Node>();
		m_scene->setRoot(rootNode);
	}

	return true;
}

void Application::terminateUniforms()
{
	// Note: Frame uniforms are now managed by the Renderer
	// This function kept for backwards compatibility
}

void Application::updateProjectionMatrix()
{
	auto cam = m_scene ? m_scene->getActiveCamera() : nullptr;
	if (cam)
	{
		// Update aspect ratio based on current window size
		float aspect = m_currentWidth / static_cast<float>(m_currentHeight);
		cam->setAspect(aspect);
	}
}

void Application::updateDragInertia(float deltaTime)
{
	if (!m_drag.active && glm::length(m_drag.velocity) > 1e-4f)
	{
		// Apply inertia
		m_drag.azimuth += m_drag.velocity.x * m_drag.sensitivity * deltaTime;
		m_drag.elevation += m_drag.velocity.y * m_drag.sensitivity * deltaTime;

		// Decay velocity
		m_drag.velocity *= m_drag.inertiaDecay;

		// Update camera position
		updateOrbitCamera();
	}
	else if (!m_drag.active)
	{
		// Stop completely when velocity is negligible
		m_drag.velocity = glm::vec2(0.0f);
	}
}

bool Application::initGui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(m_window, nullptr);
	ImGui_ImplWGPU_Init(m_context->getDevice(), 3, m_context->getSwapChainFormat(), m_depthTextureFormat);
	return true;
}

void Application::terminateGui()
{
	ImGui_ImplSDL2_Shutdown();
	ImGui_ImplWGPU_Shutdown();
}

void Application::updateGui(RenderPassEncoder renderPass)
{
	// Start the Dear ImGui frame
	ImGui_ImplWGPU_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	// Build our UI
	ImGui::Begin("Lighting & Camera Controls");

	// Shader reload button
	if (ImGui::Button("Reload Shaders (F5)"))
	{
		if (m_renderer)
		{
			m_renderer->pipelineManager().reloadAllPipelines();
		}
	}

	ImGui::SameLine();
	
	// Debug rendering toggle (functionality to be implemented)
	static bool showDebugRendering = false;
	ImGui::Checkbox("Debug Rendering", &showDebugRendering);
	// TODO: Connect to Renderer's debug visualization when implemented

	ImGui::Separator();

	// Material properties section
	if (ImGui::CollapsingHeader("Material Properties") && m_material)
	{
		auto materialProperties = m_material->getCPUObject().getProperties();
		bool materialsChanged = false;

		materialsChanged |= ImGui::ColorEdit3("Diffuse (Kd)", materialProperties.diffuse);
		materialsChanged |= ImGui::ColorEdit3("Specular (Ks)", materialProperties.specular);
		materialsChanged |= ImGui::SliderFloat("Roughness", &materialProperties.roughness, 0.0f, 1.0f);
		materialsChanged |= ImGui::SliderFloat("Metallic", &materialProperties.metallic, 0.0f, 1.0f);

		if (materialsChanged)
		{
			m_material->getCPUObject().setProperties(materialProperties);
		}
	}

	// Lights section
	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Add light button
		if (ImGui::Button("Add Light"))
		{
			addLight();
		}

		// Light list
		for (size_t i = 0; i < m_lightNodes.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			auto &lightNode = m_lightNodes[i];
			if (!lightNode)
			{
				ImGui::PopID();
				continue;
			}

			const char *lightTypeNames[] = {"Ambient", "Directional", "Point", "Spot"};

			// Create light header with type dropdown and remove button
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
				int currentType = static_cast<int>(lightNode->getLightType());
				if (ImGui::Combo("Type", &currentType, lightTypeNames, 4))
				{
					lightNode->setLightType(static_cast<uint32_t>(currentType));
				}

				glm::vec3 color = lightNode->getColor();
				if (ImGui::ColorEdit3("Color", glm::value_ptr(color)))
				{
					lightNode->setColor(color);
				}

				float intensity = lightNode->getIntensity();
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 5.0f))
				{
					lightNode->setIntensity(intensity);
				}

				auto transform = lightNode->getTransform();
				if (!transform)
				{
					ImGui::TreePop();
					ImGui::PopID();
					continue;
				}

				glm::vec3 position = transform->getLocalPosition();

				// Initialize UI angles if not present
				if (m_lightDirectionsUI.find(i) == m_lightDirectionsUI.end())
				{
					glm::quat rotation = transform->getRotation();
					glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));
					m_lightDirectionsUI[i] = eulerAngles;
				}

				glm::vec3 &angles = m_lightDirectionsUI[i];

				// Position control for point and spot lights
				if (lightNode->getLightType() > 1)
				{
					if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
					{
						transform->setLocalPosition(position);
					}
				}

				// Direction control for directional and spot lights
				if (lightNode->getLightType() == 1 || lightNode->getLightType() == 3)
				{
					if (ImGui::DragFloat3("Direction (degrees)", glm::value_ptr(angles), 0.5f))
					{
						// Build rotation from Euler angles
						glm::quat rotation = glm::quat(glm::radians(angles));
						transform->setLocalRotation(rotation);
					}
				}

				// Spot angle for spot lights
				if (lightNode->getLightType() == 3)
				{
					float spotAngle = lightNode->getLightData().spot_angle;
					if (ImGui::SliderFloat("Cone Angle", &spotAngle, 0.1f, 2.0f))
					{
						lightNode->setSpotAngle(spotAngle);
					}

					float spotSoftness = lightNode->getLightData().spot_softness;
					if (ImGui::SliderFloat("Edge Softness", &spotSoftness, 0.0f, 0.95f, "%.2f"))
					{
						lightNode->setSpotSoftness(spotSoftness);
					}
				}

				ImGui::TreePop();
			}

			ImGui::PopID();

			// Handle removal after PopID
			if (shouldRemove)
			{
				removeLight(i);
				break;
			}
		}
	}

	auto cam = m_scene ? m_scene->getActiveCamera() : nullptr;
	// Camera controls section
	if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen) && cam)
	{
		// Get current camera position
		glm::vec3 cameraPos = cam->getTransform() ? cam->getTransform()->getLocalPosition() : glm::vec3(0.0f);
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);

		// Display distance from origin
		float camDistance = glm::length(cameraPos);
		ImGui::Text("Distance from origin: %.2f", camDistance);

		// Camera orientation vectors
		if (auto transform = cam->getTransform())
		{
			glm::vec3 forward = transform->forward();
			glm::vec3 up = transform->up();
			glm::vec3 right = transform->right();

			ImGui::Separator();
			ImGui::Text("Orientation Vectors:");
			ImGui::Text("Forward: (%.2f, %.2f, %.2f)", forward.x, forward.y, forward.z);
			ImGui::Text("Up: (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
			ImGui::Text("Right: (%.2f, %.2f, %.2f)", right.x, right.y, right.z);
			ImGui::Text("Azimuth/Elevation: (%.2f / %.2f)", m_drag.azimuth, m_drag.elevation);

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
			m_drag.distance = newDistance;
			updateOrbitCamera();
		}

		// Camera reset controls
		if (ImGui::Button("Look At Origin"))
		{
			cam->lookAt(glm::vec3(0.0f));
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset Camera"))
		{
			resetCamera();
		}
	}

	ImGui::End();

	// Render UI
	ImGui::Render();
	ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
}

void Application::addLight()
{
	// Create a new LightNode
	auto lightNode = std::make_shared<engine::scene::entity::LightNode>();

	// Set light type (first light is directional, others are point)
	uint32_t lightType = m_lightNodes.empty() ? 1 : 2; // 1=directional, 2=point
	lightNode->setLightType(lightType);

	// Set default properties
	lightNode->setColor(glm::vec3(1.0f, 1.0f, 1.0f));
	lightNode->setIntensity(1.0f);
	lightNode->setSpotAngle(0.5f);
	lightNode->setSpotSoftness(0.2f);

	// Set default position and rotation
	if (lightType == 1) // Directional
	{
		// Set default direction angles (in degrees)
		float pitchDegrees = 140.0f;
		float yawDegrees = -30.0f;
		float rollDegrees = 0.0f;

		// Create rotation from euler angles
		glm::quat rotation = glm::quat(glm::radians(glm::vec3(pitchDegrees, yawDegrees, rollDegrees)));
		lightNode->getTransform()->setLocalRotation(rotation);
		lightNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 0.0f, 0.0f));

		// Store UI angles
		size_t newLightIndex = m_lightNodes.size();
		m_lightDirectionsUI[newLightIndex] = glm::vec3(pitchDegrees, yawDegrees, rollDegrees);
	}
	else // Point light
	{
		lightNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 0.0f));
	}

	// Add the light node to the scene
	auto rootNode = m_scene->getRoot();
	if (rootNode)
	{
		rootNode->addChild(lightNode);

		// Add to our tracking vector
		m_lightNodes.push_back(lightNode);

		spdlog::info("Added light node (type: {})", lightType == 1 ? "directional" : "point");
	}
}

void Application::removeLight(size_t index)
{
	if (index >= m_lightNodes.size())
	{
		spdlog::warn("Cannot remove light at index {}: out of bounds", index);
		return;
	}

	// Remove from scene graph
	auto lightNode = m_lightNodes[index];
	if (lightNode && lightNode->getParent())
	{
		lightNode->getParent()->removeChild(lightNode);
	}

	// Remove from tracking vector
	m_lightNodes.erase(m_lightNodes.begin() + index);

	// Remove UI angles for this light
	m_lightDirectionsUI.erase(index);

	// Re-index remaining lights in the UI map
	std::map<size_t, glm::vec3> newDirectionsUI;
	for (const auto &[idx, angles] : m_lightDirectionsUI)
	{
		if (idx > index)
		{
			newDirectionsUI[idx - 1] = angles;
		}
		else
		{
			newDirectionsUI[idx] = angles;
		}
	}
	m_lightDirectionsUI = newDirectionsUI;

	spdlog::info("Removed light at index {}", index);
}

void Application::updateOrbitCamera()
{
	// Normalize azimuth to [0, 2π]
	m_drag.azimuth = fmod(m_drag.azimuth, 2.0f * glm::pi<float>());
	if (m_drag.azimuth < 0)
	{
		m_drag.azimuth += 2.0f * glm::pi<float>();
	}

	// Clamp elevation to avoid gimbal lock
	m_drag.elevation = glm::clamp(m_drag.elevation, -glm::pi<float>() / 2.0f + 0.01f, glm::pi<float>() / 2.0f - 0.01f);

	// Clamp distance
	m_drag.distance = glm::clamp(m_drag.distance, 0.5f, 20.0f);

	// Convert spherical coordinates to Cartesian
	float x = cos(m_drag.elevation) * cos(m_drag.azimuth);
	float y = sin(m_drag.elevation);
	float z = cos(m_drag.elevation) * sin(m_drag.azimuth);

	glm::vec3 position = m_drag.targetPoint + glm::vec3(x, y, z) * m_drag.distance;

	// Update camera position and look-at
	auto cam = m_scene ? m_scene->getActiveCamera() : nullptr;
	if (cam && cam->getTransform())
	{
		cam->getTransform()->setLocalPosition(position);
		cam->lookAt(m_drag.targetPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

} // namespace engine
