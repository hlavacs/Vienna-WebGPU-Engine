#include "engine/resources/MaterialManager.h"
#include "engine/core/Handle.h"

namespace engine::resources
{
MaterialManager::MaterialManager(std::shared_ptr<TextureManager> textureManager) :
	m_textureManager(std::move(textureManager)) {}

std::optional<MaterialManager::MaterialPtr> MaterialManager::createMaterial(const tinyobj::material_t &objMat, const std::string &textureBasePath)
{
	auto mat = std::make_shared<Material>();

	engine::rendering::PBRProperties props{};
	// Fill properties from tinyobj
	// Diffuse RGB + opacity
	props.diffuse[0] = objMat.diffuse[0];
	props.diffuse[1] = objMat.diffuse[1];
	props.diffuse[2] = objMat.diffuse[2];
	props.diffuse[3] = objMat.dissolve; // opacity

	// Specular is not used in a PBR material directly, so we skip it

	// Emission RGB + intensity
	props.emission[0] = objMat.emission[0];
	props.emission[1] = objMat.emission[1];
	props.emission[2] = objMat.emission[2];
	props.emission[3] = 1.0f; // emission intensity

	// Transmittance RGB + intensity
	props.transmittance[0] = objMat.transmittance[0];
	props.transmittance[1] = objMat.transmittance[1];
	props.transmittance[2] = objMat.transmittance[2];
	props.transmittance[3] = 1.0f; // optional factor

	// Ambient RGB + intensity
	props.ambient[0] = objMat.ambient[0];
	props.ambient[1] = objMat.ambient[1];
	props.ambient[2] = objMat.ambient[2];
	props.ambient[3] = 1.0f; // optional factor

	// PBR parameters packed in a struct (scalar values remain separate)
	// props.shininess = objMat.shininess; // Not used in PBR
	props.roughness = objMat.roughness;
	props.metallic = objMat.metallic;
	props.ior = objMat.ior;

	mat->setProperties(props);
	mat->setName(objMat.name);

	MaterialFeature features = MaterialFeature::None;

	if (!objMat.normal_texname.empty())
		features = features | MaterialFeature::UsesNormalMap;
	if (objMat.dissolve < 1.0f || !objMat.alpha_texname.empty())
		features = features | MaterialFeature::AlphaTest;
	// Skinned / instanced flags can be set elsewhere based on mesh usage
	if (objMat.roughness > 0.0f || objMat.metallic > 0.0f)
		features = features | MaterialFeature::UsesMetallicRoughness;
	if (!objMat.ambient_texname.empty())
		features = features | MaterialFeature::UsesOcclusionMap;
	if (!objMat.emissive_texname.empty())
		features = features | MaterialFeature::UsesEmissiveMap;
	if (objMat.transmittance[3] > 0.0f)
		features = features | MaterialFeature::Transparent;
	// DoubleSided could be set from objMat.flags or some convention

	mat->setFeatureMask(features); // <-- store the feature mask on the material

	// TODO: Guess shader type based on available textures
	// For now, we default to PBR
	mat->setShader(engine::rendering::shader::default::PBR);

	// TODO: support for additional tinyobj::material_t texture slots if needed (e.g. bump_texname, displacement_texname, reflection_texname, etc.)
	using engine::core::unwrapOrHandle;

	if (!objMat.ambient_texname.empty())
		mat->setAmbientTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.ambient_texname)));
	if (!objMat.diffuse_texname.empty())
		mat->setDiffuseTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.diffuse_texname)));
	if (!objMat.specular_texname.empty())
		mat->setSpecularTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.specular_texname)));
	if (!objMat.specular_highlight_texname.empty())
		mat->setSpecularHighlightTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.specular_highlight_texname)));
	if (!objMat.bump_texname.empty())
		mat->setBumpTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.bump_texname)));
	if (!objMat.displacement_texname.empty())
		mat->setDisplacementTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.displacement_texname)));
	if (!objMat.reflection_texname.empty())
		mat->setReflectionTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.reflection_texname)));
	if (!objMat.alpha_texname.empty())
		mat->setAlphaTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.alpha_texname)));
	if (!objMat.roughness_texname.empty())
		mat->setRoughnessTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.roughness_texname)));
	if (!objMat.metallic_texname.empty())
		mat->setMetallicTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.metallic_texname)));
	if (!objMat.sheen_texname.empty())
		mat->setSheenTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.sheen_texname)));
	if (!objMat.emissive_texname.empty())
		mat->setEmissiveTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.emissive_texname)));
	if (!objMat.normal_texname.empty())
		mat->setNormalTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.normal_texname)));

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
