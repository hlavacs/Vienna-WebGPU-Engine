#include "engine/resources/MaterialManager.h"
#include "engine/core/Handle.h"

namespace engine::resources
{
MaterialManager::MaterialManager(std::shared_ptr<TextureManager> textureManager) :
	m_textureManager(std::move(textureManager)) {}

std::optional<MaterialManager::MaterialPtr> MaterialManager::createMaterial(const tinyobj::material_t &objMat, const std::string &textureBasePath)
{
	auto mat = std::make_shared<Material>();

	Material::MaterialProperties props{};
	// Fill properties from tinyobj
	props.diffuse[0] = objMat.diffuse[0];
	props.diffuse[1] = objMat.diffuse[1];
	props.diffuse[2] = objMat.diffuse[2];
	props.specular[0] = objMat.specular[0];
	props.specular[1] = objMat.specular[1];
	props.specular[2] = objMat.specular[2];
	props.transmittance[0] = objMat.transmittance[0];
	props.transmittance[1] = objMat.transmittance[1];
	props.transmittance[2] = objMat.transmittance[2];
	props.emission[0] = objMat.emission[0];
	props.emission[1] = objMat.emission[1];
	props.emission[2] = objMat.emission[2];
	props.shininess = objMat.shininess;
	props.ior = objMat.ior;
	props.opacity = objMat.dissolve;
	props.roughness = objMat.roughness;
	props.metallic = objMat.metallic;

	mat->setProperties(props);
	mat->setName(objMat.name);

	// TODO: support for additional tinyobj::material_t texture slots if needed (e.g. bump_texname, displacement_texname, reflection_texname, etc.)
	using engine::core::unwrapOrHandle;

	if (!objMat.diffuse_texname.empty())
		mat->setAlbedoTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.diffuse_texname)));

	if (!objMat.normal_texname.empty())
		mat->setNormalTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.normal_texname)));

	if (!objMat.metallic_texname.empty())
		mat->setMetallicTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.metallic_texname)));

	if (!objMat.roughness_texname.empty())
		mat->setRoughnessTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.roughness_texname)));

	if (!objMat.ambient_texname.empty())
		mat->setAOTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.ambient_texname)));

	if (!objMat.emissive_texname.empty())
		mat->setEmissiveTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.emissive_texname)));

	if (!objMat.alpha_texname.empty())
		mat->setOpacityTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.alpha_texname)));

	if (!objMat.specular_texname.empty())
		mat->setSpecularTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.specular_texname)));

	// TODO: respect other texture slots like displacement_texname, reflection_texname, etc. if needed
	auto handleOpt = add(mat);
	if (!handleOpt)
		return std::nullopt;
	return mat;
}

std::shared_ptr<TextureManager> MaterialManager::getTextureManager() const
{
	return m_textureManager;
}

} // namespace engine::resources
