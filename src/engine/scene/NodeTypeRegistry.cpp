#include "engine/scene/NodeTypeRegistry.h"

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/Texture.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"
#include "engine/scene/nodes/SpatialNode.h"

namespace engine::scene
{
namespace
{
using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace engine::scene::nodes;
using namespace engine::rendering;

json vec3ToJson(const glm::vec3 &v) { return json::array({v.x, v.y, v.z}); }
json vec4ToJson(const glm::vec4 &v) { return json::array({v.x, v.y, v.z, v.w}); }

glm::vec3 jsonToVec3(const json &j, const glm::vec3 &fallback)
{
	if (!j.is_array() || j.size() < 3)
		return fallback;
	return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

glm::vec4 jsonToVec4(const json &j, const glm::vec4 &fallback)
{
	if (!j.is_array() || j.size() < 4)
		return fallback;
	return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

// Serialize an asset reference. Engine resources stay "resource://" tokens (the
// engine provides them on every machine). For a self-contained export, project
// assets are copied into <sceneFolder>/assets/ and referenced by a scene-relative
// path; otherwise they are stored as portable "asset://" / absolute tokens.
std::string serializeAssetReference(const fs::path &path, const NodeSerializeContext &ctx)
{
	if (path.empty())
		return "";
	if (engine::core::PathProvider::isUnderResources(path))
		return engine::core::PathProvider::toEnginePath(path);
	if (ctx.selfContained && !ctx.sceneFolder.empty())
	{
		std::error_code ec;
		const fs::path destinationDir = ctx.sceneFolder / "assets";
		fs::create_directories(destinationDir, ec);
		const fs::path destination = destinationDir / path.filename();
		fs::copy_file(path, destination, fs::copy_options::overwrite_existing, ec);
		if (!ec)
			return "assets/" + path.filename().generic_string();
		spdlog::warn("SceneSerializer: could not copy asset '{}' into the scene folder ({})", path.generic_string(), ec.message());
	}
	return engine::core::PathProvider::toEnginePath(path);
}

// Resolve a stored asset reference to an absolute path. Handles engine tokens
// ("asset://" / "resource://"), absolute paths, and scene-relative paths (from a
// self-contained scene), the last resolved against the scene folder.
fs::path resolveAssetReference(const std::string &token, const NodeSerializeContext &ctx)
{
	if (token.empty())
		return {};
	const std::string assetScheme = engine::core::PathProvider::kAssetScheme;
	const std::string resourceScheme = engine::core::PathProvider::kResourceScheme;
	if (token.rfind(assetScheme, 0) == 0 || token.rfind(resourceScheme, 0) == 0)
		return engine::core::PathProvider::resolveEnginePath(token);
	fs::path path(token);
	if (path.is_absolute())
		return path;
	return ctx.sceneFolder.empty() ? path : (ctx.sceneFolder / path);
}

void serializeLight(const Node &node, json &props, const NodeSerializeContext &)
{
	const auto &light = dynamic_cast<const LightNode &>(node).getLight();
	switch (light.getLightType())
	{
	case Light::Type::Directional:
	{
		const auto &d = light.asDirectional();
		props = {{"lightType", "Directional"}, {"color", vec3ToJson(d.color)}, {"intensity", d.intensity},
		         {"range", d.range}, {"castShadows", d.castShadows}, {"shadowBias", d.shadowBias},
		         {"shadowNormalBias", d.shadowNormalBias}, {"shadowMapSize", d.shadowMapSize},
		         {"shadowPCFKernel", d.shadowPCFKernel}, {"cascadeCount", d.cascadeCount}, {"splitLambda", d.splitLambda}};
		break;
	}
	case Light::Type::Point:
	{
		const auto &p = light.asPoint();
		props = {{"lightType", "Point"}, {"color", vec3ToJson(p.color)}, {"intensity", p.intensity},
		         {"range", p.range}, {"castShadows", p.castShadows}, {"shadowBias", p.shadowBias},
		         {"shadowNormalBias", p.shadowNormalBias}, {"shadowMapSize", p.shadowMapSize}, {"shadowPCFKernel", p.shadowPCFKernel}};
		break;
	}
	case Light::Type::Spot:
	{
		const auto &s = light.asSpot();
		props = {{"lightType", "Spot"}, {"color", vec3ToJson(s.color)}, {"intensity", s.intensity},
		         {"spotAngle", s.spotAngle}, {"spotSoftness", s.spotSoftness}, {"range", s.range},
		         {"castShadows", s.castShadows}, {"shadowBias", s.shadowBias}, {"shadowNormalBias", s.shadowNormalBias},
		         {"shadowMapSize", s.shadowMapSize}, {"shadowPCFKernel", s.shadowPCFKernel}};
		break;
	}
	default:
	{
		const auto &a = light.asAmbient();
		props = {{"lightType", "Ambient"}, {"color", vec3ToJson(a.color)}, {"intensity", a.intensity}};
		break;
	}
	}
}

void deserializeLight(Node &node, const json &props, const NodeSerializeContext &)
{
	auto &lightNode = dynamic_cast<LightNode &>(node);
	const std::string type = props.value("lightType", "Ambient");
	const glm::vec3 color = jsonToVec3(props.value("color", json::array({1, 1, 1})), glm::vec3(1.0f));
	const float intensity = props.value("intensity", 1.0f);
	if (type == "Directional")
	{
		DirectionalLight d;
		d.color = color;
		d.intensity = intensity;
		d.range = props.value("range", 50.0f);
		d.castShadows = props.value("castShadows", false);
		d.shadowBias = props.value("shadowBias", 0.002f);
		d.shadowNormalBias = props.value("shadowNormalBias", 0.3f);
		d.shadowMapSize = props.value("shadowMapSize", 2048u);
		d.shadowPCFKernel = props.value("shadowPCFKernel", 2u);
		d.cascadeCount = props.value("cascadeCount", 4u);
		d.splitLambda = props.value("splitLambda", 0.7f);
		lightNode.setLight(Light(d));
	}
	else if (type == "Point")
	{
		PointLight p;
		p.color = color;
		p.intensity = intensity;
		p.range = props.value("range", 10.0f);
		p.castShadows = props.value("castShadows", false);
		p.shadowBias = props.value("shadowBias", 0.005f);
		p.shadowNormalBias = props.value("shadowNormalBias", 0.3f);
		p.shadowMapSize = props.value("shadowMapSize", 1024u);
		p.shadowPCFKernel = props.value("shadowPCFKernel", 1u);
		lightNode.setLight(Light(p));
	}
	else if (type == "Spot")
	{
		SpotLight s;
		s.color = color;
		s.intensity = intensity;
		s.spotAngle = props.value("spotAngle", 0.5f);
		s.spotSoftness = props.value("spotSoftness", 0.2f);
		s.range = props.value("range", 10.0f);
		s.castShadows = props.value("castShadows", false);
		s.shadowBias = props.value("shadowBias", 0.002f);
		s.shadowNormalBias = props.value("shadowNormalBias", 0.3f);
		s.shadowMapSize = props.value("shadowMapSize", 2048u);
		s.shadowPCFKernel = props.value("shadowPCFKernel", 1u);
		lightNode.setLight(Light(s));
	}
	else
	{
		AmbientLight a;
		a.color = color;
		a.intensity = props.value("intensity", 0.1f);
		lightNode.setLight(Light(a));
	}
}

void serializeCamera(const Node &node, json &props, const NodeSerializeContext &ctx)
{
	const auto &camera = dynamic_cast<const CameraNode &>(node);
	props["fov"] = camera.getFov();
	props["near"] = camera.getNear();
	props["far"] = camera.getFar();
	props["perspective"] = camera.isPerspective();
	props["orthographicSize"] = camera.getOrthographicSize();
	props["background"] = vec4ToJson(camera.getBackgroundColor());
	props["skybox"] = camera.isSkyboxEnabled();
	props["irradiance"] = camera.isIrradianceEnabled();
	props["irradianceIntensity"] = camera.getIrradianceIntensity();
	// Environment map by portable reference; resolved on load via the TextureManager.
	if (auto env = camera.getEnvironmentTexture(); env && env->valid())
		if (auto tex = env->get(); tex && *tex && !(*tex)->getFilePath().empty())
			props["environment"] = serializeAssetReference((*tex)->getFilePath(), ctx);
}

void deserializeCamera(Node &node, const json &props, const NodeSerializeContext &ctx)
{
	auto &camera = dynamic_cast<CameraNode &>(node);
	camera.setFov(props.value("fov", 45.0f));
	camera.setNearFar(props.value("near", 0.1f), props.value("far", 100.0f));
	camera.setPerspective(props.value("perspective", true));
	camera.setOrthographicSize(props.value("orthographicSize", 5.0f));
	if (props.contains("background"))
		camera.setBackgroundColor(jsonToVec4(props["background"], glm::vec4(0.0f)));
	camera.setSkyboxEnabled(props.value("skybox", false));
	camera.setIrradianceEnabled(props.value("irradiance", false));
	camera.setIrradianceIntensity(props.value("irradianceIntensity", 1.0f));
	// Store the resolved env-map path; SceneManager loads + assigns it on scene init.
	const std::string envToken = props.value("environment", std::string{});
	if (!envToken.empty())
		camera.setPendingEnvironmentPath(resolveAssetReference(envToken, ctx).generic_string());
}

void serializeModel(const Node &node, json &props, const NodeSerializeContext &ctx)
{
	const auto &model = dynamic_cast<const ModelRenderNode &>(node);
	props["path"] = serializeAssetReference(model.getModelPath(), ctx);
	props["layer"] = model.getRenderLayer();

	// Record the assigned project material by name; resolved on load against the
	// MaterialManager. The editor assigns one material to all submeshes, so the
	// first submesh is representative. Fall back to the pending ref when the mesh
	// has not loaded yet (re-saving a freshly loaded scene).
	std::string materialName = model.getMaterialRef();
	if (auto modelOpt = model.getModel().get())
	{
		const auto &submeshes = (*modelOpt)->getSubmeshes();
		if (!submeshes.empty() && submeshes[0].material.valid())
			if (auto mat = submeshes[0].material.get(); mat && *mat && (*mat)->getName())
				materialName = (*mat)->getName().value();
	}
	if (!materialName.empty())
		props["material"] = materialName;
}

void deserializeModel(Node &node, const json &props, const NodeSerializeContext &ctx)
{
	auto &model = dynamic_cast<ModelRenderNode &>(node);
	model.setModelPath(resolveAssetReference(props.value("path", std::string{}), ctx));
	model.setRenderLayer(props.value("layer", 0u));
	model.setMaterialRef(props.value("material", std::string{}));
}
} // namespace

NodeTypeRegistry &NodeTypeRegistry::instance()
{
	static NodeTypeRegistry registry;
	return registry;
}

const NodeTypeInfo *NodeTypeRegistry::find(const std::string &typeName) const
{
	auto it = m_types.find(typeName);
	return it != m_types.end() ? &it->second : nullptr;
}

std::string NodeTypeRegistry::typeNameOf(const nodes::Node &node) const
{
	auto it = m_byTypeIndex.find(std::type_index(typeid(node)));
	return it != m_byTypeIndex.end() ? it->second : std::string{};
}

std::vector<const NodeTypeInfo *> NodeTypeRegistry::all() const
{
	std::vector<const NodeTypeInfo *> out;
	out.reserve(m_order.size());
	for (const auto &name : m_order)
	{
		auto it = m_types.find(name);
		if (it != m_types.end())
			out.push_back(&it->second);
	}
	return out;
}

void registerBuiltinNodeTypes()
{
	auto &registry = NodeTypeRegistry::instance();

	// Factories create UNNAMED nodes so an unnamed node round-trips unchanged;
	// the editor assigns a friendly default name when a node is added.
	registry.registerType<Node>("Node", "Empty Node",
		[]() { return std::make_shared<Node>(); });

	registry.registerType<SpatialNode>("Spatial", "Spatial Node",
		[]() { return std::make_shared<SpatialNode>(); });

	registry.registerType<CameraNode>("Camera", "Camera",
		[]() { return std::make_shared<CameraNode>(); },
		serializeCamera, deserializeCamera);

	registry.registerType<LightNode>("Light", "Light",
		[]() { return std::make_shared<LightNode>(); },
		serializeLight, deserializeLight);

	registry.registerType<ModelRenderNode>("Model", "Model",
		[]() { return std::make_shared<ModelRenderNode>(std::filesystem::path{}); },
		serializeModel, deserializeModel);
}

} // namespace engine::scene
