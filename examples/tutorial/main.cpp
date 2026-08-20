/**
 * Tutorials: Unlit Shader with Custom Bind Group
 * Learn how to create custom shaders and bind groups
 */
#include "engine/EngineMain.h"
// ^ This has to be on top to define SDL_MAIN_HANDLED ^
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/ShaderType.h"
#include "engine/resources/Image.h"

#include "CustomRenderNode.h"
#include "FreeFlyCamera.h"

#include <cmath>

using PathProvider = engine::core::PathProvider;

// Tutorial 03 - Step 4: GPU-facing properties for the glass shader.
// This struct feeds the @group(2) uniform buffer, so it must match the
// GlassMaterialUniforms struct in glass.wgsl byte for byte (32 bytes).
struct GlassProperties
{
	// rgb = glass tint, a = base opacity when looking straight through
	// (LOW - real glass transmits most of the background head-on)
	glm::vec4 color{0.25f, 0.35f, 0.4f, 0.15f};
	// x = reflection strength, y = specular shininess (Blinn-Phong exponent),
	// z = specular strength, w = shadow dimming
	glm::vec4 params{2.0f, 64.0f, 1.0f, 0.7f};
};
static_assert(sizeof(GlassProperties) % 16 == 0, "uniform data must stay 16-byte aligned");

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
	mainCamera->getTransform().lookAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	mainCamera->setBackgroundColor(glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
	auto freeFlyCameraController = std::make_shared<demo::FreeFlyCameraController>(mainCamera);
	rootNode->addChild(freeFlyCameraController);

	auto maybeModelFourareen = resourceManager->m_modelManager->createModel(PathProvider::getAssets("fourareen.obj"));
	if (!maybeModelFourareen.has_value())
	{
		spdlog::error("Failed to load fourareen.obj model");
		return -1;
	}
	auto fourareenNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelFourareen.value());
	fourareenNode->getTransform().setLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	rootNode->addChild(fourareenNode);

	auto maybeModelFloor = resourceManager->m_modelManager->createModel(PathProvider::getAssets("plane.obj"));
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
	// Tutorial 02 - Step 6: add a custom @group(1) entry here, e.g.
	//   unlitShader.groups[1] = {"TileUniforms", engine::rendering::BindGroupType::Custom, engine::rendering::BindGroupReuse::PerObject, {}};
	// (index 1 is free in this shader - it has no Scene group; naming the entry
	// makes the engine treat the slot as a custom group instead of the Scene role)

	shaderRegistry.registerShader(shaderFactory.buildFromDescriptor(unlitShader));
#pragma endregion

