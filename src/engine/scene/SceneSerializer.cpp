#include "engine/scene/SceneSerializer.h"

#include <fstream>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "engine/scene/NodeTypeRegistry.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/Node.h"
#include "engine/scene/nodes/PlaceholderNode.h"
#include "engine/scene/nodes/SpatialNode.h"

namespace engine::scene
{
namespace
{
using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace engine::scene::nodes;

// Bumped to 2 with the registry-based, props-grouped node format.
constexpr int kSceneFormatVersion = 2;

json vec3ToJson(const glm::vec3 &v) { return json::array({v.x, v.y, v.z}); }

glm::vec3 jsonToVec3(const json &j, const glm::vec3 &fallback)
{
	if (!j.is_array() || j.size() < 3)
		return fallback;
	return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

json transformToJson(const Transform &t)
{
	json j;
	j["position"] = vec3ToJson(t.getLocalPosition());
	j["euler"]    = vec3ToJson(t.getLocalEulerAngles());
	j["scale"]    = vec3ToJson(t.getLocalScale());
	return j;
}

void applyTransform(Transform &t, const json &j)
{
	if (j.contains("position")) t.setLocalPosition(jsonToVec3(j["position"], glm::vec3(0.0f)));
	if (j.contains("euler"))    t.setLocalEulerAngles(jsonToVec3(j["euler"], glm::vec3(0.0f)));
	if (j.contains("scale"))    t.setLocalScale(jsonToVec3(j["scale"], glm::vec3(1.0f)));
}

// The live main camera may be an editor-only camera that opts out of
// serialization (the editor drives the viewport through it). The persisted scene
// must still record which saved camera is main, so when the live one is not
// serializable we fall back to the first serializable camera in hierarchy order.
const CameraNode *firstSerializableCamera(const Node &node)
{
	if (!node.isSerializable())
		return nullptr;
	if (const auto *camera = dynamic_cast<const CameraNode *>(&node))
		return camera;
	for (const auto &child : node.getChildren())
	{
		if (!child)
			continue;
		if (const auto *found = firstSerializableCamera(*child))
			return found;
	}
	return nullptr;
}

json nodeToJson(const Node &node, const NodeSerializeContext &ctx, const Node *mainCamera)
{
	json j;
	if (auto name = node.getName())
		j["name"] = *name;
	j["enabled"] = node.isEnabled();

	if (const auto *placeholder = dynamic_cast<const PlaceholderNode *>(&node))
	{
		// Round-trip the unknown type verbatim so it restores in a build that
		// has the real type.
		j["type"] = placeholder->getMissingType();
		try
		{
			j["props"] = json::parse(placeholder->getPreservedProps());
		}
		catch (...)
		{
			j["props"] = json::object();
		}
	}
	else
	{
		std::string typeName = NodeTypeRegistry::instance().typeNameOf(node);
		if (typeName.empty())
			typeName = "Node"; // unregistered concrete type: persist as a plain node
		j["type"] = typeName;
		json props = json::object();
		if (const auto *info = NodeTypeRegistry::instance().find(typeName); info && info->serialize)
			info->serialize(node, props, ctx);
		j["props"] = props;
	}

	if (const auto *spatial = dynamic_cast<const SpatialNode *>(&node))
		j["transform"] = transformToJson(spatial->getTransform());

	// Which camera is the main one is a scene-level fact the node can't know.
	if (dynamic_cast<const CameraNode *>(&node) && &node == mainCamera)
		j["main"] = true;

	// Reserved for a future behaviour system - round-trips untouched.
	j["scripts"] = json::array();

	json children = json::array();
	for (const auto &child : node.getChildren())
	{
		// Editor-only helper nodes (the editor camera, controllers) opt out of
		// serialization, so their whole subtree is skipped.
		if (child && child->isSerializable())
			children.push_back(nodeToJson(*child, ctx, mainCamera));
	}
	j["children"] = children;
	return j;
}

Node::Ptr nodeFromJson(const json &j, const NodeSerializeContext &ctx, Scene &scene)
{
	const std::string type = j.value("type", "Node");
	const json props = j.value("props", json::object());

	Node::Ptr node;
	if (const auto *info = NodeTypeRegistry::instance().find(type))
	{
		node = info->factory();
		if (node && info->deserialize)
			info->deserialize(*node, props, ctx);
	}
	else
	{
		// Unknown type: keep the type name + all its data in a placeholder.
		spdlog::warn("SceneSerializer: unknown node type '{}' - substituting a PlaceholderNode (data preserved)", type);
		node = std::make_shared<PlaceholderNode>(type, props.dump());
	}
	if (!node)
		node = std::make_shared<Node>();

	if (j.contains("name"))
		node->setName(j["name"].get<std::string>());
	if (j.contains("enabled") && !j["enabled"].get<bool>())
		node->disable();
	if (j.contains("transform"))
	{
		if (auto spatial = std::dynamic_pointer_cast<SpatialNode>(node))
			applyTransform(spatial->getTransform(), j["transform"]);
	}

	if (auto camera = std::dynamic_pointer_cast<CameraNode>(node))
	{
		scene.addCamera(camera);
		if (j.value("main", false) || !scene.getMainCamera())
			scene.setMainCamera(camera);
	}

	if (j.contains("children"))
	{
		for (const auto &childJson : j["children"])
		{
			// keepWorldTransform = false: the stored transform IS the local one.
			if (auto child = nodeFromJson(childJson, ctx, scene))
				node->addChild(child, false);
		}
	}
	return node;
}

// Shared writer for both save modes. In self-contained mode the per-node
// serializers copy referenced project assets into <sceneFolder>/assets/ and
// store scene-relative paths; otherwise they store portable engine tokens.
bool writeScene(const Scene &scene, const fs::path &sceneFilePath, bool selfContained)
{
	const fs::path sceneFolder = sceneFilePath.parent_path();
	std::error_code ec;
	if (!sceneFolder.empty())
		fs::create_directories(sceneFolder, ec);

	NodeSerializeContext ctx;
	ctx.sceneFolder = sceneFolder;
	ctx.selfContained = selfContained;
	const Node *mainCamera = nullptr;
	if (auto live = scene.getMainCamera(); live && live->isSerializable())
		mainCamera = live.get();
	else if (scene.getRoot())
		mainCamera = firstSerializableCamera(*scene.getRoot());

	json root;
	root["version"] = kSceneFormatVersion;
	root["scene"] = sceneFilePath.stem().string();
	if (!scene.getSettingsOverride().empty())
	{
		try
		{
			root["settingsOverride"] = json::parse(scene.getSettingsOverride());
		}
		catch (...)
		{
		}
	}
	if (scene.getRoot())
		root["root"] = nodeToJson(*scene.getRoot(), ctx, mainCamera);

	std::ofstream out(sceneFilePath, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		spdlog::error("SceneSerializer: cannot open '{}' for writing", sceneFilePath.string());
		return false;
	}
	out << root.dump(2);
	if (!out)
	{
		spdlog::error("SceneSerializer: write failed for '{}'", sceneFilePath.string());
		return false;
	}
	spdlog::info("SceneSerializer: saved scene to '{}'{}", sceneFilePath.string(), selfContained ? " (self-contained)" : "");
	return true;
}

} // namespace

bool SceneSerializer::save(const Scene &scene, const std::filesystem::path &sceneFilePath)
{
	return writeScene(scene, sceneFilePath, false);
}

bool SceneSerializer::exportSelfContained(const Scene &scene, const std::filesystem::path &sceneFilePath)
{
	return writeScene(scene, sceneFilePath, true);
}

std::shared_ptr<nodes::Node> SceneSerializer::cloneNode(const nodes::Node &node)
{
	NodeSerializeContext ctx; // default (token) mode; no scene folder
	const json j = nodeToJson(node, ctx, nullptr);
	// nodeFromJson registers any cloned cameras into the scene it is given; use a
	// throwaway scene so the clone is fully detached until the caller parents it.
	Scene scratch;
	return nodeFromJson(j, ctx, scratch);
}

std::shared_ptr<Scene> SceneSerializer::load(const std::filesystem::path &sceneFilePath)
{
	std::ifstream in(sceneFilePath, std::ios::binary);
	if (!in)
	{
		spdlog::error("SceneSerializer::load: cannot open '{}'", sceneFilePath.string());
		return nullptr;
	}

	json root;
	try
	{
		in >> root;
	}
	catch (const json::parse_error &e)
	{
		spdlog::error("SceneSerializer::load: parse error in '{}': {}", sceneFilePath.string(), e.what());
		return nullptr;
	}

	const int version = root.value("version", 0);
	if (version != kSceneFormatVersion)
		spdlog::warn("SceneSerializer::load: scene version {} differs from current {} - loading best-effort",
		             version, kSceneFormatVersion);

	auto scene = std::make_shared<Scene>();
	// Discard the constructor-seeded camera; the saved scene supplies its own (and
	// flags which one is main). Without this the seed lingers as an orphaned active
	// camera and can pre-empt the saved main on load.
	scene->clearCameras();
	if (root.contains("settingsOverride") && root["settingsOverride"].is_object())
		scene->setSettingsOverride(root["settingsOverride"].dump());
	NodeSerializeContext ctx;
	ctx.sceneFolder = sceneFilePath.parent_path();
	if (root.contains("root"))
		scene->setRoot(nodeFromJson(root["root"], ctx, *scene));

	spdlog::info("SceneSerializer: loaded scene from '{}'", sceneFilePath.string());
	return scene;
}

} // namespace engine::scene
