/**
 * @file main.cpp
 * @brief Example demonstrating the GameEngine with SceneManager API
 * 
 * This example shows how to:
 * 1. Create a GameEngine instance
 * 2. Configure engine options
 * 3. Create and setup scenes using SceneManager
 * 4. Load models and create scene nodes
 * 5. Start the engine (automatic game loop)
 */

#include "engine/GameEngine.h"
#include "engine/scene/entity/ModelRenderNode.h"
#include "engine/scene/CameraNode.h"
#include <iostream>

using namespace engine;
using namespace engine::scene;

int main(int argc, char **argv)
{
	std::cout << "Vienna WebGPU Engine - Game Engine Example" << std::endl;

	// 1. Create the game engine
	GameEngine engine;

	// 2. Configure engine options
	GameEngineOptions options;
	options.windowWidth = 1280;
	options.windowHeight = 720;
	options.resizableWindow = true;
	options.fullscreen = false;
	options.targetFrameRate = 60.0f;
	options.enableVSync = true;
	options.runPhysics = false; // Disable physics thread for this simple example
	options.showFrameStats = true; // Show FPS in console
	engine.setOptions(options);

	// 3. Get subsystems for scene setup
	auto sceneManager = engine.getSceneManager();
	auto resourceManager = engine.getResourceManager();
	auto context = engine.getContext();

	// 4. Create a scene
	auto mainScene = sceneManager->createScene("MainScene");

	// 5. Setup camera
	auto cameraNode = std::make_shared<CameraNode>();
	cameraNode->setPosition(glm::vec3(0.0f, 2.0f, 5.0f));
	cameraNode->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
	cameraNode->setPerspective(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
	mainScene->setActiveCamera(cameraNode);

	// Add camera as child of root
	mainScene->getRoot()->addChild(cameraNode);

	// 6. Load a model and add to scene (optional - requires model file)
	/*
	auto modelHandle = resourceManager->loadModel("fox/fox.gltf");
	if (modelHandle.isValid())
	{
		auto modelNode = std::make_shared<entity::ModelRenderNode>(modelHandle, context);
		modelNode->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
		mainScene->getRoot()->addChild(modelNode);
	}
	*/

	// 7. Load the scene (make it active)
	sceneManager->loadScene("MainScene");

	// 8. Start the engine - this blocks until window is closed
	// The engine will automatically:
	// - Update the active scene
	// - Collect render data
	// - Prepare GPU resources
	// - Render frames
	// - Handle physics (if enabled)
	engine.run();

	std::cout << "Engine stopped. Exiting..." << std::endl;
	return 0;
}
