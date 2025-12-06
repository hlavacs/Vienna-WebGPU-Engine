#pragma once

#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/scene/entity/RenderNode.h"
#include "engine/scene/SpatialNode.h"

namespace engine::scene::entity
{

/**
 * @brief A node that represents a light in the scene.
 * 
 * Automatically adds its light data to the RenderCollector during scene traversal.
 * Inherits from SpatialNode to have a transform for positioning the light.
 */
class LightNode : public RenderNode, public SpatialNode
{
  public:
	using Ptr = std::shared_ptr<LightNode>;

	/**
	 * @brief Constructs a light node with default settings.
	 */
	LightNode()
	{
		addNodeType(NodeType::Light);
		m_light.color = glm::vec3(1.0f, 1.0f, 1.0f);
		m_light.intensity = 1.0f;
		m_light.light_type = 0; // Ambient by default
	}

	virtual ~LightNode() = default;

	/**
	 * @brief Collects this light for rendering.
	 * @param collector The render collector to add this light to.
	 */
	void onRenderCollect(engine::rendering::RenderCollector &collector) override
	{
		if (getTransform())
		{
			// Update light transform from node's world transform
			m_light.transform = getTransform()->getWorldMatrix();
		}
		
		// Add light to the collector
		collector.addLight(m_light);
	}

	/**
	 * @brief Sets the light color.
	 * @param color RGB color values.
	 */
	void setColor(const glm::vec3 &color)
	{
		m_light.color = color;
	}

	/**
	 * @brief Gets the light color.
	 * @return The current RGB color.
	 */
	const glm::vec3 &getColor() const
	{
		return m_light.color;
	}

	/**
	 * @brief Sets the light intensity.
	 * @param intensity The intensity value.
	 */
	void setIntensity(float intensity)
	{
		m_light.intensity = intensity;
	}

	/**
	 * @brief Gets the light intensity.
	 * @return The current intensity.
	 */
	float getIntensity() const
	{
		return m_light.intensity;
	}

	/**
	 * @brief Sets the light type.
	 * @param type 0=ambient, 1=directional, 2=point, 3=spot
	 */
	void setLightType(uint32_t type)
	{
		m_light.light_type = type;
	}

	/**
	 * @brief Gets the light type.
	 * @return The current light type.
	 */
	uint32_t getLightType() const
	{
		return m_light.light_type;
	}

	/**
	 * @brief Sets the spotlight angle (for spot lights).
	 * @param angle The angle in radians.
	 */
	void setSpotAngle(float angle)
	{
		m_light.spot_angle = angle;
	}

	/**
	 * @brief Sets the spotlight softness (for spot lights).
	 * @param softness The softness value.
	 */
	void setSpotSoftness(float softness)
	{
		m_light.spot_softness = softness;
	}

	/**
	 * @brief Gets the underlying light structure.
	 * @return Reference to the light data.
	 */
	engine::rendering::LightStruct &getLightData()
	{
		return m_light;
	}

	/**
	 * @brief Gets the underlying light structure (const).
	 * @return Const reference to the light data.
	 */
	const engine::rendering::LightStruct &getLightData() const
	{
		return m_light;
	}

  private:
	engine::rendering::LightStruct m_light;
};

} // namespace engine::scene::entity
