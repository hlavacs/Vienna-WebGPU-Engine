#include "engine/resources/MaterialManager.h"
#include "engine/core/Handle.h"

namespace engine::resources
{

static engine::rendering::Texture::Handle loadTexture(
	int textureIndex,
	const std::vector<tinygltf::Texture> &textures,
	const std::vector<tinygltf::Image> &images,
	TextureManager &textureManager
)
{
	if (textureIndex < 0)
		return {};

	const auto &gltfTex = textures[textureIndex];
	if (gltfTex.source < 0)
		return {};

	const auto &gltfImg = images[gltfTex.source];

	const bool isHdr =
		gltfImg.pixel_type == TINYGLTF_COMPONENT_TYPE_FLOAT;

	const auto format = ImageFormat::formatFromChannels(
		static_cast<uint32_t>(gltfImg.component),
		isHdr
	);

	engine::resources::Image::Ptr image;

	if (isHdr)
	{
		std::vector<float> pixels(
			reinterpret_cast<const float *>(gltfImg.image.data()),
			reinterpret_cast<const float *>(gltfImg.image.data() + gltfImg.image.size())
		);

		image = std::make_shared<engine::resources::Image>(
			gltfImg.width,
			gltfImg.height,
			format,
			std::move(pixels)
		);
	}
	else
	{
		image = std::make_shared<engine::resources::Image>(
			gltfImg.width,
			gltfImg.height,
			format,
			std::vector<uint8_t>(gltfImg.image.begin(), gltfImg.image.end())
		);
	}

	using engine::core::unwrapOrHandle;

	return unwrapOrHandle(
		textureManager.createImageTexture(
			std::move(image),
			gltfImg.uri.empty()
				? std::optional<std::filesystem::path>{}
				: std::optional<std::filesystem::path>{gltfImg.uri}
		)
	);
}
std::optional<MaterialManager::MaterialPtr>
MaterialManager::createMaterial(const tinyobj::material_t &objMat, const std::string &textureBasePath)
{
	auto mat = std::make_shared<Material>();

	engine::rendering::PBRProperties props{};

	// --- Base color (OBJ = diffuse) ---
	props.diffuse[0] = objMat.diffuse[0];
	props.diffuse[1] = objMat.diffuse[1];
	props.diffuse[2] = objMat.diffuse[2];
	props.diffuse[3] = objMat.dissolve;

	// --- Emission ---
	props.emission[0] = objMat.emission[0];
	props.emission[1] = objMat.emission[1];
	props.emission[2] = objMat.emission[2];
	props.emission[3] = 1.0f;

	// --- PBR scalars ---
	props.roughness = objMat.roughness > 0.0f ? objMat.roughness : 1.0f;
	props.metallic = objMat.metallic;
	props.ior = objMat.ior;

	mat->setProperties(props);
	mat->setName(objMat.name);

	MaterialFeature::Flag features = MaterialFeature::Flag::None;

	// --- Texture presence ---
	if (!objMat.diffuse_texname.empty())
		features |= MaterialFeature::Flag::UsesBaseColorMap;

	if (!objMat.normal_texname.empty())
		features |= MaterialFeature::Flag::UsesNormalMap;

	if (!objMat.ambient_texname.empty())
		features |= MaterialFeature::Flag::UsesOcclusionMap;

	if (!objMat.emissive_texname.empty())
		features |= MaterialFeature::Flag::UsesEmissiveMap;

	if (!objMat.bump_texname.empty() || !objMat.displacement_texname.empty())
		features |= MaterialFeature::Flag::UsesHeightMap;

	// --- Metallic / Roughness (OBJ = separate) ---
	if (!objMat.metallic_texname.empty())
		features |= MaterialFeature::Flag::UsesMetallicMap;

	if (!objMat.roughness_texname.empty())
		features |= MaterialFeature::Flag::UsesRoughnessMap;

	// --- Alpha handling ---
	if (!objMat.alpha_texname.empty())
	{
		if (objMat.dissolve < 1.0f)
			features |= MaterialFeature::Flag::Transparent;
		else
			features |= MaterialFeature::Flag::AlphaTest;
	}
	else if (objMat.dissolve < 1.0f)
	{
		features |= MaterialFeature::Flag::Transparent;
	}

	mat->setFeatureMask(features);

	// --- Shader ---
	mat->setShader(engine::rendering::shader::default ::PBR);

	using engine::core::unwrapOrHandle;

	// --- Textures ---
	if (!objMat.diffuse_texname.empty())
		mat->setDiffuseTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.diffuse_texname))
		);

	if (!objMat.normal_texname.empty())
		mat->setNormalTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.normal_texname))
		);

	if (!objMat.ambient_texname.empty())
		mat->setOcclusionTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.ambient_texname))
		);

	if (!objMat.emissive_texname.empty())
		mat->setEmissiveTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.emissive_texname))
		);

	if (!objMat.metallic_texname.empty())
		mat->setMetallicTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.metallic_texname))
		);

	if (!objMat.roughness_texname.empty())
		mat->setRoughnessTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.roughness_texname))
		);

	if (!objMat.bump_texname.empty())
		mat->setBumpTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.bump_texname))
		);

	if (!objMat.displacement_texname.empty())
		mat->setBumpTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.displacement_texname))
		);

	if (!objMat.alpha_texname.empty())
		mat->setAlphaTexture(
			unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.alpha_texname))
		);

	auto handleOpt = add(mat);
	if (!handleOpt)
		return std::nullopt;

	return mat;
}

