/**
 * Vienna WebGPU Engine - Main Entry Point
 * Using GameEngine with SceneManager for declarative scene setup
 */
#include "engine/EngineMain.h"
// ^ This has to be on top to define SDL_MAIN_HANDLED ^

#include <set>

#include "DayNightCycle.h"
#include "MainDemoImGuiUI.h"
#include "OrbitCamera.h"

void setupLighting(std::shared_ptr<engine::scene::nodes::Node> rootNode, std::shared_ptr<engine::scene::nodes::LightNode> &ambientLight, std::shared_ptr<engine::scene::nodes::LightNode> &sunLight, std::shared_ptr<engine::scene::nodes::LightNode> &moonLight)
{
	// Ambient light
	ambientLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::AmbientLight ambientData;
	ambientData.color = glm::vec3(0.2f, 0.2f, 0.2f);
	ambientData.intensity = 1.0f;
	ambientLight->getLight().setData(ambientData);
	rootNode->addChild(ambientLight->asNode());

	// Sun light (directional)
	sunLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight sunData;
	sunData.color = glm::vec3(1.0f, 1.0f, 0.95f);
	sunData.shadowPCFKernel = 2;
	sunLight->getLight().setData(sunData);
	rootNode->addChild(sunLight->asNode());

	// Moon light (directional)
	moonLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight moonData;
	moonData.color = glm::vec3(0.7f, 0.8f, 1.0f);
	moonData.intensity = 0.2f;
	moonLight->getLight().setData(moonData);
	rootNode->addChild(moonLight->asNode());

	// Spot lights
	engine::rendering::SpotLight spotData;
	spotData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	spotData.intensity = 35.0f;
	spotData.castShadows = true;
	spotData.range = 100.0f;
	spotData.spotAngle = glm::radians(10.0f);
	spotData.shadowMapSize = 4096;
	spotData.shadowPCFKernel = 4;

	auto spotLight1 = std::make_shared<engine::scene::nodes::LightNode>();
	spotLight1->getLight().setData(spotData);
	glm::quat spotRotation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, -90.0f)));
	spotLight1->getTransform().setLocalRotation(spotRotation);
	spotLight1->getTransform().setWorldPosition(glm::vec3(0.0f, 9.0f, 0.0f));
	rootNode->addChild(spotLight1->asNode());

	auto spotLight2 = std::make_shared<engine::scene::nodes::LightNode>();
	spotLight2->getLight().setData(spotData);
	spotLight2->getTransform().setLocalRotation(spotRotation);
	spotLight2->getTransform().setWorldPosition(glm::vec3(0.0f, 9.0f, 0.0f));
	rootNode->addChild(spotLight2->asNode());
}

bool setupModels(std::shared_ptr<engine::scene::nodes::Node> rootNode, std::shared_ptr<engine::resources::ResourceManager> resourceManager)
{
	// Load models from disk
	auto maybeModelFourareen = resourceManager->m_modelManager->createModel("fourareen.obj");
	auto maybeModelPlane = resourceManager->m_modelManager->createModel("plane.obj");

	if (!maybeModelFourareen.has_value())
	{
		spdlog::error("Failed to load fourareen.obj model");
		return false;
	}
	if (!maybeModelPlane.has_value())
	{
		spdlog::error("Failed to load plane.obj model");
		return false;
	}

	// Add fourareen models to scene - demonstrating model instancing
	// Multiple nodes can share the same model data (GPU memory is shared)
	auto modelNode1 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode1->getTransform().setLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	rootNode->addChild(modelNode1);

	auto modelNode2 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode2->getTransform().setLocalPosition(glm::vec3(0.0f, 3.0f, 0.4f));
	rootNode->addChild(modelNode2);

	// Create floor plane with custom PBR material
	auto floorNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelPlane.value());
	floorNode->getTransform().setLocalPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	floorNode->getTransform().setLocalScale(glm::vec3(10.0f, 1.0f, 10.0f));

	// Load textures for PBR material
	auto floorPBRProperties = engine::rendering::PBRProperties();
	auto diffuseTexture = resourceManager->m_textureManager->createTextureFromFile(
		PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg")
	);
	auto normalTexture = resourceManager->m_textureManager->createTextureFromFile(
		PathProvider::getResource("cobblestone_floor_08_nor_gl_2k.png")
	);

	// Create material with both diffuse and normal maps
	auto floorMaterial = resourceManager->m_materialManager->createPBRMaterial(
		"Floor_Material",
		floorPBRProperties,
		{{engine::rendering::MaterialTextureSlots::DIFFUSE, diffuseTexture.value()->getHandle()},
		 {engine::rendering::MaterialTextureSlots::NORMAL, normalTexture.value()->getHandle()}}
	);

	// Manually assign material to the plane's submesh
	// The plane.obj file has no material defined, so we need to assign one programmatically
	auto mesh = floorNode->getModel().get().value();
	mesh->getSubmeshes()[0].material = floorMaterial.value()->getHandle();
	rootNode->addChild(floorNode);

	return true;
}

