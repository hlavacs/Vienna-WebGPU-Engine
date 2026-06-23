/**
 * Tutorials: Unlit Shader with Custom Bind Group
 * Learn how to create custom shaders and bind groups
 */
#include "engine/EngineMain.h"
// ^ This has to be on top to define SDL_MAIN_HANDLED ^
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/ShaderType.h"

#include "CustomRenderNode.h"
#include "FreeFlyCamera.h"

using PathProvider = engine::core::PathProvider;

int main(int argc, char **argv)
{
	spdlog::info("Tutorials: Unlit Shader with Custom Bind Group");

	// Initialize engine
	engine::GameEngineOptions options;
	options.windowWidth = 1152;
	options.windowHeight = 648;
	options.enableVSync = false;

	engine::GameEngine engine;
	engine.initialize(options);

	auto sceneManager = engine.getSceneManager();
	auto resourceManager = engine.getResourceManager();
	auto webgpuContext = engine.getContext();
	auto &shaderRegistry = webgpuContext->shaderRegistry();
	auto &shaderFactory = webgpuContext->shaderFactory();

	// Create scene
	auto tutorialScene = sceneManager->createScene("Tutorial");
	auto rootNode = tutorialScene->getRoot();

	// Setup camera
	auto mainCamera = tutorialScene->getMainCamera();
	mainCamera->setFov(45.0f);
	mainCamera->setNearFar(0.1f, 100.0f);
	mainCamera->setPerspective(true);
	mainCamera->getTransform().setLocalPosition(glm::vec3(0.0f, 2.0f, 5.0f));
	mainCamera->getTransform().lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	mainCamera->setBackgroundColor(glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
	auto freeFlyCameraController = std::make_shared<demo::FreeFlyCameraController>(mainCamera);
	rootNode->addChild(freeFlyCameraController);

	auto maybeModelFourareen = resourceManager->m_modelManager->createModel(PathProvider::getResource("fourareen.obj"));
	if (!maybeModelFourareen.has_value())
	{
		spdlog::error("Failed to load fourareen.obj model");
		return -1;
	}
	auto fourareenNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	fourareenNode->getTransform().setLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	rootNode->addChild(fourareenNode);

	auto maybeModelFloor = resourceManager->m_modelManager->createModel(PathProvider::getResource("plane.obj"));
	if (!maybeModelFloor.has_value())
	{
		spdlog::error("Failed to load plane.obj model");
		return -1;
	}

#pragma region Tutorial Shader Registering
	// The bind groups are read straight from unlit.wgsl by reflection: Frame@0
	// and Object@3 come from the engine `#include`s, Material@2 from the structs
	// the shader declares. The descriptor only adds what WGSL cannot say - here,
	// that the base color texture at @group(2) @binding(2) is the material's
	// DIFFUSE slot with a white fallback.
	engine::rendering::webgpu::ShaderDescriptor unlitShader;
	unlitShader.name         = "unlit";
	unlitShader.type         = engine::rendering::ShaderType::Unlit;
	unlitShader.path         = PathProvider::getShaders("unlit.wgsl");
	unlitShader.vertexLayout = engine::rendering::VertexLayout::PositionNormalUV;

	engine::rendering::webgpu::BindGroupMeta material;
	material.bindings[2] = {engine::rendering::MaterialTextureSlots::DIFFUSE, glm::vec3(1.0f, 1.0f, 1.0f)};
	unlitShader.groups[2] = material;
	// Tutorial 02 - Step 6: add a custom @group(4) entry here, e.g.
	//   unlitShader.groups[4] = {"TileUniforms", BindGroupType::Custom, BindGroupReuse::PerObject, {}};

	shaderRegistry.registerShader(shaderFactory.buildFromDescriptor(unlitShader));
#pragma endregion

#pragma region Tutorial Material Creation and Setup
	auto unlitProperties = engine::rendering::UnlitProperties{};
	unlitProperties.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	auto diffuseTexture = resourceManager->m_textureManager->createTextureFromFile(
		PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg")
	);
	auto maybeFloorMaterial = resourceManager->m_materialManager->createMaterial(
		"Floor_Material",
		unlitProperties,
		"unlit", // Use unlit shader
		{{engine::rendering::MaterialTextureSlots::DIFFUSE, diffuseTexture.value()->getHandle()}}
	);
	if (!maybeFloorMaterial.has_value())
	{
		spdlog::error("Failed to create floor material");
		return -1;
	}

	auto floorMaterial = maybeFloorMaterial.value();
	auto floorModel = maybeModelFloor.value();

	// Tutorial 01 - Step 9: Uncomment this line after completing the shader
	// Tutorial 02 - Step 9: Uncomment this line after completing the shader
	// This assigns our custom material to the floor's only submesh.
	// plane.obj has only one mesh, so we use [0] to access it.
	// ------------------
	// floorModel->getSubmeshes()[0].material = floorMaterial->getHandle();

	// Tutorial 02 - Step 8: Create CustomRenderNode instance
	auto floorNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(floorModel);
	floorNode->getTransform().setLocalScale(glm::vec3(10.0f, 1.0f, 10.0f));
	rootNode->addChild(floorNode);
#pragma endregion

#pragma region Setup Lights
	auto sunLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight sunLightData;
	sunLightData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	sunLightData.intensity = 1.0f;
	sunLight->getLight().setData(sunLightData);
	sunLight->getTransform().setLocalRotation(glm::quat(glm::radians(glm::vec3(45.0f, 180.0f, 00.0f))));
	rootNode->addChild(sunLight->asNode());

	auto ambientLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::AmbientLight ambientLightData;
	ambientLightData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	ambientLightData.intensity = 0.02f; // Lower intensity for ambient light so the direct light is more visible
	ambientLight->getLight().setData(ambientLightData);
	rootNode->addChild(ambientLight->asNode());
#pragma endregion

	// Load and run
	sceneManager->loadScene("Tutorial");
	engine.run();

	spdlog::info("Tutorial completed successfully");
	return 0;
}
