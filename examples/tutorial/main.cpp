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
	auto shaderInfo =
		shaderFactory
			.begin(
				"unlit",
				engine::rendering::ShaderType::Unlit,
				PathProvider::getShaders("unlit_custom.wgsl"), // Adjust based on tutorial
				"vs_main",
				"fs_main",
				engine::rendering::VertexLayout::PositionNormalUV,
				true,  // enableDepth
				false, // enableBlend
				true   // cullBackFaces
			)
			.addFrameBindGroup()
			.addObjectBindGroup()
			.addBindGroup(
				engine::rendering::bindgroup::defaults::MATERIAL,
				engine::rendering::BindGroupReuse::PerObject,
				engine::rendering::BindGroupType::Material
			)
			.addUniform(
				engine::rendering::bindgroup::entry::defaults::MATERIAL_PROPERTIES,
				sizeof(engine::rendering::UnlitProperties),
				WGPUShaderStage_Fragment
			)
			.addSampler(
				"textureSampler",
				wgpu::SamplerBindingType::Filtering,
				WGPUShaderStage_Fragment
			)
			.addMaterialTexture(
				"baseColorTexture",
				engine::rendering::MaterialTextureSlots::DIFFUSE, // material slot name
				wgpu::TextureSampleType::Float,
				wgpu::TextureViewDimension::_2D,
				WGPUShaderStage_Fragment
			) // Tutorial 2 - Step 6: Register Shader with Custom Bind Group
			.build();

	shaderRegistry.registerShader(shaderInfo);
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

	// Tutorial 1 - Step 9: Uncomment this line after completing the shader
	// This assigns our custom material to the floor's only submesh.
	// plane.obj has only one mesh, so we use [0] to access it.
	// ------------------
	// floorModel->getSubmeshes()[0].material = floorMaterial->getHandle();

	// Tutorial 2: Step 8: Create CustomRenderNode Instance
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
	sunLight->getTransform().setLocalRotation(glm::quat(glm::radians(glm::vec3(45.0f, 0.0f, 0.0f))));
	rootNode->addChild(sunLight->asNode());

	auto ambientLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::AmbientLight ambientLightData;
	ambientLightData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	ambientLightData.intensity = 0.02f; // Lower intensity for ambient light so the direct light is more visible
	ambientLight->getLight().setData(ambientLightData);
	ambientLight->getTransform().setLocalRotation(glm::quat(glm::radians(glm::vec3(45.0f, 0.0f, 0.0f))));
	rootNode->addChild(ambientLight->asNode());
#pragma endregion

	// Load and run
	sceneManager->loadScene("Tutorial");
	engine.run();

	spdlog::info("Tutorial completed successfully");
	return 0;
}
