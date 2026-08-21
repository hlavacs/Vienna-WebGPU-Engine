#include "engine/resources/MaterialSerializer.h"

#include <fstream>
#include <typeindex>
#include <unordered_map>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/ColorSpace.h"
#include "engine/rendering/Material.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/TextureManager.h"

namespace engine::resources
{
namespace
{
using json = nlohmann::json;
using engine::rendering::ColorSpace;
using engine::rendering::Material;
using engine::rendering::PBRProperties;
using engine::rendering::UnlitProperties;
using TextureHandle = engine::rendering::Texture::Handle;

constexpr int kMaterialFormatVersion = 1;

json vec4ToJson(const glm::vec4 &v) { return json::array({v.x, v.y, v.z, v.w}); }

glm::vec4 jsonToVec4(const json &j, const glm::vec4 &fallback)
{
	if (!j.is_array() || j.size() < 4)
		return fallback;
	return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

const char *colorSpaceName(ColorSpace cs) { return cs == ColorSpace::Linear ? "Linear" : "sRGB"; }
ColorSpace colorSpaceFromName(const std::string &s) { return s == "Linear" ? ColorSpace::Linear : ColorSpace::sRGB; }
} // namespace

bool MaterialSerializer::save(const Material &material, const std::filesystem::path &file)
{
	json j;
	j["version"] = kMaterialFormatVersion;
	j["name"] = material.getName().value_or(file.stem().string());
	j["shader"] = material.getShader();

	if (material.getPropertiesType() == std::type_index(typeid(PBRProperties)))
	{
		j["type"] = "PBR";
		const auto &p = material.getProperties<PBRProperties>();
		json &jp = j["properties"];
		jp["diffuse"] = vec4ToJson(p.diffuse);
		jp["emission"] = vec4ToJson(p.emission);
		jp["transmittance"] = vec4ToJson(p.transmittance);
		jp["ambient"] = vec4ToJson(p.ambient);
		jp["roughness"] = p.roughness;
		jp["metallic"] = p.metallic;
		jp["ior"] = p.ior;
		jp["normalTextureScale"] = p.normalTextureScale;
		jp["alphaMode"] = p.alphaMode;
		jp["alphaCutoff"] = p.alphaCutoff;
	}
	else if (material.getPropertiesType() == std::type_index(typeid(UnlitProperties)))
	{
		j["type"] = "Unlit";
		j["properties"]["color"] = vec4ToJson(material.getProperties<UnlitProperties>().color);
	}
	else
	{
		j["type"] = "Unknown";
	}

	json textures = json::object();
	for (const auto &[slot, texSlot] : material.getTextureSlots())
	{
		if (!texSlot.handle.valid())
			continue;
		auto texOpt = texSlot.handle.get();
		if (!texOpt || !*texOpt)
			continue;
		const auto &path = (*texOpt)->getFilePath();
		if (path.empty())
			continue;
		json entry;
		entry["path"] = engine::core::PathProvider::toEnginePath(path);
		entry["colorSpace"] = colorSpaceName(texSlot.colorSpace);
		textures[slot] = entry;
	}
	j["textures"] = textures;

	std::error_code ec;
	if (!file.parent_path().empty())
		std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		spdlog::error("MaterialSerializer::save: cannot open '{}'", file.string());
		return false;
	}
	out << j.dump(2);
	return static_cast<bool>(out);
}

std::shared_ptr<Material> MaterialSerializer::load(
	const std::filesystem::path &file, MaterialManager &materials, TextureManager &textures)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
	{
		spdlog::error("MaterialSerializer::load: cannot open '{}'", file.string());
		return nullptr;
	}
	json j;
	try
	{
		in >> j;
	}
	catch (const json::parse_error &e)
	{
		spdlog::error("MaterialSerializer::load: parse error in '{}': {}", file.string(), e.what());
		return nullptr;
	}

	const std::string name = j.value("name", file.stem().string());
	const std::string shader = j.value("shader", std::string("PBR_Lit_Shader"));
	const std::string type = j.value("type", std::string("PBR"));

	// Resolve texture references first so the material features reflect them.
	std::unordered_map<std::string, TextureHandle> textureHandles;
	std::unordered_map<std::string, ColorSpace> textureColorSpaces;
	if (j.contains("textures") && j["textures"].is_object())
	{
		for (const auto &[slot, entry] : j["textures"].items())
		{
			const std::string token = entry.value("path", std::string());
			if (token.empty())
				continue;
			const std::filesystem::path resolved = engine::core::PathProvider::resolveEnginePath(token);
			if (auto tex = textures.createTextureFromFile(resolved.string()))
			{
				textureHandles[slot] = (*tex)->getHandle();
				textureColorSpaces[slot] = colorSpaceFromName(entry.value("colorSpace", std::string("sRGB")));
			}
			else
			{
				spdlog::warn("MaterialSerializer: texture '{}' for slot '{}' not found", token, slot);
			}
		}
	}

	std::shared_ptr<Material> material;
	if (type == "Unlit")
	{
		UnlitProperties props;
		if (j.contains("properties"))
			props.color = jsonToVec4(j["properties"].value("color", json()), props.color);
		if (auto created = materials.createMaterial<UnlitProperties>(name, props, shader, textureHandles))
			material = *created;
	}
	else
	{
		PBRProperties props;
		if (j.contains("properties"))
		{
			const json &jp = j["properties"];
			props.diffuse = jsonToVec4(jp.value("diffuse", json()), props.diffuse);
			props.emission = jsonToVec4(jp.value("emission", json()), props.emission);
			props.transmittance = jsonToVec4(jp.value("transmittance", json()), props.transmittance);
			props.ambient = jsonToVec4(jp.value("ambient", json()), props.ambient);
			props.roughness = jp.value("roughness", props.roughness);
			props.metallic = jp.value("metallic", props.metallic);
			props.ior = jp.value("ior", props.ior);
			props.normalTextureScale = jp.value("normalTextureScale", props.normalTextureScale);
			props.alphaMode = jp.value("alphaMode", props.alphaMode);
			props.alphaCutoff = jp.value("alphaCutoff", props.alphaCutoff);
		}
		if (auto created = materials.createPBRMaterial(name, props, textureHandles))
		{
			(*created)->setShader(shader);
			material = *created;
		}
	}

	if (material)
	{
		material->setName(name); // ensure getByName() resolves scene material references
		// Re-apply the stored colour space per slot (the create* path defaults it).
		for (const auto &[slot, handle] : textureHandles)
			material->setTexture(slot, handle, textureColorSpaces[slot]);
	}
	return material;
}

} // namespace engine::resources
