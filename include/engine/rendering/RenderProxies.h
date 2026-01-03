#pragma once

#include <memory>
#include <glm/glm.hpp>

#include "engine/core/Handle.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/LightUniforms.h"

// Forward declarations
namespace engine::scene::nodes
{
	class CameraNode;
}

namespace engine::rendering
{

/**
 * @brief Base interface for all render proxies.
 * 
 * RenderProxies decouple scene nodes from the rendering system.
 * Nodes produce proxies during scene traversal, which are then
 * processed by the renderer to create GPU resources and submit draw calls.
 */
struct RenderProxy
{
	virtual ~RenderProxy() = default;
	
	/**
	 * @brief Gets the render layer for sorting.
	 * @return Layer index (lower values render first).
	 */
	virtual uint32_t getLayer() const = 0;

	/**
	 * @brief Gets a unique ID for this proxy's source object (for bind group caching).
	 * @return Object ID used for bind group caching. Return 0 if caching not needed.
	 */
	virtual uint64_t getObjectID() const = 0;
};

/**
 * @brief Proxy for rendering 3D models.
 * 
 * Contains all data needed to render a model instance:
 * - Model geometry reference
 * - Material reference (can override model's default material)
 * - World transform matrix
 * - Render layer for sorting
 * - Object ID for bind group caching
 */
struct ModelRenderProxy : public RenderProxy
{
	engine::core::Handle<Model> model;
	engine::core::Handle<Material> material; // Optional: overrides model's material if valid
	glm::mat4 transform;
	uint32_t layer;
	uint64_t objectID; // Unique ID for bind group caching (typically node ID)

	ModelRenderProxy(
		engine::core::Handle<Model> modelHandle,
		const glm::mat4 &worldTransform,
		uint32_t renderLayer = 0,
		engine::core::Handle<Material> materialOverride = {},
		uint64_t objID = 0
	) : model(modelHandle),
		material(materialOverride),
		transform(worldTransform),
		layer(renderLayer),
		objectID(objID)
	{
	}

	uint32_t getLayer() const override { return layer; }
	uint64_t getObjectID() const override { return objectID; }
};

/**
 * @brief Proxy for rendering lights.
 * 
 * Contains all data needed for a light source:
 * - Light type (directional, point, spot, ambient)
 * - Color, intensity, attenuation
 * - World transform (position/direction)
 */
struct LightRenderProxy : public RenderProxy
{
	LightStruct lightData;
	uint32_t layer;

	LightRenderProxy(
		const LightStruct &light,
		uint32_t renderLayer = 0
	) : lightData(light),
		layer(renderLayer)
	{
	}

	uint32_t getLayer() const override { return layer; }
	uint64_t getObjectID() const override { return 0; } // Lights don't need per-instance caching
};

/**
 * @brief Proxy for camera registration.
 * 
 * Cameras create this proxy to register themselves with the scene during collection.
 * This allows the scene to discover cameras that are part of the scene graph
 * without maintaining a separate explicit camera list.
 */
struct CameraRenderProxy : public RenderProxy
{
	std::shared_ptr<engine::scene::nodes::CameraNode> camera;
	uint32_t layer;

	CameraRenderProxy(
		std::shared_ptr<engine::scene::nodes::CameraNode> cameraNode,
		uint32_t renderLayer = 0
	) : camera(cameraNode),
		layer(renderLayer)
	{
	}

	uint32_t getLayer() const override { return layer; }
	uint64_t getObjectID() const override { return 0; } // Cameras don't need per-instance caching
};

/**
 * @brief Proxy for rendering UI elements.
 * 
 * Placeholder for future UI rendering system.
 * Will contain screen-space position, size, and UI element reference.
 */
struct UIRenderProxy : public RenderProxy
{
	// Placeholder: Will be implemented when UI system is added
	// engine::core::Handle<UIElement> uiElement;
	glm::vec2 position;
	glm::vec2 size;
	uint32_t layer;

	UIRenderProxy(
		const glm::vec2 &screenPosition,
		const glm::vec2 &elementSize,
		uint32_t renderLayer = 0
	) : position(screenPosition),
		size(elementSize),
		layer(renderLayer)
	{
	}

	uint32_t getLayer() const override { return layer; }
	uint64_t getObjectID() const override { return 0; }
};

/**
 * @brief Proxy for rendering debug primitives.
 * 
 * Placeholder for debug visualization system.
 * Will contain debug primitive type, transform, color, etc.
 */
struct DebugRenderProxy : public RenderProxy
{
	// Placeholder: Will be implemented when debug rendering is added
	enum class PrimitiveType
	{
		Line,
		Box,
		Sphere,
		Frustum
	};

	PrimitiveType primitiveType;
	glm::mat4 transform;
	glm::vec4 color;
	uint32_t layer;

	DebugRenderProxy(
		PrimitiveType type,
		const glm::mat4 &worldTransform,
		const glm::vec4 &debugColor = glm::vec4(1.0f),
		uint32_t renderLayer = 0
	) : primitiveType(type),
		transform(worldTransform),
		color(debugColor),
		layer(renderLayer)
	{
	}

	uint32_t getLayer() const override { return layer; }
	uint64_t getObjectID() const override { return 0; }
};

} // namespace engine::rendering
