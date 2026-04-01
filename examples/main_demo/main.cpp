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

std::shared_ptr<engine::scene::SceneManager> sceneManager;
std::shared_ptr<engine::resources::ResourceManager> resourceManager;
std::shared_ptr<engine::ui::ImGuiManager> imguiManager;

std::shared_ptr<demo::OrbitCameraController> setupCamera(std::shared_ptr<engine::scene::Scene> scene)
{
	auto mainCamera = scene->getMainCamera();
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
	orbitState.distance = 20.0f;
	orbitState.azimuth = -0.5f;
	orbitState.elevation = 0.5f;
	auto orbitController = std::make_shared<demo::OrbitCameraController>(orbitState, mainCamera);
	scene->getRoot()->addChild(orbitController);
	return orbitController;
}

void setupComplexLighting(std::shared_ptr<engine::scene::Scene> scene, std::shared_ptr<engine::scene::nodes::LightNode> &ambientLight, std::shared_ptr<engine::scene::nodes::LightNode> &sunLight, std::shared_ptr<engine::scene::nodes::LightNode> &moonLight)
{
	auto rootNode = scene->getRoot();
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
	spotData.intensity = 100.0f;
	spotData.castShadows = true;
	spotData.range = 100.0f;
	spotData.spotAngle = glm::radians(70.0f);
	spotData.shadowMapSize = 4096;
	spotData.shadowPCFKernel = 4;

	auto spotLight1 = std::make_shared<engine::scene::nodes::LightNode>();
	spotLight1->getLight().setData(spotData);
	glm::quat spotRotation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, -90.0f)));
	spotLight1->getTransform().setLocalRotation(spotRotation);
	spotLight1->getTransform().setWorldPosition(glm::vec3(0.0f, 9.0f, 0.0f));
	rootNode->addChild(spotLight1->asNode());
}

void setupSimpleLighting(std::shared_ptr<engine::scene::Scene> scene)
{
	auto rootNode = scene->getRoot();

	// Ambient light
	auto ambientLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::AmbientLight ambientData;
	ambientData.color = glm::vec3(0.3f, 0.3f, 0.3f);
	ambientData.intensity = 1.0f;
	ambientLight->getLight().setData(ambientData);
	rootNode->addChild(ambientLight->asNode());

	// Directional light
	auto directionalLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight directionalData;
	directionalData.color = glm::vec3(1.0f, 1.0f, 0.95f);
	directionalData.intensity = 1.0f;
	directionalData.castShadows = true;
	directionalData.shadowPCFKernel = 2;
	directionalLight->getLight().setData(directionalData);
	glm::quat dirRotation = glm::quat(glm::radians(glm::vec3(140.0f, -30.0f, 0.0f)));
	directionalLight->getTransform().setLocalRotation(dirRotation);
	rootNode->addChild(directionalLight->asNode());

	// Point light
	auto pointLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::PointLight pointData;
	pointData.color = glm::vec3(1.0f, 0.8f, 0.6f);
	pointData.intensity = 10.0f;
	pointData.castShadows = true;
	pointData.range = 20.0f;
	pointLight->getLight().setData(pointData);
	pointLight->getTransform().setWorldPosition(glm::vec3(3.0f, 3.0f, 0.0f));
	rootNode->addChild(pointLight->asNode());

	// Spot light
	auto spotLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::SpotLight spotData;
	spotData.color = glm::vec3(0.8f, 0.9f, 1.0f);
	spotData.intensity = 25.0f;
	spotData.castShadows = true;
	spotData.range = 30.0f;
	spotData.spotAngle = glm::radians(20.0f);
	spotData.spotSoftness = 0.3f;
	spotData.shadowMapSize = 2048;
	spotData.shadowPCFKernel = 3;
	spotLight->getLight().setData(spotData);
	glm::quat spotRotation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, -90.0f)));
	spotLight->getTransform().setLocalRotation(spotRotation);
	spotLight->getTransform().setWorldPosition(glm::vec3(-3.0f, 5.0f, 0.0f));
	rootNode->addChild(spotLight->asNode());
}

bool setupScene1(std::shared_ptr<engine::scene::Scene> scene)
{
	auto orbitController = setupCamera(scene);

	// Use lazy loading constructor - models will load during scene initialization
	auto modelNode1 = std::make_shared<engine::scene::nodes::ModelRenderNode>(
		PathProvider::getResource("fourareen.obj")
	);
	modelNode1->getTransform().setLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	scene->getRoot()->addChild(modelNode1);

	auto modelNode2 = std::make_shared<engine::scene::nodes::ModelRenderNode>(
		PathProvider::getResource("fourareen.obj")
	);
	modelNode2->getTransform().setLocalPosition(glm::vec3(0.0f, 3.0f, 0.4f));
	scene->getRoot()->addChild(modelNode2);

	// Create floor plane with custom PBR material (use immediate loading)
	auto maybeModelPlane = resourceManager->m_modelManager->createModel(
		PathProvider::getResource("plane.obj"),
		"Floor_Plane"
	);
	if (!maybeModelPlane.has_value())
	{
		spdlog::error("Failed to load plane.obj model");
		return false;
	}

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
	auto mesh = floorNode->getModel().get().value();
	mesh->getSubmeshes()[0].material = floorMaterial.value()->getHandle();

	scene->getRoot()->addChild(floorNode);

	return true;
}

