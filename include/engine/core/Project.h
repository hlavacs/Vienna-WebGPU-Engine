#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "engine/GameEngine.h" // GameEngineOptions

namespace engine
{

/**
 * @brief A project: the engine settings plus the set of scenes that make up a
 * game. Stored as project.json next to a portable assets/ folder.
 *
 * Scene references are portable engine path tokens (asset:// / resource://).
 * The effective settings for a scene are the project's @ref settings merged
 * with that scene's optional per-scene override (see Scene::getSettingsOverride).
 */
struct Project
{
	std::string name = "Untitled";
	GameEngineOptions settings;			///< project-wide default engine settings
	std::vector<std::string> scenes;	///< scene references (engine path tokens)
	std::vector<std::string> materials; ///< material library refs (.mat.json engine tokens)
	std::vector<std::string> scripts;	///< custom C++ script files (project-relative), compiled by Build Game
	std::string startupScene;			///< scene loaded first (empty = first entry)
};

/**
 * @brief JSON load/save for a Project (project.json).
 */
class ProjectSerializer
{
  public:
	static bool save(const Project &project, const std::filesystem::path &projectFilePath);
	static std::optional<Project> load(const std::filesystem::path &projectFilePath);
};

} // namespace engine
