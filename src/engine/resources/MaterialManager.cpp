#include "engine/resources/MaterialManager.h"
#include "engine/core/Handle.h"

namespace engine::resources
{

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

std::optional<MaterialManager::MaterialPtr> MaterialManager::createMaterial(
    const tinygltf::Material &gltfMat,
    const std::string &textureBasePath)
{
    auto mat = std::make_shared<Material>();

    engine::rendering::PBRProperties props{};
    
    // Base color (diffuse)
    if (gltfMat.values.find("baseColorFactor") != gltfMat.values.end())
    {
        const auto &factor = gltfMat.values.at("baseColorFactor").ColorFactor();
        props.diffuse[0] = factor[0];
        props.diffuse[1] = factor[1];
        props.diffuse[2] = factor[2];
        props.diffuse[3] = factor[3];
    }
    else
    {
        props.diffuse[0] = props.diffuse[1] = props.diffuse[2] = 1.0f;
        props.diffuse[3] = 1.0f;
    }

    // Metallic / roughness
    if (gltfMat.values.find("metallicFactor") != gltfMat.values.end())
        props.metallic = static_cast<float>(gltfMat.values.at("metallicFactor").Factor());
    else
        props.metallic = 0.0f;

    if (gltfMat.values.find("roughnessFactor") != gltfMat.values.end())
        props.roughness = static_cast<float>(gltfMat.values.at("roughnessFactor").Factor());
    else
        props.roughness = 1.0f;

    // Emission
    if (gltfMat.additionalValues.find("emissiveFactor") != gltfMat.additionalValues.end())
    {
        const auto &em = gltfMat.additionalValues.at("emissiveFactor").ColorFactor();
        props.emission[0] = em[0];
        props.emission[1] = em[1];
        props.emission[2] = em[2];
        props.emission[3] = 1.0f;
    }

    mat->setProperties(props);
    mat->setName(gltfMat.name);

    MaterialFeature features = MaterialFeature::None;

    // Set features based on available textures
    if (gltfMat.normalTexture.index >= 0)
        features |= MaterialFeature::UsesNormalMap;
    if (gltfMat.occlusionTexture.index >= 0)
        features |= MaterialFeature::UsesOcclusionMap;
    if (gltfMat.emissiveTexture.index >= 0)
        features |= MaterialFeature::UsesEmissiveMap;
    if (props.diffuse[3] < 1.0f)
        features |= MaterialFeature::AlphaTest;
    if (props.metallic > 0.0f || props.roughness < 1.0f)
        features |= MaterialFeature::UsesMetallicRoughness;
    // DoubleSided could be set from gltfMat.doubleSided

    mat->setFeatureMask(features);

    // Default shader
    mat->setShader(engine::rendering::shader::default::PBR);

    // Load textures using TextureManager
    using engine::core::unwrapOrHandle;

    if (gltfMat.pbrMetallicRoughness.baseColorTexture.index >= 0)
    {
        const auto &tex = gltfMat.pbrMetallicRoughness.baseColorTexture;
        mat->setDiffuseTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + getTextureFileName(tex))));
    }

    if (gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
    {
        const auto &tex = gltfMat.pbrMetallicRoughness.metallicRoughnessTexture;
        mat->setMetallicTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + getTextureFileName(tex))));
        mat->setRoughnessTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + getTextureFileName(tex))));
    }

    if (gltfMat.normalTexture.index >= 0)
    {
        mat->setNormalTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + getTextureFileName(gltfMat.normalTexture))));
    }

    if (gltfMat.occlusionTexture.index >= 0)
    {
        mat->setOcclusionTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + getTextureFileName(gltfMat.occlusionTexture))));
    }

    if (gltfMat.emissiveTexture.index >= 0)
    {
        mat->setEmissiveTexture(unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + getTextureFileName(gltfMat.emissiveTexture))));
    }

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
