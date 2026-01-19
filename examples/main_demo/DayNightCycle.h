#pragma once

#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/UpdateNode.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace demo
{

class DayNightCycle : public engine::scene::nodes::UpdateNode
{
  public:
	using Ptr = std::shared_ptr<DayNightCycle>;

	DayNightCycle(
		engine::scene::nodes::LightNode::Ptr sun,
		engine::scene::nodes::LightNode::Ptr moon = nullptr,
		engine::scene::nodes::LightNode::Ptr ambient = nullptr
	) : m_sunLight(sun), m_moonLight(moon), m_ambientLight(ambient) {}

	void update(float deltaTime) override
	{
		if (m_paused)
			return;

		// advance hour
		m_hour += deltaTime / m_cycleDuration * 24.0f;
		if (m_hour >= 24.0f)
		{
			m_hour -= 24.0f;
			m_dayOfYear = (m_dayOfYear % 365) + 1; // advance day
		}

		if (m_sunLight)
			updateSun();
		if (m_moonLight)
			updateMoon();
		if (m_ambientLight)
			updateAmbient();
	}

	void setHour(float h) { m_hour = glm::clamp(h, 0.0f, 24.0f); }
	float getHour() const { return m_hour; }

	void setCycleDuration(float seconds) { m_cycleDuration = seconds; }
	float getCycleDuration() const { return m_cycleDuration; }

	void setPaused(bool paused) { m_paused = paused; }
	bool isPaused() const { return m_paused; }

	void setSunIntensity(float i) { m_sunIntensity = i; }
	void setMoonIntensity(float i) { m_moonIntensity = i; }

	void setLatitude(float lat) { m_latitude = lat; }
	void setDayOfYear(int day) { m_dayOfYear = glm::clamp(day, 1, 365); }

  private:
	float m_hour = 12.0f;
	float m_cycleDuration = 120.0f;
	bool m_paused = false;
	float m_sunIntensity = 1.0f;
	float m_moonIntensity = 1.0f;

	float m_latitude = 48.2f; // degrees
	int m_dayOfYear = 172;	  // summer solstice

	engine::scene::nodes::LightNode::Ptr m_sunLight;
	engine::scene::nodes::LightNode::Ptr m_moonLight;
	engine::scene::nodes::LightNode::Ptr m_ambientLight;

	// --------------------------------------------------------
	// PHYSICALLY BASED SUN/MOON POSITION
	// --------------------------------------------------------
	float solarDeclination() const
	{
		float dayAngle = glm::radians(360.0f / 365.0f * (m_dayOfYear - 81));
		return glm::radians(23.44f) * std::sin(dayAngle);
	}

	glm::vec3 computeSunDirectionPhys(float hour) const
	{
		float phi = glm::radians(m_latitude);
		float delta = solarDeclination();

		float H = glm::radians(15.0f * (hour - 12.0f)); // hour angle

		float sinAlt = std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(H);
		float alt = std::asin(glm::clamp(sinAlt, -1.0f, 1.0f));

		float cosAz = (std::sin(alt) - std::sin(phi) * std::sin(delta)) / (std::cos(phi) * std::cos(alt));
		float az = glm::acos(glm::clamp(cosAz, -1.0f, 1.0f));
		if (H > 0.0f)
			az = 2.0f * glm::pi<float>() - az;

		glm::vec3 dir;
		dir.x = std::sin(az) * std::cos(alt);
		dir.y = std::sin(alt);
		dir.z = std::cos(az) * std::cos(alt);
		return glm::normalize(dir);
	}

	glm::vec3 computeMoonDirectionPhys(float hour) const
	{
		return -computeSunDirectionPhys(hour); // opposite sun
	}

	float sunIntensityFromAltitude(float altitudeRad) const
	{
		float altitudeDeg = glm::degrees(altitudeRad);
		if (altitudeDeg < 0.0f)
			return 0.0f;

		float sinAlt = std::sin(altitudeRad);
		float AM = 1.0f / (sinAlt + 0.50572f * std::pow(altitudeDeg + 6.07995f, -1.6364f));
		float intensity = std::pow(0.7f, std::pow(AM, 0.678f));
		return glm::clamp(intensity, 0.0f, 1.0f);
	}

	float moonIntensityFromAltitude(float altitudeRad) const
	{
		return 0.4f * sunIntensityFromAltitude(altitudeRad);
	}

	float ambientIntensityFromSun(float sunAltitudeRad) const
	{
		const float minAmbient = 0.05f;
		const float maxAmbient = 0.25f;
		float t = glm::clamp(std::sin(sunAltitudeRad), 0.0f, 1.0f);
		return minAmbient + t * (maxAmbient - minAmbient);
	}

	// --------------------------------------------------------
	// APPLY ROTATION
	// --------------------------------------------------------
	void applyRotation(engine::scene::nodes::LightNode::Ptr light, const glm::vec3 &dir)
	{
		glm::vec3 forward = -dir;
		glm::vec3 up = glm::vec3(0, 1, 0);
		if (glm::abs(glm::dot(forward, up)) > 0.99f)
			up = glm::vec3(1, 0, 0);
		glm::vec3 right = glm::normalize(glm::cross(up, forward));
		glm::vec3 actualUp = glm::cross(forward, right);
		light->getTransform()->setLocalRotation(glm::quat_cast(glm::mat3(right, actualUp, forward)));
	}

	// --------------------------------------------------------
	// COLORS
	// --------------------------------------------------------
	glm::vec3 sunColor() const
	{
		if (m_hour < 6.0f || m_hour > 18.0f)
			return glm::vec3(1.0f, 0.9f, 0.8f); // night twilight
		if (m_hour < 8.0f)						// sunrise
		{
			float t = (m_hour - 6.0f) / 2.0f;
			return glm::mix(glm::vec3(1.0f, 0.9f, 0.8f), glm::vec3(1.0f, 0.5f, 0.3f), t);
		}
		if (m_hour < 18.0f)
			return glm::vec3(1.0f, 0.95f, 0.9f);
		return glm::vec3(1.0f, 0.9f, 0.8f);
	}

	glm::vec3 moonColor() const { return glm::vec3(0.7f, 0.8f, 1.0f); }

	// --------------------------------------------------------
	// UPDATE SUN & MOON
	// --------------------------------------------------------
	void updateSun()
	{
		auto dir = computeSunDirectionPhys(m_hour);
		applyRotation(m_sunLight, dir);

		auto &light = m_sunLight->getLight();
		if (!light.isDirectional())
			return;
		auto &dLight = light.asDirectional();

		dLight.color = sunColor();
		dLight.intensity = m_sunIntensity; // constant for shadows
		dLight.castShadows = true;

		light.setData(dLight);
	}

	void updateMoon()
	{
		if (!m_moonLight)
			return;

		auto dir = computeMoonDirectionPhys(m_hour);
		applyRotation(m_moonLight, dir);

		auto &light = m_moonLight->getLight();
		if (!light.isDirectional())
			return;
		auto &dLight = light.asDirectional();

		dLight.color = moonColor();
		dLight.intensity = moonIntensityFromAltitude(glm::asin(dir.y)) * m_moonIntensity;
		dLight.castShadows = dLight.intensity > 0.05f;

		light.setData(dLight);
	}

	void updateAmbient()
	{
		if (!m_ambientLight || !m_sunLight)
			return;

		float sunAlt = glm::asin(computeSunDirectionPhys(m_hour).y);
		float intensity = ambientIntensityFromSun(sunAlt);

		auto &light = m_ambientLight->getLight();
		if (!light.isAmbient())
			return;
		auto &aLight = light.asAmbient();

		aLight.color = glm::vec3(0.5f, 0.5f, 0.8f); // twilight tint
		aLight.intensity = intensity;

		light.setData(aLight);
	}
};

} // namespace demo
