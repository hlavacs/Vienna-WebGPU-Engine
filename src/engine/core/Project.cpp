#include "engine/core/Project.h"

#include <fstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "engine/core/EngineSettingsSerializer.h"

namespace engine
{
namespace
{
using json = nlohmann::json;
constexpr int kProjectFormatVersion = 1;
} // namespace

bool ProjectSerializer::save(const Project &project, const std::filesystem::path &projectFilePath)
{
	json root;
	root["version"] = kProjectFormatVersion;
	root["name"] = project.name;
	root["settings"] = settings::toJson(project.settings);
	root["scenes"] = project.scenes;
	root["materials"] = project.materials;
	root["scripts"] = project.scripts;
	root["startupScene"] = project.startupScene;

	std::error_code ec;
	if (!projectFilePath.parent_path().empty())
		std::filesystem::create_directories(projectFilePath.parent_path(), ec);

	std::ofstream out(projectFilePath, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		spdlog::error("ProjectSerializer::save: cannot open '{}' for writing", projectFilePath.string());
		return false;
	}
	out << root.dump(2);
	if (!out)
	{
		spdlog::error("ProjectSerializer::save: write failed for '{}'", projectFilePath.string());
		return false;
	}
	spdlog::info("ProjectSerializer: saved project to '{}'", projectFilePath.string());
	return true;
}

std::optional<Project> ProjectSerializer::load(const std::filesystem::path &projectFilePath)
{
	std::ifstream in(projectFilePath, std::ios::binary);
	if (!in)
	{
		spdlog::error("ProjectSerializer::load: cannot open '{}'", projectFilePath.string());
		return std::nullopt;
	}

	json root;
	try
	{
		in >> root;
	}
	catch (const json::parse_error &e)
	{
		spdlog::error("ProjectSerializer::load: parse error in '{}': {}", projectFilePath.string(), e.what());
		return std::nullopt;
	}

	Project project;
	project.name = root.value("name", std::string("Untitled"));
	if (root.contains("settings"))
		settings::fromJson(root["settings"], project.settings);
	if (root.contains("scenes") && root["scenes"].is_array())
	{
		for (const auto &entry : root["scenes"])
			if (entry.is_string())
				project.scenes.push_back(entry.get<std::string>());
	}
	if (root.contains("materials") && root["materials"].is_array())
	{
		for (const auto &entry : root["materials"])
			if (entry.is_string())
				project.materials.push_back(entry.get<std::string>());
	}
	if (root.contains("scripts") && root["scripts"].is_array())
	{
		for (const auto &entry : root["scripts"])
			if (entry.is_string())
				project.scripts.push_back(entry.get<std::string>());
	}
	project.startupScene = root.value("startupScene", std::string());

	spdlog::info("ProjectSerializer: loaded project from '{}'", projectFilePath.string());
	return project;
}

} // namespace engine
