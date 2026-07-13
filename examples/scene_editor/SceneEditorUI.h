#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "engine/GameEngine.h" // GameEngine + GameEngineOptions (settings panel edits a copy)
#include "engine/core/Project.h"

#include "EditorLog.h"
#include "EditorState.h"

namespace engine::scene
{
class Scene;
}
namespace engine::scene::nodes
{
class Node;
class ModelRenderNode;
}
namespace engine::rendering
{
struct Material;
}

namespace editor
{

/**
 * @brief A configurable key binding: a key plus the modifier state it requires.
 * `key` holds an ImGuiKey value (stored as int so this header needs no ImGui
 * include). A binding fires only when its modifiers match exactly.
 */
struct Shortcut
{
	int key = 0; // ImGuiKey_None
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
};

/**
 * @brief Editor actions bound to configurable shortcuts. Defaults follow Unity's
 * scene-view conventions (Q/W/E/R tools). Keep in sync with the name + default
 * table (kActionDefs) in the .cpp.
 */
enum class EditorAction
{
	ViewTool,	   // hide the transform gizmo (select / navigate only)
	Move,		   // translate gizmo
	Rotate,		   // rotate gizmo
	Scale,		   // scale gizmo
	ToggleSpace,   // world <-> local gizmo space
	FocusSelected, // frame the camera on the selection
	Duplicate,	   // clone the selected node
	DeleteSelected,
	NewScene,
	OpenScene,
	SaveProject,
	Count
};

/**
 * @brief The dockable scene-editor UI: menu bar, dockspace layout, and the
 * Viewport / Hierarchy / Inspector panels.
 *
 * Registered as a single ImGui frame callback. The Viewport panel displays the
 * editor camera's off-screen render via ImGui::Image and reports its pixel size
 * back through EditorState so the camera renders at the right resolution.
 */
class SceneEditorUI
{
  public:
	SceneEditorUI(engine::GameEngine &engine, EditorState &state, std::shared_ptr<LogStore> logStore = nullptr);
	~SceneEditorUI();

	/// Build the whole editor UI for this frame.
	void render();

  private:
	/// One-time startup: load recent lists and auto-open the most recent project;
	/// if none opens, raise the welcome screen. Called on the first rendered frame.
	void bootstrap();
	/// The "no project" welcome screen (New / Open / Recent). Shown until a project
	/// is loaded; while it is up the normal panels are not drawn.
	void drawWelcome();
	void drawMenuBar();
	void buildDefaultLayout(unsigned int dockspaceId);
	void drawViewport();
	bool drawGizmoToolbar(float originX, float originY); // returns true if a toolbar button is hovered
	void processShortcuts(); // dispatch all configurable key bindings for this frame
	void loadShortcuts();	 // restore bindings from config (falls back to defaults)
	void saveShortcuts();	 // persist bindings to config
	void focusSelected();	 // frame the editor camera on the selected node
	void drawShortcutsPopup();
	void drawHierarchy();
	void drawInspector();
	void drawMaterialEditor(engine::scene::nodes::ModelRenderNode &modelNode);
	void drawSettings();
	void drawShaders();
	void drawMaterials();
	void drawAssets();
	void drawLog();
	void drawFileGrid(const std::filesystem::path &root, bool editable); // single-pane explorer
	void assetNavigateTo(const std::filesystem::path &target);           // push history + go there
	void pasteClipboard(const std::filesystem::path &destFolder);        // paste cut/copied entries
	/// Render an absolute path as its portable engine path with a right-click
	/// "copy as engine / relative / absolute" menu. @p id disambiguates the popup.
	void pathLabel(const std::filesystem::path &absolutePath, const char *id);
	/// Resolve a picked file to an engine path for use in the scene. If it already
	/// lives under the asset/resource roots, @p apply runs immediately with its
	/// token. Otherwise a prompt offers to import (copy) it into a chosen assets/
	/// subfolder - @p apply then runs with the new asset:// token (or the absolute
	/// path if the user keeps it external). Keeps scenes portable by default.
	void requestImport(const std::filesystem::path &source, const std::string &defaultSubdir, std::function<void(const std::string &)> apply);
	/// Assign a texture file to a material slot, importing it into the project first
	/// if it lives outside the asset/resource roots (see requestImport).
	void requestTextureAssign(const std::shared_ptr<engine::rendering::Material> &material, const char *slot, const std::string &pathOrToken);
	void drawNodeTree(const std::shared_ptr<engine::scene::nodes::Node> &node);
	void drawAddNodeMenu(const std::shared_ptr<engine::scene::nodes::Node> &parent);
	void applyDeferredHierarchyOps();
	void saveScene(const std::filesystem::path &path);
	void newScene();
	void openScene(const std::filesystem::path &path);
	void newProject();
	void openProject(const std::filesystem::path &path);
	/// Import a scene authored in another project into the current one, copying its
	/// assets/materials/scripts with content-hash dedup and rename-on-conflict, then
	/// open it. Shows a summary popup (and flags any unresolved script type clashes).
	void importSceneFromProject(const std::filesystem::path &sourceScene);
	void saveProject();
	/// Compile the project into a standalone game (background thread; output goes
	/// to the Log). Saves the project first, builds the runtime player in Release,
	/// then stages the engine resources + project data next to the produced exe.
	void buildGame();
	void runBuild(std::filesystem::path projectFile); // thread body for buildGame
	/// Apply the effective settings for @p scene (project defaults + the scene's
	/// optional override) to the engine, and sync the Settings panel buffer.
	void applySceneSettings(engine::scene::Scene &scene);
	/// If the scene has unsaved changes, ask (native dialog) whether to discard.
	/// Returns true if it is safe to proceed (no changes, or user confirmed).
	bool confirmDiscardIfDirty();
	/// Open a .json by content: a scene (has "root") loads into the project, a
	/// project (has "scenes"/"settings") replaces the workspace. One entry point so
	/// the user never has to know which kind of file it is up front.
	void openPath(const std::filesystem::path &path);
	void addRecentScene(const std::filesystem::path &path);
	void addRecentProject(const std::filesystem::path &path);
	void loadRecentScenes();
	void saveRecentScenes();
	void loadRecentProjects();
	void saveRecentProjects();

