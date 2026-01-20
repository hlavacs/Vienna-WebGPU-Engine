#pragma once
#include <variant>
#include <glm/glm.hpp>
#include "engine/rendering/LightUniforms.h"

namespace engine::rendering
{

/**
 * @brief Ambient light data (omnidirectional illumination).
 */
struct AmbientLight
{
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 0.1f;
};

/**
 * @brief Directional light data (parallel rays, like the sun).
 */
struct DirectionalLight
{
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;
	glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // World-space direction
	float range = 100.0f; // Shadow influence area 
	bool castShadows = false;
	float shadowBias = 0.005f;
	float shadowNormalBias = 0.01f;
	uint32_t shadowMapSize = 4096; // Shadow map resolution
	uint32_t shadowPCFKernel = 1; // PCF kernel size (1 = 3x3, 2 = 5x5)
};

/**
 * @brief Point light data (radiates in all directions from a point).
 */
struct PointLight
{
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;
	glm::vec3 position = glm::vec3(0.0f);
	float range = 10.0f; // Effective range for culling
	bool castShadows = false;
	float shadowBias = 0.005f;
	uint32_t shadowMapSize = 1024; // Shadow cube map resolution per face
	uint32_t shadowPCFKernel = 1;
};

/**
 * @brief Spot light data (cone of light from a point).
 */
struct SpotLight
{
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
	float spotAngle = 0.5f; // Inner cone angle (radians)
	float spotSoftness = 0.2f; // Softness of the cone edge
	float range = 10.0f; // Effective range for culling and attenuation
	bool castShadows = false;
	float shadowBias = 0.005f;
	float shadowNormalBias = 0.01f;
	uint32_t shadowMapSize = 2048; // Shadow map resolution
	uint32_t shadowPCFKernel = 1;
};

/**
 * @brief Type-safe light representation using std::variant.
 * 
 * Provides easier access to type-specific properties and shadow information
 * compared to the raw LightStruct. Can extract uniform data via toUniforms().
 */
class Light
{
  public:
	using LightData = std::variant<AmbientLight, DirectionalLight, PointLight, SpotLight>;

	enum class Type : uint32_t
	{
		Ambient = 0,
		Directional = 1,
		Point = 2,
		Spot = 3
	};

	/**
	 * @brief Constructs an ambient light by default.
	 */
	Light() : m_data(AmbientLight{}), m_transform(glm::mat4(1.0f)) {}

	/**
	 * @brief Constructs a light with specific data.
	 * @param data Light-specific data (AmbientLight, DirectionalLight, etc.)
	 */
	explicit Light(const LightData& data) : m_data(data), m_transform(glm::mat4(1.0f)) {}

	/**
	 * @brief Sets the light data.
	 * @param data Light-specific data.
	 */
	void setData(const LightData& data) { m_data = data; }

	/**
	 * @brief Gets the light data.
	 * @return Reference to the variant containing light-specific data.
	 */
	const LightData& getData() const { return m_data; }

	/**
	 * @brief Gets the light data for modification.
	 * @return Reference to the variant containing light-specific data.
	 */
	LightData& getData() { return m_data; }

	/**
	 * @brief Sets the world transform (used for directional/spot directions, point/spot positions).
	 * @param transform World-space transformation matrix.
	 */
	void setTransform(const glm::mat4& transform) { m_transform = transform; }

	/**
	 * @brief Gets the world transform.
	 * @return World-space transformation matrix.
	 */
	const glm::mat4& getTransform() const { return m_transform; }

	/**
	 * @brief Checks if this light type can cast shadows.
	 * @return True if the light has castShadows enabled.
	 */
	bool canCastShadows() const
	{
		return std::visit(
			[](const auto& light) -> bool
			{
				using T = std::decay_t<decltype(light)>;
				if constexpr (std::is_same_v<T, AmbientLight>)
					return false;
				else
					return light.castShadows;
			},
			m_data
		);
	}

	/**
	 * @brief Gets the light type as an integer (0=ambient, 1=directional, 2=point, 3=spot).
	 * @return Light type identifier.
	 */
	Light::Type getLightType() const
	{
		return static_cast<Light::Type>(m_data.index());
	}

	/**
	 * @brief Extracts uniform data for GPU rendering.
	 * @return LightStruct containing data in GPU-friendly format.
	 */
	LightStruct toUniforms() const
	{
		LightStruct uniforms;
		uniforms.transform = m_transform;
		uniforms.light_type = static_cast<uint32_t>(getLightType());
		uniforms.shadowIndex = 0;  // Will be set by RenderCollector based on shadow assignment
		uniforms.shadowCount = 0;  // Will be set by RenderCollector based on light type and shadows

		std::visit(
			[&uniforms](const auto& light)
			{
				using T = std::decay_t<decltype(light)>;

				// Common properties
				uniforms.color = light.color;
				uniforms.intensity = light.intensity;

				// Type-specific properties
				if constexpr (std::is_same_v<T, DirectionalLight>)
				{
					uniforms.range = light.range;
				}
				else if constexpr (std::is_same_v<T, PointLight>)
				{
					uniforms.range = light.range;
				}
				else if constexpr (std::is_same_v<T, SpotLight>)
				{
					uniforms.spot_angle = light.spotAngle;
					uniforms.spot_softness = light.spotSoftness;
					uniforms.range = light.range;
				}
			},
			m_data
		);

		return uniforms;
	}

	/**
	 * @brief Helper to get ambient light data (throws if not ambient).
	 */
	const AmbientLight& asAmbient() const { return std::get<AmbientLight>(m_data); }
	AmbientLight& asAmbient() { return std::get<AmbientLight>(m_data); }
	bool isAmbient() const { return std::holds_alternative<AmbientLight>(m_data); }

	/**
	 * @brief Helper to get directional light data (throws if not directional).
	 */
	const DirectionalLight& asDirectional() const { return std::get<DirectionalLight>(m_data); }
	DirectionalLight& asDirectional() { return std::get<DirectionalLight>(m_data); }
	bool isDirectional() const { return std::holds_alternative<DirectionalLight>(m_data); }

	/**
	 * @brief Helper to get point light data (throws if not point).
	 */
	const PointLight& asPoint() const { return std::get<PointLight>(m_data); }
	PointLight& asPoint() { return std::get<PointLight>(m_data); }
	bool isPoint() const { return std::holds_alternative<PointLight>(m_data); }

	/**
	 * @brief Helper to get spot light data (throws if not spot).
	 */
	const SpotLight& asSpot() const { return std::get<SpotLight>(m_data); }
	SpotLight& asSpot() { return std::get<SpotLight>(m_data); }
	bool isSpot() const { return std::holds_alternative<SpotLight>(m_data); }

  private:

	LightData m_data;
	glm::mat4 m_transform; // World-space transform (for positions/directions)
};

} // namespace engine::rendering
