// Vienna WebGPU Engine - Runtime Player
//
// A minimal standalone player: it loads the project bundled next to the
// executable (assets/project.json), applies its engine settings, and runs the
// project's startup scene. This is what the editor's "Build Game" compiles to
// produce a finished, runnable game - no editor UI.

#include "engine/EngineMain.h"
// ^ defines SDL_MAIN_HANDLED, must stay first ^

#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "engine/GameEngine.h"
#include "engine/core/EngineSettingsSerializer.h"
#include "engine/core/PathProvider.h"
#include "engine/core/Project.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/MaterialSerializer.h"
#include "engine/resources/ResourceManager.h"
#include "engine/resources/TextureManager.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/SceneSerializer.h"

int main()
{
	// Resolve paths relative to the executable, then read the bundled project.
	engine::core::PathProvider::initialize();
	const auto projectFile = engine::core::PathProvider::getAssets("project.json");
	auto project = engine::ProjectSerializer::load(projectFile);
	if (!project)
		spdlog::warn("RuntimePlayer: no project at '{}' - running with default settings", projectFile.string());

	engine::GameEngineOptions options = project ? project->settings : engine::GameEngineOptions{};

	engine::GameEngine engine;
	engine.initialize(options);

	auto sceneManager = engine.getSceneManager();

	// Load the project's material library before any scene, so model material
	// references (stored by name) resolve against the MaterialManager.
	if (project)
	{
		if (auto resources = engine.getResourceManager(); resources && resources->m_materialManager && resources->m_textureManager)
		{
			for (const auto &token : project->materials)
			{
				const auto file = engine::core::PathProvider::resolveEnginePath(token);
				engine::resources::MaterialSerializer::load(file, *resources->m_materialManager, *resources->m_textureManager);
			}
		}
	}

	// Startup scene: the explicit one, else the first listed.
	std::string startup;
	if (project)
		startup = project->startupScene.empty()
			? (project->scenes.empty() ? std::string() : project->scenes.front())
			: project->startupScene;

	if (startup.empty())
	{
		spdlog::warn("RuntimePlayer: project has no startup scene");
	}
	else
	{
		const auto scenePath = engine::core::PathProvider::resolveEnginePath(startup);
		if (auto scene = engine::scene::SceneSerializer::load(scenePath))
		{
			// Effective settings = project defaults + this scene's override.
			if (project && !scene->getSettingsOverride().empty())
			{
				engine::GameEngineOptions effective = project->settings;
				try
				{
					engine::settings::fromJson(nlohmann::json::parse(scene->getSettingsOverride()), effective);
				}
				catch (...)
				{
				}
				engine.setOptions(effective);
			}
			sceneManager->registerScene("main", scene);
			sceneManager->loadScene("main");
		}
		else
		{
			spdlog::error("RuntimePlayer: failed to load startup scene '{}'", scenePath.string());
		}
	}

	engine.run();
	return 0;
}
