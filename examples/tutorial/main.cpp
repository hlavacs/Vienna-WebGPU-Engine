#include <engine/GameEngine.h>
#include <engine/scene/SceneManager.h>
#include <engine/scene/Scene.h>
#include <engine/scene/entity/ModelRenderNode.h>
#include <engine/scene/LightNode.h>
#include <engine/rendering/WebGPUPipelineManager.h>
#include <engine/resources/loaders/ObjLoader.h>

using namespace engine;
using namespace engine::scene;

int main(int argc, char* argv[]) {
	// Initialize engine
	GameEngine engine(800, 600, "Tutorial 01: Unlit Shader with Custom Bind Group");

	// Get engine systems
	auto& sceneManager = engine.getSceneManager();
	auto& renderer = engine.getRenderer();
	auto& pipelineManager = renderer.getPipelineManager();
	auto& webgpuContext = engine.getWebGPUContext();
	auto& resourceManager = engine.getResourceManager();

	// Create the main scene
	auto scene = sceneManager.createScene("TutorialScene");
	sceneManager.setActiveScene("TutorialScene");

	// === STEP 1: Load the floor model ===
	auto floorModelHandle = resourceManager.loadModel("models/fourareen.obj");

	// === STEP 2: Create a ModelRenderNode for the floor ===
	auto floorNode = scene->createNode<ModelRenderNode>("Floor");
	floorNode->setModel(floorModelHandle);
	floorNode->getTransform()->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
	floorNode->getTransform()->setScale(glm::vec3(2.0f, 1.0f, 2.0f));

	// === STEP 3: Register the custom unlit shader ===
	// TODO: We'll register the shader from assets/unlit.wgsl
	// The shader file is currently empty - we'll implement it step by step in the tutorial
	
	// === STEP 4: Create a custom pipeline with the unlit shader ===
	// TODO: Configure pipeline with our custom shader
	
	// === STEP 5: Assign the custom pipeline to the floor node ===
	// TODO: Tell the floor to use our custom unlit shader

	// Add a basic directional light
	auto lightNode = scene->createNode<LightNode>("DirectionalLight");
	lightNode->getLight().type = LightType::Directional;
	lightNode->getLight().direction = glm::vec3(0.0f, -1.0f, -0.5f);
	lightNode->getLight().color = glm::vec3(1.0f, 1.0f, 1.0f);
	lightNode->getLight().intensity = 1.0f;

	// Setup camera
	auto camera = scene->getActiveCamera();
	camera->getTransform()->setPosition(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->getTransform()->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

	// Run the engine
	engine.run();

	return 0;
}