	engine::GameEngine &m_engine;
	EditorState &m_state;
	bool m_bootstrapped = false; // first-frame startup (auto-open last project) has run
	bool m_showWelcome = false;  // no project loaded: show the welcome screen, hide panels
	bool m_layoutInitialized = false;
	bool m_forceLayoutRebuild = false; // set by "Reset Layout" to rebuild over a saved imgui.ini

	// Panel visibility (toggled from the Window menu; also set by each window's
	// close button via Begin(&flag)).
	bool m_showViewport = true;
	bool m_showHierarchy = true;
	bool m_showInspector = true;
	bool m_showAssets = true;
	bool m_showLog = true;
	bool m_showSettings = false;
	bool m_showShaders = true;
	bool m_showMaterials = true;

	// Editable copy of the engine settings (Apply pushes them to the engine).
	engine::GameEngineOptions m_settings;
	// Current project (settings + scenes + startup) and where it is saved.
	engine::Project m_project;
	std::filesystem::path m_projectPath;
	bool m_overrideSceneSettings = false; // edit/store the active scene's override vs the project

	// Background "Build Game" job (compiles the runtime player + stages data).
	std::atomic<bool> m_building{false};
	std::thread m_buildThread;
	bool m_openSaveAsPopup = false;
	std::string m_saveAsPath;
	std::string m_importSummary;		 // last cross-project import report (for the popup)
	bool m_openImportReportPopup = false;
	bool m_importHadClashes = false;	 // last import left unresolved script type clashes
	std::vector<std::string> m_recentScenes;   // most-recent-first, persisted to configs
	std::vector<std::string> m_recentProjects; // most-recent-first, persisted to configs
	std::string m_assetStatus;
	std::unordered_map<std::string, std::string> m_texturePathBuffers; // per-slot path input buffers
	std::string m_newMaterialShader = "PBR_Lit_Shader";
	std::string m_newMaterialName = "Material";                    // Materials panel: new-material name
	std::weak_ptr<engine::rendering::Material> m_selectedMaterial; // Materials panel: selected material
	std::string m_materialFilter;                                  // Materials panel: list filter text
	std::string m_cameraEnvPathBuffer;                             // Inspector: camera environment-map path input
	std::string m_newShaderName;
	int m_newShaderType = 0; // 0 = material, 1 = post-process
	std::string m_shaderStatus;

	int m_externalEditor = 0; // 0 = OS default app, 1 = VS Code