#pragma region Tutorial Material Creation and Setup
	auto unlitProperties = engine::rendering::UnlitProperties{};
	unlitProperties.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	auto diffuseTexture = resourceManager->m_textureManager->createTextureFromFile(
		PathProvider::getAssets("cobblestone_floor_08_diff_2k.jpg")
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

#pragma region Tutorial 03 Glass Shader
	// Tutorial 03 - Step 11: Give the glass something to reflect.
	// Reflections need an environment image. Instead of shipping a multi-MB
	// .hdr file, the tutorial synthesizes a tiny equirectangular sky: a
	// vertical gradient (blue zenith -> pale horizon -> dark ground) plus a
	// bright HDR sun blob aligned with the directional light below. The
	// engine treats it exactly like a loaded .hdr: it GGX-prefilters it into
	// the mip chain the shader samples through prefiltered_env (binding 11),
	// and the skybox pass draws it behind the scene. Rendering the very sky
	// the glass mirrors is what makes the reflections read as real - and the
	// forward pass needs a shaded background to blend the glass against.
	constexpr uint32_t skyWidth = 64;
	constexpr uint32_t skyHeight = 32;
	constexpr float pi = 3.14159265f;
	// Raw linear HDR values, chosen for the engine's ACES tonemap at its
	// default exposure: the sky lands in the readable midtones instead of
	// clipping to white, so the brighter glass rim stays distinguishable.
	const glm::vec3 zenithColor{0.05f, 0.13f, 0.35f};
	const glm::vec3 horizonColor{0.2f, 0.28f, 0.42f};
	const glm::vec3 groundColor{0.08f, 0.075f, 0.07f};
	// Same rotation as the sun light below, so the reflected sun blob and the
	// specular glint sit in the same spot on the glass.
	const glm::vec3 towardSun = glm::quat(glm::radians(glm::vec3(45.0f, 135.0f, 0.0f))) * glm::vec3(0.0f, 0.0f, -1.0f);
	std::vector<float> skyPixels;
	skyPixels.reserve(static_cast<size_t>(skyWidth) * skyHeight * 4);
	for (uint32_t pixelY = 0; pixelY < skyHeight; ++pixelY)
	{
		for (uint32_t pixelX = 0; pixelX < skyWidth; ++pixelX)
		{
			// Invert the equirect mapping direction_to_equirect_uv() uses:
			// u spans the azimuth around the Y axis, v the angle from zenith.
			const float azimuth = ((pixelX + 0.5f) / skyWidth - 0.5f) * 2.0f * pi;
			const float polar = (pixelY + 0.5f) / skyHeight * pi;
			const glm::vec3 direction{
				std::cos(azimuth) * std::sin(polar),
				std::cos(polar),
				std::sin(azimuth) * std::sin(polar)
			};
			glm::vec3 color = direction.y >= 0.0f
				? glm::mix(horizonColor, zenithColor, direction.y)
				: glm::mix(horizonColor, groundColor, -direction.y);
			// HDR values far above 1.0 are what make the reflected sun pop
			// after tonemapping; the pow keeps the blob tight.
			const float sunAmount = glm::clamp(glm::dot(direction, towardSun), 0.0f, 1.0f);
			color += glm::vec3(8.0f, 7.5f, 6.5f) * std::pow(sunAmount, 200.0f);
			skyPixels.insert(skyPixels.end(), {color.r, color.g, color.b, 1.0f});
		}
	}
	auto skyImage = std::make_shared<engine::resources::Image>(
		skyWidth, skyHeight, engine::resources::ImageFormat::Type::HDR_RGBA16F, std::move(skyPixels)
	);
	auto maybeSkyTexture = resourceManager->m_textureManager->createImageTexture(skyImage);
	if (!maybeSkyTexture.has_value())
	{
		spdlog::error("Failed to create the procedural sky texture");
		return -1;
	}
	// The camera owns the environment reference; the renderer prefilters it
	// once and binds it into every forward draw's Scene group.
	mainCamera->setEnvironmentTexture(maybeSkyTexture.value()->getHandle());
	mainCamera->setSkyboxEnabled(true);

	// Tutorial 03 - Step 12: Register the glass shader.
	// No groups metadata is needed: Frame/Scene/Object come from the engine
	// #includes and the material group is a single uniform buffer with no
	// texture slots - reflection recovers everything from the WGSL.
	engine::rendering::webgpu::ShaderDescriptor glassShader;
	glassShader.name         = "glass";
	glassShader.type         = engine::rendering::ShaderType::Custom;
	glassShader.path         = PathProvider::getShaders("glass.wgsl");
	glassShader.vertexLayout = engine::rendering::VertexLayout::PositionNormalUV;
	shaderRegistry.registerShader(shaderFactory.buildFromDescriptor(glassShader));

	// Tutorial 03 - Step 13: Create the glass material and mark it Transparent.
	auto glassProperties = GlassProperties{};
	auto maybeGlassMaterial = resourceManager->m_materialManager->createMaterial(
		"Glass_Material",
		glassProperties,
		"glass",
		{} // no textures - the look comes entirely from the uniform parameters
	);
	if (!maybeGlassMaterial.has_value())
	{
		spdlog::error("Failed to create glass material");
		return -1;
	}
	auto glassMaterial = maybeGlassMaterial.value();
	// The Transparent feature flag is what switches the pipeline to alpha
	// blending with depth writes off, and (together with the custom shader)
	// routes the material through the forward transparency pass.
	glassMaterial->setFeatureMask(glassMaterial->getFeatureMask() | engine::rendering::MaterialFeature::Flag::Transparent);

	// Tutorial 03 - Step 14: A glass cylinder built from the engine's stock mesh.
	// PathProvider::getResource resolves into the engine's resources folder -
	// the same place the shader #includes come from. The curved shell is ideal
	// for glass: every horizontal direction is represented, so the fresnel rim
	// and the environment reflection sweep smoothly across it.
	auto maybeGlassModel = resourceManager->m_modelManager->createModel(
		PathProvider::getResource("cylinder.obj")
	);
	if (!maybeGlassModel.has_value())
	{
		spdlog::error("Failed to load cylinder.obj model for the glass object");
		return -1;
	}
	auto glassModel = maybeGlassModel.value();

	// Tutorial 03 - Step 14: Uncomment this line after reading through the shader.
	// It assigns the glass material to the cylinder's only submesh - before that
	// the cylinder renders opaque with its plain gray material from the .mtl file.
	// ------------------
	// glassModel->getSubmeshes()[0].material = glassMaterial->getHandle();

	// A tall glass column beside the boat: the unit cylinder (radius 1,
	// height 2, centered on the origin) is scaled to radius 0.5 / height 3 and
	// lifted so its base sits on the floor. Placed in front of the boat's bow,
	// inside the diagonal shadow the boat casts, so the shadow crosses the
	// cylinder's lower body - that is where the received shadow is easiest to
	// see - while its upper half rises above the floor's horizon, showing the
	// sky through the glass (and the boat itself through the lower half).
	auto glassNode = std::make_shared<engine::scene::nodes::ModelRenderNode>(glassModel);
	glassNode->getTransform().setLocalPosition(glm::vec3(-1.0f, 1.5f, 1.1f));
	glassNode->getTransform().setLocalScale(glm::vec3(0.5f, 1.5f, 0.5f));
	rootNode->addChild(glassNode);
#pragma endregion

#pragma region Setup Lights
	auto sunLight = std::make_shared<engine::scene::nodes::LightNode>();
	engine::rendering::DirectionalLight sunLightData;
	sunLightData.color = glm::vec3(1.0f, 1.0f, 1.0f);
	sunLightData.intensity = 1.0f;
	// Without this the engine renders no shadow maps at all - calculate_shadow()
	// (Tutorial 03) would see shadowCount == 0 and return fully lit everywhere.
	sunLightData.castShadows = true;
	sunLight->getLight().setData(sunLightData);
	// The sun sits up-left behind the camera: the surfaces facing the viewer
	// stay lit, and the boat's shadow falls diagonally past its bow - right
	// across the glass cylinder, where the camera can actually see it.
	sunLight->getTransform().setLocalRotation(glm::quat(glm::radians(glm::vec3(45.0f, 135.0f, 0.0f))));
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
