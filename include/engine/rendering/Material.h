#pragma once

#include <glm/glm.hpp>
#include "engine/core/Identifiable.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering
{

	struct Material : public engine::core::Identifiable<Material>
	{

		glm::vec3 diffuseColor = glm::vec3(1.0f);
		glm::vec3 specularColor = glm::vec3(1.0f);
		float shininess = 32.0f;

		TextureHandle diffuseTexture;
		TextureHandle specularTexture;
		TextureHandle normalTexture;

		bool hasDiffuseTexture() const { return diffuseTexture.valid(); }
		bool hasSpecularTexture() const { return specularTexture.valid(); }
		bool hasNormalTexture() const { return normalTexture.valid(); }
	};

} // namespace engine::rendering