	// Asset-browser directory cache: listings are read once and reused until the
	// generation changes (Reload button or the auto-refresh poll), so the tree
	// does not hit the disk every frame.
	struct DirListing
	{
		std::vector<std::filesystem::path> dirs;
		std::vector<std::filesystem::path> files;
		uint64_t generation = 0;
	};
	std::unordered_map<std::string, DirListing> m_dirCache;
	uint64_t m_browserGeneration = 1;
	bool m_browserAutoRefresh = true;
	double m_browserLastRefresh = 0.0;

	// Single-pane asset explorer (one of the two roots at a time).
	int m_assetGridRoot = 1;                 // 0 = Resources (read-only), 1 = Assets (editable)
	std::filesystem::path m_assetBrowsePath; // folder currently shown
	float m_assetIconSize = 72.0f;
	std::vector<std::filesystem::path> m_navBack;        // Back history (most-recent last)
	std::vector<std::filesystem::path> m_navForward;     // Forward history (most-recent last)
	std::vector<std::filesystem::path> m_assetSelection; // currently selected entries
	std::filesystem::path m_selectionAnchor;             // shift-range selection anchor
	std::vector<std::filesystem::path> m_clipboard;      // cut/copied entries
	bool m_clipboardCut = false;                         // true = move (cut), false = copy

	// Deferred "import external file into the project" prompt: copies a file picked
	// from outside the project into a chosen assets/ subfolder, then runs the apply
	// callback with the resulting engine path (an asset:// token, or the absolute
	// path if the user chooses to keep it external).
	std::filesystem::path m_pendingImportSource;
	std::string m_pendingImportSubdir;
	std::function<void(const std::string &)> m_pendingImportApply;
	bool m_openImportPopup = false;

	// Deferred delete confirmation for asset files/folders (supports multi-select).
	std::vector<std::filesystem::path> m_pendingDeletes;
	bool m_openDeletePopup = false;

	// Deferred rename for an asset file/folder.
	std::filesystem::path m_pendingRenameSource;
	std::string m_renameBuffer;
	bool m_openRenamePopup = false;

	// Log panel.
	std::shared_ptr<LogStore> m_logStore;
	std::vector<LogEntry> m_logCache;  // per-frame snapshot of m_logStore
	int m_logMinLevel = 2;			   // minimum spdlog level to show (2 = info)
	std::string m_logFilter;
	bool m_logAutoScroll = true;
	uint64_t m_logSelectedSeq = 0;

	// Deferred hierarchy edits. Applied after the tree is drawn so we never
	// mutate a node's child list while iterating it.
	std::weak_ptr<engine::scene::nodes::Node> m_nodeToDelete;
	std::weak_ptr<engine::scene::nodes::Node> m_reparentChild;
	std::weak_ptr<engine::scene::nodes::Node> m_reparentTarget;
	std::weak_ptr<engine::scene::nodes::Node> m_addChildParent;
	std::string m_addChildType; // registry type name to create; empty = none
	std::weak_ptr<engine::scene::nodes::Node> m_addModelParent;
	std::string m_addModelPath; // engine path of a model to add as a ModelRenderNode; empty = none
	std::weak_ptr<engine::scene::nodes::Node> m_dragNode;
	std::weak_ptr<engine::scene::nodes::Node> m_replaceWithPlain; // pending: swap placeholder -> plain Node

	// ImGuizmo state (stored as int to keep ImGuizmo out of this header; set to
	// ImGuizmo::OPERATION / MODE values in the constructor).
	int m_gizmoOperation = 0;
	int m_gizmoMode = 0;
	bool m_gizmoActive = true; // false = View tool (gizmo hidden; select / navigate only)

	// Gizmo snapping (grid for move, degrees for rotate, step for scale).
	bool m_gizmoSnap = false;
	float m_snapTranslate = 1.0f;
	float m_snapRotate = 15.0f;
	float m_snapScale = 0.1f;

	// Configurable key bindings, indexed by EditorAction. Defaults (Unity-like) are
	// assigned in the constructor, then overridden by any saved config.
	std::array<Shortcut, static_cast<size_t>(EditorAction::Count)> m_shortcuts;
	int m_rebindAction = -1;	   // index of the binding awaiting a new key (-1 = none)
	bool m_openShortcutsPopup = false;
};

} // namespace editor
