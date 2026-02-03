/**
 * Vienna WebGPU Engine - Multi View Example
 * Demonstrates 4 cameras with free-fly controller and camera switching
 */
#include "engine/EngineMain.h"
// ^ This has to be on top to define SDL_MAIN_HANDLED ^

#include "CameraSwitcher.h"
#include "FreeFlyCamera.h"

int main(int argc, char **argv)
{
	spdlog::info("Vienna WebGPU Engine - Multi View Example Starting...");

	engine::GameEngineOptions options;
	options.windowWidth = 1600;
	options.windowHeight = 900;
	options.enableVSync = false;

	// Create and configure the engine
	engine::GameEngine engine;
	engine.initialize(options);

	// Get managers for setup
	auto sceneManager = engine.getSceneManager();
	auto resourceManager = engine.getResourceManager();
	auto context = engine.getContext();

	// Create main scene
	auto mainScene = sceneManager->createScene("MultiView");
	auto rootNode = mainScene->getRoot();

	// Create 4 cameras with different viewports (2x2 grid)
	std::vector<std::shared_ptr<engine::scene::nodes::CameraNode>> cameras;

	// Camera 1: Top-left
	{
		auto camera = mainScene->getMainCamera();
		camera->setViewport(glm::vec4(0.0f, 0.0f, 0.5f, 0.5f));
		camera->setFov(60.0f);
		camera->setNearFar(0.1f, 100.0f);
		camera->setPerspective(true);
		camera->setBackgroundColor(glm::vec4(0.15f, 0.15f, 0.2f, 1.0f));
		camera->getTransform().setLocalPosition(glm::vec3(-5.0f, 3.0f, 5.0f));
		camera->getTransform().lookAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		cameras.push_back(camera);
	}

	// Camera 2: Top-right
	{
		auto camera = std::make_shared<engine::scene::nodes::CameraNode>();
		camera->setViewport(glm::vec4(0.5f, 0.0f, 1.0f, 0.5f));
		camera->setFov(60.0f);
		camera->setNearFar(0.1f, 100.0f);
		camera->setPerspective(true);
		camera->setBackgroundColor(glm::vec4(0.2f, 0.15f, 0.15f, 1.0f));
		camera->getTransform().setLocalPosition(glm::vec3(5.0f, 3.0f, 5.0f));
		camera->getTransform().lookAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		cameras.push_back(camera);
		mainScene->addCamera(camera);
	}

	// Camera 3: Bottom-left
	{
		auto camera = std::make_shared<engine::scene::nodes::CameraNode>();
		camera->setViewport(glm::vec4(0.0f, 0.5f, 0.5f, 1.0f));
		camera->setFov(60.0f);
		camera->setNearFar(0.1f, 100.0f);
		camera->setPerspective(true);
		camera->setBackgroundColor(glm::vec4(0.15f, 0.2f, 0.15f, 1.0f));
		camera->getTransform().setLocalPosition(glm::vec3(0.0f, 8.0f, 0.0f));
		camera->getTransform().lookAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		cameras.push_back(camera);
		mainScene->addCamera(camera);
	}

	// Camera 4: Bottom-right
	{
		auto camera = std::make_shared<engine::scene::nodes::CameraNode>();
		camera->setViewport(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
		camera->setFov(60.0f);
		camera->setNearFar(0.1f, 100.0f);
		camera->setPerspective(true);
		camera->setBackgroundColor(glm::vec4(0.2f, 0.2f, 0.15f, 1.0f));
		camera->getTransform().setLocalPosition(glm::vec3(0.0f, 1.5f, -5.0f));
		camera->getTransform().lookAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		cameras.push_back(camera);
		mainScene->addCamera(camera);
	}

	auto freeFlyCameraController = std::make_shared<demo::FreeFlyCameraController>(cameras[0]);
	rootNode->addChild(freeFlyCameraController);

	// Create ambient light
	auto ambientLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::AmbientLight ambientData;
	ambientData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	ambientData.intensity = 0.05f;
	ambientLightNode->getLight().setData(ambientData);
	rootNode->addChild(ambientLightNode->asNode());

	// Create directional light (sun)
	auto dirLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight dirData;
	dirData.color = glm::vec3(1.0f, 0.95f, 0.9f);
	dirData.intensity = 1.5f;
    dirData.cascadeCount = 2;
	dirData.castShadows = true;
	dirData.shadowPCFKernel = 2;
	dirLightNode->getLight().setData(dirData);
	dirLightNode->getTransform().setLocalRotation(glm::quat(glm::radians(glm::vec3(-45.0f, 0.0f, 0.0f))));
	rootNode->addChild(dirLightNode->asNode());

	// Create spotlight
	auto spotLightNode = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::SpotLight spotData;
	spotData.color = glm::vec3(1.0f, 0.8f, 0.6f);
	spotData.intensity = 50.0f;
	spotData.castShadows = true;
	spotData.range = 20.0f;
	spotData.spotAngle = glm::radians(40.0f);
    spotData.spotSoftness = 0.8f;
	spotData.shadowMapSize = 2048;
	spotData.shadowPCFKernel = 3;
	spotLightNode->getLight().setData(spotData);
	spotLightNode->getTransform().setWorldPosition(glm::vec3(0.0f, 6.0f, 0.0f));
	glm::quat spotRotation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, -90.0f)));
	spotLightNode->getTransform().setLocalRotation(spotRotation);
	rootNode->addChild(spotLightNode->asNode());

	// Load models
	auto maybeModelFourareen = resourceManager->m_modelManager->createModel("fourareen.obj");
	auto maybeModelFloor = resourceManager->m_modelManager->createModel("floor.obj");

	if (!maybeModelFourareen.has_value())
	{
		spdlog::error("Failed to load fourareen.obj model");
		return -1;
	}
	if (!maybeModelFloor.has_value())
	{
		spdlog::error("Failed to load floor.obj model");
		return -1;
	}

	// Add some objects to the scene
	auto modelNode1 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode1->getTransform().setLocalPosition(glm::vec3(-2.0f, 1.0f, 0.0f));
	rootNode->addChild(modelNode1);

	auto modelNode2 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode2->getTransform().setLocalPosition(glm::vec3(2.0f, 1.0f, -1.0f));
	rootNode->addChild(modelNode2);

	auto modelNode3 = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	modelNode3->getTransform().setLocalPosition(glm::vec3(0.0f, 2.0f, 2.0f));
	rootNode->addChild(modelNode3);

	// Add floor plane
	auto floorNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFloor.value());
	floorNode->getTransform().setLocalPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	floorNode->getTransform().setLocalScale(glm::vec3(15.0f, 1.0f, 15.0f));
    floorNode->setDebugEnabled(true); // Enable debug rendering to see the floor outline and world coordinates
	rootNode->addChild(floorNode);

	// Create camera switcher
	auto cameraSwitcher = std::make_shared<demo::CameraSwitcher>(
		freeFlyCameraController,
		cameras
	);
	rootNode->addChild(cameraSwitcher);

	// Load the scene (makes it active)
	sceneManager->loadScene("MultiView");

	spdlog::info("Multi View Example Ready!");
	spdlog::info("Controls:");
	spdlog::info("  - Press 1, 2, 3, or 4 to switch active camera");
	spdlog::info("  - Hold Right Mouse Button to enable mouse look");
	spdlog::info("  - WASD to move camera");
	spdlog::info("  - Space/LShift to move up/down");

	// Run the engine (blocks until window closed)
	engine.run();

	spdlog::info("Engine shut down successfully");
	return 0;
}
