// Vienna WebGPU Engine - Scene Editor
//
// A dockable, live scene editor: render-to-texture viewport, hierarchy +
// inspector, and JSON save/load to a portable scene folder. Built on the engine
// foundation (SceneSerializer, off-screen camera output, docking ImGui).

#include "engine/EngineMain.h"
// ^ defines SDL_MAIN_HANDLED, must stay first ^

#include <csignal>
#include <cstdlib>
#include <exception>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "imgui.h"

#include "engine/core/PathProvider.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"
#include "engine/scene/nodes/Node.h"

#include "EditorCameraController.h"
#include "EditorLog.h"
#include "EditorState.h"
#include "SceneEditorUI.h"

using PathProvider = engine::core::PathProvider;

namespace
{
// Best-effort crash recovery: on an unhandled exception / std::terminate, dump
// the live scene so work is not lost. Raw pointer (not owning) into the
// SceneManager owned by main; valid for the process lifetime once set. Querying
// the active scene at crash time (rather than caching a Scene*) keeps recovery
// correct after the scene is replaced by opening a project.
engine::scene::SceneManager *g_crashSceneManager = nullptr;

void crashSave()
{
	if (!g_crashSceneManager)
		return;
	try
	{
		if (auto scene = g_crashSceneManager->getActiveScene())
		{
			engine::scene::SceneSerializer::save(*scene, "crash_recovery/scene.json");
			spdlog::warn("Scene editor crashed - recovery scene written to crash_recovery/scene.json");
		}
	}
	catch (...)
	{
	}
}
} // namespace

int main()
{
	spdlog::info("Vienna Scene Editor starting...");

	engine::GameEngineOptions options;
	options.windowWidth = 1600;
	options.windowHeight = 900;
	options.enableVSync = true;

	engine::GameEngine engine;
	engine.initialize(options);

	// Route spdlog output into the editor's Log panel (after init so the engine's
	// loggers already exist).
	auto logStore = std::make_shared<editor::LogStore>();
	editor::installEditorLogSink(logStore);

	auto sceneManager = engine.getSceneManager();
	auto resourceManager = engine.getResourceManager();
	auto imguiManager = engine.getImGuiManager();
	SDL_Window *window = engine.getWindow();

	// Enable docking and scale the UI for high-DPI / 4K displays. ScaleAllSizes
	// scales spacing/padding; FontGlobalScale scales the font. The scale tracks
	// the window's display, so moving between monitors picks up their DPI.
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	const float dpiScale = window ? SDL_GetWindowDisplayScale(window) : 1.0f;
	if (dpiScale > 0.0f)
	{
		ImGui::GetStyle().ScaleAllSizes(dpiScale);
		io.FontGlobalScale = dpiScale;
	}

	editor::EditorState editorState;

	auto scene = sceneManager->createScene("Untitled");
	auto root = scene->getRoot();

	// Editor camera + free-fly controller, driven off-screen into the Viewport
	// panel. Both are flagged non-serializable so they never land in saved scenes.
	editor::setupEditorCamera(*scene, editorState);

	// Default lighting so loaded models are visible.
	auto sun = std::make_shared<engine::scene::nodes::LightNode>();
	sun->setName("Sun");
	engine::rendering::DirectionalLight sunData;
	sunData.intensity = 2.0f;
	sun->setLight(engine::rendering::Light(sunData));
	sun->getTransform().setLocalEulerAngles(glm::vec3(50.0f, -30.0f, 0.0f));
	// Off-origin so the light's gizmo does not overlap the floor (which sits at the
	// origin) and block clicking the floor. Direction comes from rotation, not position.
	sun->getTransform().setLocalPosition(glm::vec3(-4.0f, 6.0f, 4.0f));
	root->addChild(sun->asNode());
	// No default selection: a fresh click in the viewport should pick the floor
	// without a pre-existing gizmo intercepting it.

	auto ambient = std::make_shared<engine::scene::nodes::LightNode>();
	ambient->setName("Ambient");
	engine::rendering::AmbientLight ambientData;
	ambientData.intensity = 0.15f;
	ambient->setLight(engine::rendering::Light(ambientData));
	root->addChild(ambient->asNode());

	auto ui = std::make_shared<editor::SceneEditorUI>(engine, editorState, logStore);
	imguiManager->addFrame([ui]() { ui->render(); });

	g_crashSceneManager = sceneManager.get();
	std::set_terminate([]() { crashSave(); std::abort(); });

	sceneManager->loadScene("Untitled");
	engine.run();

	spdlog::info("Scene Editor closed.");
	return 0;
}
