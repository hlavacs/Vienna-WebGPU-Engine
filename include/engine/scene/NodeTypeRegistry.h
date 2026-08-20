#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/reflection/JsonReflector.h"

#include "engine/scene/nodes/Node.h"

namespace engine::scene
{

/**
 * @brief Context handed to a node's (de)serialization so type-specific code can
 * resolve asset references (e.g. a model's path).
 *
 * Default save stores portable engine path tokens (`asset://`, `resource://`)
 * resolved via PathProvider. When @ref selfContained is set (the "export"
 * flow), project assets are instead copied into `sceneFolder/assets/` and
 * referenced by a path relative to @ref sceneFolder, so the whole folder is
 * movable. Engine resources stay `resource://` tokens in both modes - the
 * engine provides them on every machine.
 */
struct NodeSerializeContext
{
	std::filesystem::path sceneFolder;
	bool selfContained = false;
};

/**
 * @brief One registered node type: how to create it and how to (de)serialize
 * its own type-specific data. Common fields (name, enabled, transform) are
 * handled by the serializer, not here.
 */
struct NodeTypeInfo
{
	std::string typeName;	 ///< Stable key written to scene files
	std::string displayName; ///< Shown in the editor's Add menu
	std::function<nodes::Node::Ptr()> factory;
	std::function<void(const nodes::Node &, nlohmann::json &, const NodeSerializeContext &)> serialize;
	std::function<void(nodes::Node &, const nlohmann::json &, const NodeSerializeContext &)> deserialize;
};

/**
 * @class NodeTypeRegistry
 * @brief Maps a stable type-name string to a factory + (de)serialization hooks.
 *
 * The engine registers its built-in node types (Node, Spatial, Camera, Light,
 * Model). A project registers its own custom node types once at startup, after
 * which they appear in the editor's Add menu and round-trip through scene files.
 *
 * If a saved scene references a type that the current build did not register
 * (e.g. a scene exported from a project that has a custom node this build lacks),
 * the loader substitutes a PlaceholderNode that preserves the data - the type
 * name and its serialized props - so nothing is lost and it restores fully when
 * opened in a build that has the type. Behaviour (the C++ code) only runs where
 * the type is compiled in; this registry never carries executable code.
 *
 * Node.h itself stays free of any JSON dependency: the serialization hooks live
 * here, in the registry, not on the node.
 */
class NodeTypeRegistry
{
  public:
	static NodeTypeRegistry &instance();

	/**
	 * @brief Register a node type.
	 * @tparam T The concrete node class (used for the typeid reverse lookup).
	 * @param typeName Stable key written to scene files (do not rename casually).
	 * @param displayName Label shown in the editor.
	 * @param factory Creates a default instance (T need not be default-constructible).
	 * @param serialize Writes the node's type-specific data; omit if it has none.
	 * @param deserialize Reads the node's type-specific data; omit if it has none.
	 */
	template <class T>
	void registerType(
		std::string typeName,
		std::string displayName,
		std::function<nodes::Node::Ptr()> factory,
		std::function<void(const nodes::Node &, nlohmann::json &, const NodeSerializeContext &)> serialize = {},
		std::function<void(nodes::Node &, const nlohmann::json &, const NodeSerializeContext &)> deserialize = {})
	{
		NodeTypeInfo info;
		info.typeName = typeName;
		info.displayName = std::move(displayName);
		info.factory = std::move(factory);
		info.serialize = std::move(serialize);
		info.deserialize = std::move(deserialize);
		m_byTypeIndex[std::type_index(typeid(T))] = typeName;
		if (m_types.find(typeName) == m_types.end())
			m_order.push_back(typeName);
		m_types[typeName] = std::move(info);
	}

	/**
	 * @brief One-call registration for scripts. Supplies a default make_shared
	 * factory (T must be default-constructible) and uses @p typeName as the display
	 * name. serialize/deserialize are optional - omit them for behaviour with no
	 * saved parameters. Prefer the VIENNA_REGISTER_NODE macro for the common case.
	 */
	template <class T>
	void registerScript(
		std::string typeName,
		std::function<void(const nodes::Node &, nlohmann::json &, const NodeSerializeContext &)> serialize = {},
		std::function<void(nodes::Node &, const nlohmann::json &, const NodeSerializeContext &)> deserialize = {})
	{
		std::string display = typeName;
		// When the script does not hand-write serialize/deserialize, drive both from
		// its reflect() field list automatically - the public fields it exposes are
		// stored in JSON and restored, keeping their member-initializer defaults for
		// any key missing from the file.
		if (!serialize)
			serialize = [](const nodes::Node &n, nlohmann::json &j, const NodeSerializeContext &)
			{ engine::reflection::reflectToJson(n, j); };
		if (!deserialize)
			deserialize = [](nodes::Node &n, const nlohmann::json &j, const NodeSerializeContext &)
			{ engine::reflection::reflectFromJson(n, j); };
		registerType<T>(std::move(typeName), std::move(display),
			[] { return std::shared_ptr<nodes::Node>(std::make_shared<T>()); },
			std::move(serialize), std::move(deserialize));
	}

	/** @brief Look up a type by its stable name, or nullptr if not registered. */
	[[nodiscard]] const NodeTypeInfo *find(const std::string &typeName) const;

	/** @brief The registered type name for a live node, or "" if its concrete
	 *  type was never registered. */
	[[nodiscard]] std::string typeNameOf(const nodes::Node &node) const;

	/** @brief All registered types in registration order (for the Add menu). */
	[[nodiscard]] std::vector<const NodeTypeInfo *> all() const;

  private:
	std::unordered_map<std::string, NodeTypeInfo> m_types;
	std::unordered_map<std::type_index, std::string> m_byTypeIndex;
	std::vector<std::string> m_order;
};

/**
 * @brief Register the engine's built-in node types with the registry. Called
 * once during engine initialization, before any scene is loaded.
 */
void registerBuiltinNodeTypes();

} // namespace engine::scene

/**
 * @brief Register a script node type at program start. Put it at file scope in a
 * project script .cpp, e.g. `REGISTER_NODE(Rotator);` registers the class `Rotator`
 * under the name "Rotator". Its reflect() fields are saved/loaded/inspected
 * automatically. For fully custom (de)serialization, call
 * `NodeTypeRegistry::instance().registerScript<T>(name, serialize, deserialize)`
 * from a static initializer instead.
 */
#define REGISTER_NODE(TYPE)                                                               \
	static const bool TYPE##_node_registered =                                            \
		(::engine::scene::NodeTypeRegistry::instance().registerScript<TYPE>(#TYPE), true)