std::optional<MaterialManager::MaterialPtr> MaterialManager::createMaterial(
	const tinygltf::Material &gltfMat,
	const std::vector<tinygltf::Texture> &textures,
	const std::vector<tinygltf::Image> &images,
	const std::string &textureBasePath
)
{
	auto mat = std::make_shared<Material>();
	engine::rendering::PBRProperties props{};

	// Base color (diffuse)
	if (gltfMat.pbrMetallicRoughness.baseColorFactor.size() == 4)
	{
		props.diffuse[0] = static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[0]);
		props.diffuse[1] = static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[1]);
		props.diffuse[2] = static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[2]);
		props.diffuse[3] = static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[3]);
	}
	else
	{
		props.diffuse[0] = props.diffuse[1] = props.diffuse[2] = 1.0f;
		props.diffuse[3] = 1.0f;
	}

	// Metallic / Roughness
	props.metallic = static_cast<float>(gltfMat.pbrMetallicRoughness.metallicFactor);
	props.roughness = static_cast<float>(gltfMat.pbrMetallicRoughness.roughnessFactor);
	props.normalTextureScale = gltfMat.normalTexture.scale;
	

	// Emission
	if (gltfMat.emissiveFactor.size() == 3)
	{
		props.emission[0] = static_cast<float>(gltfMat.emissiveFactor[0]);
		props.emission[1] = static_cast<float>(gltfMat.emissiveFactor[1]);
		props.emission[2] = static_cast<float>(gltfMat.emissiveFactor[2]);
		props.emission[3] = 1.0f;
	}

	mat->setProperties(props);
	mat->setName(gltfMat.name);

	MaterialFeature::Flag features = MaterialFeature::Flag::None;

	// Diffuse texture
	if (gltfMat.pbrMetallicRoughness.baseColorTexture.index >= 0)
	{
		mat->setDiffuseTexture(
			loadTexture(gltfMat.pbrMetallicRoughness.baseColorTexture.index, textures, images, *m_textureManager)
		);
	}

	// Metallic-Roughness texture
	if (gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
	{
		auto tex = loadTexture(gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index, textures, images, *m_textureManager);
		mat->setMetallicTexture(tex);
		mat->setRoughnessTexture(tex);
		features |= MaterialFeature::Flag::UsesMetallicRoughnessMap;
	}

	// Normal map
	if (gltfMat.normalTexture.index >= 0)
	{
		mat->setNormalTexture(
			loadTexture(gltfMat.normalTexture.index, textures, images, *m_textureManager)
		);
		features |= MaterialFeature::Flag::UsesNormalMap;
	}

	// Occlusion map
	if (gltfMat.occlusionTexture.index >= 0)
	{
		mat->setOcclusionTexture(
			loadTexture(gltfMat.occlusionTexture.index, textures, images, *m_textureManager)
		);
		features |= MaterialFeature::Flag::UsesOcclusionMap;
	}

	// Emissive map
	if (gltfMat.emissiveTexture.index >= 0)
	{
		mat->setEmissiveTexture(
			loadTexture(gltfMat.emissiveTexture.index, textures, images, *m_textureManager)
		);
		features |= MaterialFeature::Flag::UsesEmissiveMap;
	}

	// Alpha test for transparent materials
	if (props.diffuse[3] < 1.0f)
		features |= MaterialFeature::Flag::AlphaTest;

	// Double-sided
	if (gltfMat.doubleSided)
		features |= MaterialFeature::Flag::DoubleSided;

	mat->setFeatureMask(features);

	// Default shader
	mat->setShader(engine::rendering::shader::default ::PBR);

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
