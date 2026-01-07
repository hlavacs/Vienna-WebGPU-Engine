#pragma once

#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/SpatialNode.h"

namespace engine::scene::nodes
{

/**
 * @brief A node that represents a light in the scene.
 * 
 * Automatically adds its light data to the RenderCollector during scene traversal.
 * Inherits from SpatialNode to have a transform for positioning the light.
 * Uses the new Light class with variant-based type system for easier access to type-specific properties.
 */
class LightNode : public nodes::RenderNode, public nodes::SpatialNode
{
  public:
	using Ptr = std::shared_ptr<LightNode>;

	/**
	 * @brief Constructs a light node with default ambient light.
	 */
	LightNode()
	{
		addNodeType(nodes::NodeType::Light);
		m_light = engine::rendering::Light(engine::rendering::AmbientLight{});
	}

	virtual ~LightNode() = default;

	/**
	 * @brief Add this light to the render collector.
	 * @param collector The render collector to add light data to.
	 */
	void onRenderCollect(engine::rendering::RenderCollector &collector) override
	{
		if (getTransform())
		{
			// Update light transform from node's world transform
			m_light.setTransform(getTransform()->getWorldMatrix());
		}

		// Add light directly to collector
		collector.addLight(m_light);
	}

	/**
	 * @brief Draws debug visualization for the light.
	 * @param collector The debug render collector to add primitives to.
	 */
	void onDebugDraw(engine::rendering::DebugRenderCollector &collector) override
	{
		if (!getTransform())
			return;

		auto worldMatrix = getTransform()->getWorldMatrix();
		glm::vec3 position = glm::vec3(worldMatrix[3]);
		glm::vec3 direction = -glm::vec3(worldMatrix[2]); // Forward direction

		// Get color from light (using visitor pattern)
		glm::vec3 lightColor = std::visit(
			[](const auto& light) -> glm::vec3 { return light.color; },
			m_light.getData()
		);
		glm::vec4 color = glm::vec4(lightColor, 1.0f);

		// Light type: 0=ambient, 1=directional, 2=point, 3=spot
		switch (m_light.getLightType())
		{
		case 0: // Ambient - no debug visualization
			break;

		case 1: // Directional - draw arrow in light direction
		case 3: // Spot - draw arrow in light direction
		{
			float arrowLength = 0.5f;
			float arrowHeadSize = 0.2f;
			glm::vec3 endPos = position - direction * arrowLength;
			collector.addArrow(position, endPos, arrowHeadSize, color);
			break;
		}

		case 2: // Point - draw 3 orthogonal disks (sphere representation)
		{
			float radius = 0.5f;
			
			// XY plane disk (normal along Z)
			collector.addDisk(position, glm::vec3(radius, radius, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
			
			// XZ plane disk (normal along Y)
			collector.addDisk(position, glm::vec3(radius, 0.0f, radius), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
			
			// YZ plane disk (normal along X)
			collector.addDisk(position, glm::vec3(0.0f, radius, radius), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
			break;
		}

		default:
			break;
		}
	}

	/**
	 * @brief Sets the light data.
	 * @param light Light object with type-specific data.
	 */
	void setLight(const engine::rendering::Light& light)
	{
		m_light = light;
	}

	/**
	 * @brief Gets the light object.
	 * @return Reference to the light.
	 */
	engine::rendering::Light& getLight()
	{
		return m_light;
	}

	/**
	 * @brief Gets the light object (const).
	 * @return Const reference to the light.
	 */
	const engine::rendering::Light& getLight() const
	{
		return m_light;
	}

	/**
	 * @brief Sets the light color (works for all light types).
	 * @param color RGB color values.
	 */
	void setColor(const glm::vec3 &color)
	{
		std::visit(
			[&color](auto& light) { light.color = color; },
			m_light.getData()
		);
	}

	/**
	 * @brief Gets the light color (works for all light types).
	 * @return The current RGB color.
	 */
	glm::vec3 getColor() const
	{
		return std::visit(
			[](const auto& light) -> glm::vec3 { return light.color; },
			m_light.getData()
		);
	}

	/**
	 * @brief Sets the light intensity (works for all light types).
	 * @param intensity The intensity value.
	 */
	void setIntensity(float intensity)
	{
		std::visit(
			[intensity](auto& light) { light.intensity = intensity; },
			m_light.getData()
		);
	}

	/**
	 * @brief Gets the light intensity (works for all light types).
	 * @return The current intensity.
	 */
	float getIntensity() const
	{
		return std::visit(
			[](const auto& light) -> float { return light.intensity; },
			m_light.getData()
		);
	}

	/**
	 * @brief Gets the light type.
	 * @return The current light type (0=ambient, 1=directional, 2=point, 3=spot).
	 */
	uint32_t getLightType() const
	{
		return m_light.getLightType();
	}

	/**
	 * @brief Sets whether this light casts shadows (only for directional, point, spot).
	 * @param castShadows True to enable shadow casting.
	 */
	void setCastShadows(bool castShadows)
	{
		std::visit(
			[castShadows](auto& light)
			{
				using T = std::decay_t<decltype(light)>;
				if constexpr (!std::is_same_v<T, engine::rendering::AmbientLight>)
				{
					light.castShadows = castShadows;
				}
			},
			m_light.getData()
		);
	}

	/**
	 * @brief Gets whether this light casts shadows.
	 * @return True if the light can cast shadows.
	 */
	bool getCastShadows() const
	{
		return m_light.canCastShadows();
	}

  private:
	engine::rendering::Light m_light;
};

} // namespace engine::scene::nodes
