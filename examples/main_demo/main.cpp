/**
 * Vienna WebGPU Engine - Main Entry Point
 * Using GameEngine with SceneManager for declarative scene setup
 */
#include "engine/EngineMain.h"
#include <windows.h>
// ^ This has to be on top to define SDL_MAIN_HANDLED ^
#include "DayNightCycle.h"
#include "MainDemoImGuiUI.h"
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
	auto cameraNode = mainScene->getMainCamera();
	if (cameraNode)
	{
		cameraNode->setFov(45.0f);
		cameraNode->setNearFar(0.1f, 100.0f);
		cameraNode->setPerspective(true);
		cameraNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 2.0f, 5.0f));
		cameraNode->lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		cameraNode->setBackgroundColor(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
		// IMPORTANT: Clear both color and depth buffers every frame
	}

	// Initialize orbit camera state
	demo::OrbitCameraState orbitState;
	orbitState.distance = 5.0f;
	orbitState.azimuth = 0.0f;
	orbitState.elevation = 0.3f;

	// Create ambient light
	auto ambientLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::AmbientLight ambientData;
	ambientData.color = glm::vec3(0.2f, 0.2f, 0.2f);
	ambientData.intensity = 1.0f;
	ambientLightNode->getLight().setData(ambientData);

	rootNode->addChild(std::static_pointer_cast<engine::scene::nodes::Node>(ambientLightNode));

	// Create sun light (directional)
	auto sunLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight sunData;
	sunData.color = glm::vec3(1.0f, 1.0f, 0.95f);
	sunData.shadowPCFKernel = 2;
	sunLightNode->getLight().setData(sunData);
	rootNode->addChild(std::static_pointer_cast<engine::scene::nodes::Node>(sunLightNode));

	// Create moon light (directional, optional)
	auto moonLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight moonData;
	moonData.color = glm::vec3(0.7f, 0.8f, 1.0f);
	moonData.intensity = 0.2f;
	moonLightNode->getLight().setData(moonData);
	rootNode->addChild(std::static_pointer_cast<engine::scene::nodes::Node>(moonLightNode));

	// Create day-night cycle controller
	auto dayNightCycle = std::make_shared<demo::DayNightCycle>(sunLightNode, moonLightNode, ambientLightNode);
	dayNightCycle->setCycleDuration(120.0f); // 2 minutes for full cycle
	dayNightCycle->setHour(12.0f);			 // noon
	rootNode->addChild(dayNightCycle);

	auto spotLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::SpotLight spotlightData;
	spotlightData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	spotlightData.intensity = 35.0f;
	spotlightData.castShadows = true;
	spotlightData.range = 100.0f;
	spotlightData.spotAngle = glm::radians(10.0f);
	spotlightData.shadowMapSize = 4096;
	spotlightData.shadowPCFKernel = 4;
	spotLightNode->getLight().setData(spotlightData);
	glm::quat spotLightRotation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, -90.0f)));
	spotLightNode->getTransform()->setLocalRotation(spotLightRotation);
	spotLightNode->getTransform()->setWorldPosition(glm::vec3(0.0f, 9.0f, 0.0f));

	rootNode->addChild(spotLightNode->asNode());
	auto spotLightNode2 = std::make_shared<engine::scene::nodes::LightNode>();
	spotLightNode2->getLight().setData(spotlightData);
	glm::quat spotLightRotation2 = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, -90.0f)));
	spotLightNode2->getTransform()->setLocalRotation(spotLightRotation2);
	spotLightNode2->getTransform()->setWorldPosition(glm::vec3(0.0f, 9.0f, 0.0f));
	rootNode->addChild(spotLightNode2->asNode());

	// Track all lights and their UI angles (for spot lights)
	std::vector<std::shared_ptr<engine::scene::nodes::LightNode>> lightNodes;
	std::map<size_t, glm::vec3> lightDirectionsUI;

	// Load model (CPU-side only, GPU resources will be created by the renderer)
	engine::rendering::Model::Handle fourareenModleHandle;
	engine::rendering::Model::Handle planeModelHandle;
	auto maybeModelFourareen = resourceManager->m_modelManager->createModel("fourareen.obj");
	auto maybeModelPlane = resourceManager->m_modelManager->createModel("plane.obj");
	if (!maybeModelFourareen.has_value())
	{
		spdlog::error("Failed to load fourareen.obj model");
		return -1;
	}
	if (!maybeModelPlane.has_value())
	{
		spdlog::error("Failed to load plane.obj model");
		return -1;
	}

	auto modelNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	rootNode->addChild(modelNode);

	modelNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 3.0f, 0.4f));
	rootNode->addChild(modelNode);

	modelNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelPlane.value());
	modelNode->getTransform()->setLocalPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	modelNode->getTransform()->setLocalScale(glm::vec3(10.0f, 1.0f, 10.0f));
	rootNode->addChild(modelNode);

	// Create an UpdateNode to handle orbit camera input
	auto orbitCameraController = std::make_shared<demo::OrbitCameraController>(orbitState, cameraNode);
	rootNode->addChild(orbitCameraController);

	// Get ImGui manager and add UI callbacks
	auto imguiManager = engine.getImGuiManager();

	// Create and register the ImGui UI class
	demo::MainDemoImGuiUI mainDemoImGuiUI(engine, rootNode, orbitCameraController);
	imguiManager->addFrame([&]()
						   { mainDemoImGuiUI.render(); });
	imguiManager->addFrame([&]()
						   { mainDemoImGuiUI.renderPerformanceWindow(); });
	imguiManager->addFrame([&]() {
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
		ImGui::End();
	});

	// Load the scene (makes it active)
	sceneManager->loadScene("Main");

	// Run the engine (blocks until window closed)
	engine.run();

	spdlog::info("Engine shut down successfully");
	return 0;
}