bool setupScene2(std::shared_ptr<engine::scene::Scene> scene)
{
	auto orbitController = setupCamera(scene);
	auto rootNode = scene->getRoot();

	// Use lazy loading constructor - models will load during scene initialization
	auto modelNodeSeaKeep = std::make_shared<engine::scene::nodes::ModelRenderNode>(
		PathProvider::getModels("sea_keep_lonely_watcher/scene.gltf")
	);
	modelNodeSeaKeep->getTransform().setLocalPosition(glm::vec3(0.0f, -3.0f, 0.0f));
	modelNodeSeaKeep->getTransform().setLocalScale(glm::vec3(0.025f, 0.025f, 0.025f));
	rootNode->addChild(modelNodeSeaKeep);

	return true;
}

void setupImGui(std::shared_ptr<engine::ui::ImGuiManager> imguiManager, std::shared_ptr<demo::MainDemoImGuiUI> mainDemoUI, std::shared_ptr<demo::DayNightCycle> dayNightCycle)
{
	imguiManager->addFrame([mainDemoUI]()
					   { mainDemoUI->render(sceneManager); });

	imguiManager->addFrame([mainDemoUI]()
					   { mainDemoUI->renderPerformanceWindow(); });

	imguiManager->addFrame([mainDemoUI]()
					   { mainDemoUI->renderShadowDebugWindow(); });

	imguiManager->addFrame([]()
	{
		ImGui::Begin("Scene Controls");
		
		auto activeScene = sceneManager->getActiveScene();
		if (activeScene)
		{
			ImGui::Text("Current Scene: %s", sceneManager->getActiveSceneName().c_str());
			ImGui::Separator();
		}
		
		if (ImGui::Button("Next Scene"))
		{
			auto currentName = sceneManager->getActiveSceneName();
			if (currentName == "Demo")
			{
				sceneManager->loadSceneAsync("SeaKeep");
			}
			else
			{
				sceneManager->loadSceneAsync("Demo");
			}
		}
		
		ImGui::End();
	});

imguiManager->addFrame([dayNightCycle]()
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

	sceneManager = engine.getSceneManager();
	resourceManager = engine.getResourceManager();
	imguiManager = engine.getImGuiManager();

	// Create scenes
	auto demoScene = sceneManager->createScene("Demo");
	auto seaKeepScene = sceneManager->createScene("SeaKeep");

	// Setup Demo Scene (one of each light type)
	if (!setupScene1(demoScene))
	{
		spdlog::error("Failed to setup demo scene");
		return -1;
	}
	setupSimpleLighting(demoScene);

	// Setup SeaKeep Scene (day-night cycle with multiple lights)
	if (!setupScene2(seaKeepScene))
	{
		spdlog::error("Failed to setup sea keep scene");
		return -1;
	}
	
	// Setup complex lighting with day-night cycle
	std::shared_ptr<engine::scene::nodes::LightNode> ambientLight, sunLight, moonLight;
	setupComplexLighting(seaKeepScene, ambientLight, sunLight, moonLight);

	// Setup day-night cycle
	auto dayNightCycle = std::make_shared<demo::DayNightCycle>(sunLight, moonLight, ambientLight);
	dayNightCycle->setCycleDuration(120.0f);
	dayNightCycle->setHour(12.0f);
	seaKeepScene->getRoot()->addChild(dayNightCycle);

	// Setup ImGui - get orbit controller from active scene dynamically
	auto getOrbitController = []()
	{
		if (!sceneManager || !sceneManager->getActiveScene())
			return std::shared_ptr<demo::OrbitCameraController>();
		auto root = sceneManager->getActiveScene()->getRoot();
		if (!root)
			return std::shared_ptr<demo::OrbitCameraController>();
		auto controllers = root->getChildrenOfType<demo::OrbitCameraController>();
		return controllers.empty() ? nullptr : controllers[0];
	};
	

	// Load demo scene first (async) and wait for it to complete
	auto load = sceneManager->loadScene("Demo");

	auto mainDemoUI = std::make_shared<demo::MainDemoImGuiUI>(engine);
	setupImGui(imguiManager, mainDemoUI, dayNightCycle);
	
	// Run engine
	engine.run();
	return 0;
}