void setupImGui(std::shared_ptr<engine::ui::ImGuiManager> imguiManager, demo::MainDemoImGuiUI &mainDemoUI, std::shared_ptr<demo::DayNightCycle> dayNightCycle)
{
	imguiManager->addFrame([&]()
						   { mainDemoUI.render(); });

	imguiManager->addFrame([&]()
						   { mainDemoUI.renderPerformanceWindow(); });

	imguiManager->addFrame([&]()
						   { mainDemoUI.renderShadowDebugWindow(); });

	imguiManager->addFrame([&, dayNightCycle]()
						   {
		ImGui::Begin("Day-Night Cycle Controls");
		
		float hour = dayNightCycle->getHour();
		if (ImGui::SliderFloat("Hour of Day", &hour, 0.0f, 24.0f))
		{
			dayNightCycle->setHour(hour);
		}
		
		bool paused = dayNightCycle->isPaused();
		if (ImGui::Checkbox("Pause Cycle", &paused))
		{
			dayNightCycle->setPaused(paused);
		}
		
		float cycleDuration = dayNightCycle->getCycleDuration();
		if (ImGui::SliderFloat("Cycle Duration (seconds)", &cycleDuration, 10.0f, 600.0f))
		{
			dayNightCycle->setCycleDuration(cycleDuration);
		}
		
		ImGui::End(); });
}

int main(int argc, char **argv)
{
	spdlog::info("Vienna WebGPU Engine Starting...");

	// Initialize engine
	engine::GameEngineOptions options;
	options.windowWidth = 1152;
	options.windowHeight = 648;
	options.enableVSync = false;

	engine::GameEngine engine;
	engine.initialize(options);

	auto sceneManager = engine.getSceneManager();
	auto resourceManager = engine.getResourceManager();
	auto imguiManager = engine.getImGuiManager();

	// Create scene
	auto mainScene = sceneManager->createScene("Main");
	auto rootNode = mainScene->getRoot();

	// Setup cameras
	auto mainCamera = mainScene->getMainCamera();
	if (mainCamera)
	{
		mainCamera->setFov(45.0f);
		mainCamera->setNearFar(0.1f, 100.0f);
		mainCamera->setPerspective(true);
		mainCamera->getTransform().setLocalPosition(glm::vec3(0.0f, 2.0f, 5.0f));
		mainCamera->getTransform().lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		mainCamera->setBackgroundColor(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
		mainCamera->setMSAAEnabled(true);
	}

	// Setup orbit camera controller
	demo::OrbitCameraState orbitState;
	orbitState.distance = 5.0f;
	orbitState.azimuth = 0.0f;
	orbitState.elevation = 0.3f;
	auto orbitController = std::make_shared<demo::OrbitCameraController>(orbitState, mainCamera);
	rootNode->addChild(orbitController);

	// Setup lighting
	std::shared_ptr<engine::scene::nodes::LightNode> ambientLight, sunLight, moonLight;
	setupLighting(rootNode, ambientLight, sunLight, moonLight);

	// Setup day-night cycle
	auto dayNightCycle = std::make_shared<demo::DayNightCycle>(sunLight, moonLight, ambientLight);
	dayNightCycle->setCycleDuration(120.0f);
	dayNightCycle->setHour(12.0f);
	rootNode->addChild(dayNightCycle);

	// Setup models
	if (!setupModels(rootNode, resourceManager))
	{
		spdlog::error("Failed to setup scene models");
		return -1;
	}

	// Setup ImGui
	demo::MainDemoImGuiUI mainDemoUI(engine, rootNode, orbitController);
	setupImGui(imguiManager, mainDemoUI, dayNightCycle);

	// Load and run
	sceneManager->loadScene("Main");
	engine.run();

	spdlog::info("Engine shut down successfully");
	return 0;
}