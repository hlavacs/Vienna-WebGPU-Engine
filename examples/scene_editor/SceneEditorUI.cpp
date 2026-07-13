#include "SceneEditorUI.h"

#include <spdlog/spdlog.h>

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ImGuizmo.h"
#include "tinyfiledialogs.h"

#include "EditorCameraController.h"
#include "SceneImporter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "engine/GameEngine.h"
#include "engine/core/EngineSettingsSerializer.h"
#include "engine/core/PathProvider.h"
#include "engine/core/Project.h"
#include "engine/math/AABB.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/ShaderType.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/MaterialSerializer.h"
#include "engine/resources/ModelManager.h"
#include "engine/resources/ResourceManager.h"
#include "engine/resources/TextureManager.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/Renderer.h"
#include "engine/scene/NodeTypeRegistry.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"
#include "engine/scene/nodes/Node.h"
#include "engine/reflection/Reflector.h"
#include "engine/scene/nodes/PlaceholderNode.h"
#include "engine/scene/nodes/SpatialNode.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace editor
{
namespace
{
using namespace engine::scene;

// A wgpu::TextureView must reach ImGui as the raw WGPUTextureView pointer cast
// to ImTextureID; the webgpu.hpp wrapper's implicit conversions would otherwise
// pick the wrong (bool) overload. Mirrors the helper in the main demo UI.
ImTextureID toImTextureID(const wgpu::TextureView &view)
{
	return reinterpret_cast<ImTextureID>(static_cast<WGPUTextureView>(view));
}

std::string nodeLabel(const engine::scene::nodes::Node &node)
{
	if (auto name = node.getName())
		return *name;
	return "Node " + std::to_string(node.getId());
}

// Registered type name for display; a placeholder reports its missing type so
// the user can see what the scene expected.
std::string nodeTypeName(const engine::scene::nodes::Node &node)
{
	if (const auto *placeholder = dynamic_cast<const engine::scene::nodes::PlaceholderNode *>(&node))
		return "(Missing) " + placeholder->getMissingType();
	const std::string name = engine::scene::NodeTypeRegistry::instance().typeNameOf(node);
	return name.empty() ? "Node" : name;
}

bool rayIntersectsAABB(const glm::vec3 &origin, const glm::vec3 &dir, const engine::math::AABB &box, float &tHit)
{
	const glm::vec3 invDir = 1.0f / dir; // axis-parallel rays yield +/-inf, handled by min/max
	const glm::vec3 t0 = (box.min - origin) * invDir;
	const glm::vec3 t1 = (box.max - origin) * invDir;
	const glm::vec3 tmin = glm::min(t0, t1);
	const glm::vec3 tmax = glm::max(t0, t1);
	const float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
	const float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
	if (tFar < 0.0f || tNear > tFar)
		return false;
	tHit = tNear >= 0.0f ? tNear : tFar;
	return true;
}

// World-space AABB for picking: a model's mesh bounds transformed to world, or
// a small handle box at the origin of any other node so lights / cameras /
// empties stay clickable.
engine::math::AABB nodeWorldAABB(const std::shared_ptr<engine::scene::nodes::Node> &node, const glm::mat4 &world)
{
	using engine::math::AABB;
	if (auto model = std::dynamic_pointer_cast<engine::scene::nodes::ModelRenderNode>(node))
	{
		if (auto modelRes = model->getModel().get(); modelRes && (*modelRes)->hasMesh())
		{
			if (auto mesh = (*modelRes)->getMesh().get())
			{
				const AABB local = (*mesh)->getBoundingBox();
				const glm::vec3 first = glm::vec3(world * glm::vec4(local.getCorner(0), 1.0f));
				AABB out(first, first);
				for (int i = 1; i < 8; ++i)
					out.expandToFit(glm::vec3(world * glm::vec4(local.getCorner(i), 1.0f)));
				return out;
			}
		}
	}
	const glm::vec3 center = glm::vec3(world[3]);
	return AABB(center - glm::vec3(0.25f), center + glm::vec3(0.25f));
}

void pickRecursive(
	const std::shared_ptr<engine::scene::nodes::Node> &node,
	const glm::vec3 &origin,
	const glm::vec3 &dir,
	uint64_t skipId,
	float &bestT,
	std::shared_ptr<engine::scene::nodes::Node> &best)
{
	if (!node)
		return;
	if (node->getId() != skipId)
	{
		if (auto spatial = node->asSpatialNode())
		{
			const glm::mat4 world = spatial->getTransform().getWorldMatrix();
			float t = 0.0f;
			if (rayIntersectsAABB(origin, dir, nodeWorldAABB(node, world), t) && t < bestT)
			{
				bestT = t;
				best = node;
			}
		}
	}
	for (const auto &child : node->getChildren())
		pickRecursive(child, origin, dir, skipId, bestT, best);
}

// --- Configurable shortcut helpers ---

std::string shortcutText(const Shortcut &s)
{
	if (s.key == ImGuiKey_None)
		return "unbound";
	std::string text;
	if (s.ctrl)
		text += "Ctrl+";
	if (s.shift)
		text += "Shift+";
	if (s.alt)
		text += "Alt+";
	text += ImGui::GetKeyName(static_cast<ImGuiKey>(s.key));
	return text;
}

bool shortcutPressed(const Shortcut &s)
{
	if (s.key == ImGuiKey_None)
		return false;
	const ImGuiIO &io = ImGui::GetIO();
	if (s.ctrl != io.KeyCtrl || s.shift != io.KeyShift || s.alt != io.KeyAlt)
		return false;
	return ImGui::IsKeyPressed(static_cast<ImGuiKey>(s.key), false);
}

// A modifier key (should not be captured as the bound key during a rebind).
bool isModifierKey(ImGuiKey key)
{
	switch (key)
	{
	case ImGuiKey_LeftCtrl:  case ImGuiKey_RightCtrl:
	case ImGuiKey_LeftShift: case ImGuiKey_RightShift:
	case ImGuiKey_LeftAlt:   case ImGuiKey_RightAlt:
	case ImGuiKey_LeftSuper: case ImGuiKey_RightSuper:
	case ImGuiKey_ReservedForModCtrl: case ImGuiKey_ReservedForModShift:
	case ImGuiKey_ReservedForModAlt:  case ImGuiKey_ReservedForModSuper:
		return true;
	default:
		return false;
	}
}

// Display name + default binding per EditorAction; order MUST match the enum.
// Defaults follow Unity's scene-view conventions (Q/W/E/R tools, no modifier).
struct ActionDef
{
	const char *name;
	Shortcut def;
};
const ActionDef kActionDefs[] = {
	/* ViewTool       */ {"View / Pan", {ImGuiKey_Q, false, false, false}},
	/* Move           */ {"Move Tool", {ImGuiKey_W, false, false, false}},
	/* Rotate         */ {"Rotate Tool", {ImGuiKey_E, false, false, false}},
	/* Scale          */ {"Scale Tool", {ImGuiKey_R, false, false, false}},
	/* ToggleSpace    */ {"Toggle World/Local", {ImGuiKey_X, false, false, false}},
	/* FocusSelected  */ {"Frame Selected", {ImGuiKey_F, false, false, false}},
	/* Duplicate      */ {"Duplicate", {ImGuiKey_D, true, false, false}},
	/* DeleteSelected */ {"Delete Selected", {ImGuiKey_Delete, false, false, false}},
	/* NewScene       */ {"New Scene", {ImGuiKey_N, true, false, false}},
	/* OpenScene      */ {"Open Scene", {ImGuiKey_O, true, false, false}},
	/* SaveProject    */ {"Save Project", {ImGuiKey_S, true, false, false}},
};
static_assert(sizeof(kActionDefs) / sizeof(kActionDefs[0]) == static_cast<size_t>(EditorAction::Count),
	"kActionDefs must have exactly one entry per EditorAction");

const Shortcut &binding(const std::array<Shortcut, static_cast<size_t>(EditorAction::Count)> &shortcuts, EditorAction a)
{
	return shortcuts[static_cast<size_t>(a)];
}

// --- Procedural vector icons (crisp at any DPI; no asset pipeline) ---

void drawMoveIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	const float h = r * 0.5f; // arrowhead size
	dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.5f);
	dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 1.5f);
	dl->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x + r - h, c.y - h * 0.6f), ImVec2(c.x + r - h, c.y + h * 0.6f), col);
	dl->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x - r + h, c.y - h * 0.6f), ImVec2(c.x - r + h, c.y + h * 0.6f), col);
	dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x - h * 0.6f, c.y - r + h), ImVec2(c.x + h * 0.6f, c.y - r + h), col);
	dl->AddTriangleFilled(ImVec2(c.x, c.y + r), ImVec2(c.x - h * 0.6f, c.y + r - h), ImVec2(c.x + h * 0.6f, c.y + r - h), col);
}

void drawViewIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	// A pointer/cursor: an arrowhead toward the upper-left with a short tail.
	const ImVec2 tip(c.x - r * 0.75f, c.y - r * 0.75f);
	dl->AddTriangleFilled(tip, ImVec2(c.x - r * 0.75f, c.y + r * 0.35f), ImVec2(c.x + r * 0.35f, c.y - r * 0.75f), col);
	dl->AddLine(ImVec2(c.x - r * 0.1f, c.y - r * 0.1f), ImVec2(c.x + r * 0.6f, c.y + r * 0.6f), col, 2.0f);
}

void drawRotateIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	dl->PathArcTo(c, r, 0.6f, IM_PI * 1.85f, 24);
	dl->PathStroke(col, ImDrawFlags_None, 1.5f);
	const ImVec2 end(c.x + cosf(0.6f) * r, c.y + sinf(0.6f) * r);
	const float h = r * 0.55f;
	dl->AddTriangleFilled(end, ImVec2(end.x - h, end.y - h * 0.3f), ImVec2(end.x + h * 0.2f, end.y - h), col);
}

void drawScaleIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	const ImVec2 a(c.x - r, c.y + r), b(c.x + r, c.y - r);
	dl->AddLine(a, b, col, 1.5f);
	const float s = r * 0.4f;
	dl->AddRectFilled(ImVec2(a.x - s * 0.5f, a.y - s * 0.5f), ImVec2(a.x + s * 0.5f, a.y + s * 0.5f), col);
	dl->AddRectFilled(ImVec2(b.x - s * 0.5f, b.y - s * 0.5f), ImVec2(b.x + s * 0.5f, b.y + s * 0.5f), col);
}

void drawWorldIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	dl->AddCircle(c, r, col, 18, 1.5f);
	dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.0f);
	dl->AddEllipse(c, ImVec2(r * 0.45f, r), col, 0.0f, 18, 1.0f);
}

void drawLocalIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	const ImVec2 p[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
	dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, 1.5f);
	dl->AddLine(c, ImVec2(c.x + r * 0.5f, c.y), col, 1.0f);
	dl->AddLine(c, ImVec2(c.x, c.y - r * 0.5f), col, 1.0f);
}

void drawSnapIcon(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	// A 2x2 grid to suggest snapping.
	dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 0.0f, 0, 1.0f);
	dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 1.0f);
	dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.0f);
}

// Large folder / file glyphs for the asset grid view.
void drawFolderGlyph(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	const ImVec2 bodyMin(c.x - r, c.y - r * 0.45f);
	const ImVec2 bodyMax(c.x + r, c.y + r * 0.7f);
	dl->AddRectFilled(bodyMin, bodyMax, col, r * 0.15f);
	dl->AddRectFilled(ImVec2(bodyMin.x, bodyMin.y - r * 0.35f), ImVec2(bodyMin.x + r, bodyMin.y + r * 0.1f), col, r * 0.15f); // tab
}

void drawFileGlyph(ImDrawList *dl, const ImVec2 &c, float r, ImU32 col)
{
	const ImVec2 mn(c.x - r * 0.7f, c.y - r);
	const ImVec2 mx(c.x + r * 0.7f, c.y + r);
	dl->AddRect(mn, mx, col, r * 0.12f, 0, 2.0f);
	dl->AddLine(ImVec2(mx.x - r * 0.45f, mn.y), ImVec2(mx.x, mn.y + r * 0.45f), col, 2.0f); // folded corner
}

// A square button with a procedurally drawn icon and a tooltip. Returns pressed.
bool iconButton(const char *id, void (*draw)(ImDrawList *, const ImVec2 &, float, ImU32), bool active, const char *tooltip, float size)
{
	ImGui::PushID(id);
	if (active)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool pressed = ImGui::Button("##icon", ImVec2(size, size));
	if (active)
		ImGui::PopStyleColor();
	const ImVec2 mn = ImGui::GetItemRectMin();
	const ImVec2 mx = ImGui::GetItemRectMax();
	const ImVec2 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
	draw(ImGui::GetWindowDrawList(), center, size * 0.28f, ImGui::GetColorU32(ImGuiCol_Text));
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", tooltip);
	ImGui::PopID();
	return pressed;
}

// Combo over shader names. Writes the chosen name to `out` and returns true on change.
bool shaderCombo(const char *label, const std::string &current, const std::vector<std::string> &names, std::string &out)
{
	bool changed = false;
	if (ImGui::BeginCombo(label, current.empty() ? "(none)" : current.c_str()))
	{
		for (const auto &name : names)
		{
			const bool selected = (name == current);
			if (ImGui::Selectable(name.c_str(), selected))
			{
				out = name;
				changed = true;
			}
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return changed;
}

// Lowercase copy for case-insensitive filtering.
std::string toLowerCopy(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// Searchable combo over names: a filter field at the top of the dropdown narrows
// the list. Writes the chosen name to `out` and returns true on change.
bool searchableCombo(const char *label, const std::string &current, const std::vector<std::string> &names, std::string &out)
{
	bool changed = false;
	if (ImGui::BeginCombo(label, current.empty() ? "(none)" : current.c_str()))
	{
		static char filter[128] = "";
		if (ImGui::IsWindowAppearing())
		{
			filter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##search", "search...", filter, sizeof(filter));
		ImGui::Separator();
		const std::string needle = toLowerCopy(filter);
		for (const auto &name : names)
		{
			if (!needle.empty() && toLowerCopy(name).find(needle) == std::string::npos)
				continue;
			const bool selected = (name == current);
			if (ImGui::Selectable(name.c_str(), selected))
			{
				out = name;
				changed = true;
			}
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return changed;
}

// Open a file in the OS default app or VS Code. Uses std::system so we need no
// extra link dependency (ShellExecute would pull in shell32); the brief console
// flash is acceptable for an editor tool.
void openExternally(const std::filesystem::path &path, bool useVsCode)
{
	const std::string quoted = "\"" + path.string() + "\"";
#if defined(_WIN32)
	// `explorer "file"` launches the file's default association reliably; a plain
	// `start "" "file"` through std::system mangles the nested quotes and no-ops.
	const std::string command = useVsCode ? ("code " + quoted) : ("explorer " + quoted);
#elif defined(__APPLE__)
	const std::string command = (useVsCode ? "code " : "open ") + quoted;
#else
	const std::string command = (useVsCode ? "code " : "xdg-open ") + quoted;
#endif
	std::system(command.c_str());
}

// Reveal a file/folder in the OS file browser (highlighting the file).
void showInExplorer(const std::filesystem::path &path)
{
	const std::string quoted = "\"" + path.string() + "\"";
#if defined(_WIN32)
	std::error_code ec;
	// /select highlights a file in its folder; a folder opens directly.
	const std::string command = std::filesystem::is_directory(path, ec)
		? ("explorer " + quoted)
		: ("explorer /select," + quoted);
	std::system(command.c_str());
#elif defined(__APPLE__)
	std::system(("open -R " + quoted).c_str());
#else
	std::system(("xdg-open " + quoted).c_str());
#endif
}

// The editor's SDL window, so native dialogs can drop it out of fullscreen while
// they are open (set in the SceneEditorUI constructor).
SDL_Window *g_dialogParentWindow = nullptr;

// A native modal dialog (tinyfiledialogs) can be hidden behind a fullscreen
// window: since the dialog blocks the thread, the app looks frozen and cannot be
// closed. While a dialog is open, leave fullscreen and restore it afterwards.
struct FullscreenDialogGuard
{
	SDL_Window *window = nullptr;
	bool wasFullscreen = false;

	FullscreenDialogGuard()
	{
		window = g_dialogParentWindow;
		if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
		{
			wasFullscreen = true;
			SDL_SetWindowFullscreen(window, false);
			SDL_SyncWindow(window); // ensure the change lands before the dialog blocks
		}
	}

	~FullscreenDialogGuard()
	{
		if (wasFullscreen && window)
		{
			SDL_SetWindowFullscreen(window, true);
			SDL_SyncWindow(window);
		}
	}
};

// Native OS open-file dialog (tinyfiledialogs). Returns the picked path, or an
// empty string if the user cancelled. @p patterns are globs like "*.png".
// @p defaultDir, if non-empty, is the directory the dialog opens in.
std::string pickFile(const char *title, const std::vector<const char *> &patterns, const char *description, const std::string &defaultDir = "")
{
	const FullscreenDialogGuard dialogGuard;
	// A trailing separator tells tinyfiledialogs to treat it as a start directory.
	const std::string start = defaultDir.empty() ? std::string() : defaultDir + "/";
	const char *result = tinyfd_openFileDialog(
		title,
		start.c_str(),
		static_cast<int>(patterns.size()),
		patterns.empty() ? nullptr : patterns.data(),
		description,
		0 /* single selection */);
	return result ? std::string(result) : std::string();
}

// Accept an ASSET_PATH drag-drop payload onto the last-submitted item. Writes the
// dropped engine path into @p out and returns true on a drop this frame.
bool acceptAssetDrop(std::string &out)
{
	bool dropped = false;
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
		{
			out.assign(static_cast<const char *>(payload->Data));
			dropped = true;
		}
		ImGui::EndDragDropTarget();
	}
	return dropped;
}

// Reusable "copy path as..." menu items, always in this order: root (absolute),
// relative (within its asset/resource root), then the resource/asset engine
// token. Call between ImGui Begin/EndPopup (or a context menu).
void copyPathMenuItems(const std::filesystem::path &absolutePath)
{
	using engine::core::PathProvider;
	if (ImGui::MenuItem("Copy root path"))
		ImGui::SetClipboardText(absolutePath.generic_string().c_str());

	std::string relative;
	if (auto asset = PathProvider::toAssetRelative(absolutePath))
		relative = *asset;
	else if (auto resource = PathProvider::toResourceRelative(absolutePath))
		relative = *resource;
	else
		relative = absolutePath.filename().generic_string();
	if (ImGui::MenuItem("Copy relative path"))
		ImGui::SetClipboardText(relative.c_str());

	if (ImGui::MenuItem("Copy resource/asset path"))
		ImGui::SetClipboardText(PathProvider::toEnginePath(absolutePath).c_str());
}

// A reusable path field: a "..." button (native file dialog) + an input field
// with a tooltip, and accepts a dragged asset. Writes into @p buffer and returns
// true when it changed this frame (browsed, Enter pressed, or a file dropped).
bool pathInput(const char *id, std::string &buffer, const char *tooltip,
	const std::vector<const char *> &filters, const std::string &defaultDir)
{
	bool changed = false;
	ImGui::PushID(id);
	if (ImGui::Button("..."))
	{
		const std::string picked = pickFile("Select file", filters, "Files", defaultDir);
		if (!picked.empty())
		{
			buffer = picked;
			changed = true;
		}
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Browse (system file dialog)");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputText("##path", &buffer, ImGuiInputTextFlags_EnterReturnsTrue))
		changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", tooltip);
	if (acceptAssetDrop(buffer))
		changed = true;
	ImGui::PopID();
	return changed;
}

// Native OS save-file dialog. Returns the chosen path, or empty if cancelled.
std::string pickSaveFile(const char *title, const char *defaultName, const std::vector<const char *> &patterns, const char *description)
{
	const FullscreenDialogGuard dialogGuard;
	const char *result = tinyfd_saveFileDialog(
		title,
		defaultName,
		static_cast<int>(patterns.size()),
		patterns.empty() ? nullptr : patterns.data(),
		description);
	return result ? std::string(result) : std::string();
}

// Depth-first collect every CameraNode in the subtree, skipping @p excludeId
// (used to leave out the editor camera, which the dropdown lists separately).
void collectCameras(const std::shared_ptr<engine::scene::nodes::Node> &node, uint64_t excludeId,
	std::vector<std::shared_ptr<engine::scene::nodes::CameraNode>> &out)
{
	if (!node)
		return;
	if (auto camera = std::dynamic_pointer_cast<engine::scene::nodes::CameraNode>(node); camera && camera->getId() != excludeId)
		out.push_back(camera);
	for (const auto &child : node->getChildren())
		collectCameras(child, excludeId, out);
}

const char *logLevelShort(int level)
{
	switch (level)
	{
	case 0: return "TRC";
	case 1: return "DBG";
	case 2: return "INF";
	case 3: return "WRN";
	case 4: return "ERR";
	case 5: return "CRT";
	default: return "?";
	}
}

ImVec4 logLevelColor(int level)
{
	switch (level)
	{
	case 0: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f); // trace
	case 1: return ImVec4(0.60f, 0.65f, 0.75f, 1.0f); // debug
	case 2: return ImVec4(0.70f, 0.82f, 0.70f, 1.0f); // info
	case 3: return ImVec4(1.00f, 0.80f, 0.30f, 1.0f); // warn
	case 4: return ImVec4(1.00f, 0.42f, 0.42f, 1.0f); // error
	case 5: return ImVec4(1.00f, 0.30f, 0.55f, 1.0f); // critical
	default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

// Counts a node's reflected fields (so the inspector only shows a Fields section
// when there is something to show).
class CountReflector : public engine::reflection::Reflector
{
  public:
	int count = 0;
	void field(const char *, float &) override { ++count; }
	void field(const char *, int &) override { ++count; }
	void field(const char *, bool &) override { ++count; }
	void field(const char *, glm::vec2 &) override { ++count; }
	void field(const char *, glm::vec3 &) override { ++count; }
	void field(const char *, glm::vec4 &) override { ++count; }
	void field(const char *, std::string &) override { ++count; }
};

// Draws an editable inspector row per reflected field; `changed` records edits.
class ImGuiReflector : public engine::reflection::Reflector
{
  public:
	bool changed = false;
	void field(const char *name, float &value) override { changed |= ImGui::DragFloat(name, &value, 0.05f); }
	void field(const char *name, int &value) override { changed |= ImGui::DragInt(name, &value); }
	void field(const char *name, bool &value) override { changed |= ImGui::Checkbox(name, &value); }
	void field(const char *name, glm::vec2 &value) override { changed |= ImGui::DragFloat2(name, &value.x, 0.05f); }
	void field(const char *name, glm::vec3 &value) override { changed |= ImGui::DragFloat3(name, &value.x, 0.05f); }
	void field(const char *name, glm::vec4 &value) override { changed |= ImGui::DragFloat4(name, &value.x, 0.05f); }
	void field(const char *name, std::string &value) override { changed |= ImGui::InputText(name, &value); }
	void field(const char *name, float &value, float min, float max) override { changed |= ImGui::SliderFloat(name, &value, min, max); }
	void field(const char *name, int &value, int min, int max) override { changed |= ImGui::SliderInt(name, &value, min, max); }
	void colorField(const char *name, glm::vec3 &value) override { changed |= ImGui::ColorEdit3(name, &value.x); }
	void colorField(const char *name, glm::vec4 &value) override { changed |= ImGui::ColorEdit4(name, &value.x); }
};
} // namespace

SceneEditorUI::SceneEditorUI(engine::GameEngine &engine, EditorState &state, std::shared_ptr<LogStore> logStore) :
	m_engine(engine), m_state(state), m_logStore(std::move(logStore))
{
	m_gizmoOperation = ImGuizmo::TRANSLATE;
	m_gizmoMode = ImGuizmo::WORLD;

	for (size_t i = 0; i < m_shortcuts.size(); ++i)
		m_shortcuts[i] = kActionDefs[i].def;
	loadShortcuts(); // override the Unity-like defaults with any saved bindings

	// Native file dialogs use this to leave fullscreen while they are open.
	g_dialogParentWindow = m_engine.getWindow();

	m_settings = m_engine.getOptions();
	m_project.settings = m_settings;
	loadRecentScenes();
	loadRecentProjects();
}

SceneEditorUI::~SceneEditorUI()
{
	if (m_buildThread.joinable())
		m_buildThread.join();
}

void SceneEditorUI::render()
{
	ImGuizmo::BeginFrame();

	// Startup: on the first frame, auto-open the most recent project. With no
	// project the welcome screen stands in for the editor until one is chosen.
	if (!m_bootstrapped)
	{
		m_bootstrapped = true;
		bootstrap();
	}
	if (m_showWelcome)
	{
		drawWelcome();
		return;
	}

	processShortcuts();

	drawMenuBar();

	const ImGuiID dockspaceId = ImGui::GetID("SceneEditorDockSpace");
	// A layout restored from imgui.ini already owns this dock node, so only build
	// the default the first time ever (or when "Reset Layout" forces it). This lets
	// the user's docking - and panel sizes - persist across sessions.
	const bool hasSavedLayout = ImGui::DockBuilderGetNode(dockspaceId) != nullptr;
	ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());
	if (!m_layoutInitialized)
	{
		m_layoutInitialized = true;
		if (m_forceLayoutRebuild || !hasSavedLayout)
			buildDefaultLayout(dockspaceId);
		m_forceLayoutRebuild = false;
	}

	drawViewport();
	drawHierarchy();
	drawInspector();
	drawShaders();
	drawMaterials();
	drawAssets();
	drawSettings();
	drawLog();

	// Mutate the node tree only after every panel has finished reading it.
	applyDeferredHierarchyOps();

	if (m_openSaveAsPopup)
	{
		ImGui::OpenPopup("Save Scene As");
		m_openSaveAsPopup = false;
	}
	if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Scene folder file (e.g. scenes/MyScene/scene.json):");
		ImGui::InputText("##saveAsPath", &m_saveAsPath);
		if (ImGui::Button("Save") && !m_saveAsPath.empty())
		{
			saveScene(m_saveAsPath);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Summary of the last cross-project scene import (what was copied / reused /
	// renamed, and any script type clashes the user must reconcile).
	if (m_openImportReportPopup)
	{
		ImGui::OpenPopup("Scene Imported");
		m_openImportReportPopup = false;
	}
	if (ImGui::BeginPopupModal("Scene Imported", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(m_importSummary.c_str());
		if (m_importHadClashes)
		{
			ImGui::Separator();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
			ImGui::TextWrapped("A different script already defines that type name in this project. "
							   "Reconcile the clash before Build Game, or the game will not compile.");
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
		if (ImGui::Button("OK"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Prompt to import a file picked from outside the project into a chosen assets/
	// subfolder, so the scene references it by a portable asset:// token instead of
	// an absolute path. The apply callback (set by requestImport) does the actual
	// use (assign to a material, set as environment, add as a model, ...).
	if (m_openImportPopup)
	{
		ImGui::OpenPopup("Import Into Project");
		m_openImportPopup = false;
	}
	if (ImGui::BeginPopupModal("Import Into Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("'%s' is outside the project. Import a copy into the project's assets so the scene stays portable.",
			m_pendingImportSource.filename().string().c_str());
		ImGui::Spacing();
		ImGui::TextUnformatted("Destination folder (under assets/):");
		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputText("##importSubdir", &m_pendingImportSubdir);
		const std::filesystem::path dest =
			engine::core::PathProvider::getAssetRoot() / m_pendingImportSubdir / m_pendingImportSource.filename();
		ImGui::TextDisabled("-> %s", engine::core::PathProvider::toEnginePath(dest).c_str());
		ImGui::Spacing();

		if (ImGui::Button("Import"))
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			fs::create_directories(dest.parent_path(), ec);
			fs::copy_file(m_pendingImportSource, dest, fs::copy_options::overwrite_existing, ec);
			if (ec)
				m_assetStatus = "Import failed: " + ec.message();
			else if (m_pendingImportApply)
				m_pendingImportApply(engine::core::PathProvider::toEnginePath(dest));
			m_pendingImportApply = nullptr;
			m_pendingImportSource.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep External"))
		{
			// Absolute path: not portable. Warn now; Build Game warns again at stage time.
			spdlog::warn("Editor: using an absolute asset path outside the project - '{}'. The scene is no longer portable.",
				m_pendingImportSource.generic_string());
			if (m_pendingImportApply)
				m_pendingImportApply(m_pendingImportSource.generic_string());
			m_pendingImportApply = nullptr;
			m_pendingImportSource.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_pendingImportApply = nullptr;
			m_pendingImportSource.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Confirm deletion of one or more project assets.
	if (m_openDeletePopup)
	{
		ImGui::OpenPopup("Delete Assets");
		m_openDeletePopup = false;
	}
	if (ImGui::BeginPopupModal("Delete Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (m_pendingDeletes.size() == 1)
			ImGui::TextWrapped("Delete '%s'? This cannot be undone.",
				m_pendingDeletes.front().filename().string().c_str());
		else
			ImGui::TextWrapped("Delete %zu items? This cannot be undone.", m_pendingDeletes.size());
		ImGui::Spacing();
		if (ImGui::Button("Delete"))
		{
			for (const auto &victim : m_pendingDeletes)
			{
				std::error_code ec;
				std::filesystem::remove_all(victim, ec);
				if (ec)
					m_assetStatus = "Delete failed: " + ec.message();
			}
			++m_browserGeneration; // refresh the explorer listing
			m_assetSelection.clear();
			m_pendingDeletes.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_pendingDeletes.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Rename a project asset file/folder.
	if (m_openRenamePopup)
	{
		ImGui::OpenPopup("Rename Asset");
		m_openRenamePopup = false;
	}
	if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Rename '%s' to:", m_pendingRenameSource.filename().string().c_str());
		ImGui::InputText("##renameBuffer", &m_renameBuffer);
		ImGui::Spacing();
		if (ImGui::Button("Rename") && !m_renameBuffer.empty())
		{
			std::error_code ec;
			const std::filesystem::path dest = m_pendingRenameSource.parent_path() / m_renameBuffer;
			std::filesystem::rename(m_pendingRenameSource, dest, ec);
			m_assetStatus = ec ? ("Rename failed: " + ec.message()) : ("Renamed to " + m_renameBuffer);
			++m_browserGeneration; // refresh the asset tree
			m_pendingRenameSource.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_pendingRenameSource.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	drawShortcutsPopup();
}

void SceneEditorUI::bootstrap()
{
	loadRecentProjects();
	loadRecentScenes();

	// Auto-open the most recent project that still exists on disk.
	for (const auto &entry : m_recentProjects)
	{
		std::error_code ec;
		const std::filesystem::path candidate(entry);
		if (!std::filesystem::exists(candidate, ec))
			continue;
		openProject(candidate);
		if (!m_projectPath.empty())
			return; // a project loaded; the editor proceeds normally
	}

	// Nothing to reopen: stand up the welcome screen.
	m_showWelcome = true;
	m_assetStatus.clear();
}

void SceneEditorUI::drawWelcome()
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
								   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
								   ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("##welcome", nullptr, flags))
	{
		const float contentWidth = 460.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - contentWidth) * 0.5f);
		ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.22f);
		ImGui::BeginGroup();

		ImGui::TextUnformatted("Vienna Scene Editor");
		ImGui::TextDisabled("Open or create a project to begin. A project keeps its scenes,");
		ImGui::TextDisabled("materials, textures and scripts together in one folder on disk.");
		ImGui::Dummy(ImVec2(0.0f, 14.0f));

		if (ImGui::Button("New Project...", ImVec2(contentWidth, 0.0f)))
			newProject();
		if (ImGui::Button("Open Project...", ImVec2(contentWidth, 0.0f)))
		{
			const std::string path = pickFile("Open Project", {"*.vproj"}, "Project");
			if (!path.empty())
				openProject(path);
		}

		if (!m_recentProjects.empty())
		{
			ImGui::Dummy(ImVec2(0.0f, 14.0f));
			ImGui::TextDisabled("Recent projects");
			ImGui::Separator();
			for (size_t i = 0; i < m_recentProjects.size() && i < 8; ++i)
			{
				if (ImGui::Selectable(m_recentProjects[i].c_str()))
				{
					openProject(m_recentProjects[i]);
					break; // openProject reorders m_recentProjects; stop iterating
				}
			}
		}

		if (!m_assetStatus.empty())
		{
			ImGui::Dummy(ImVec2(0.0f, 14.0f));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
			ImGui::TextUnformatted(m_assetStatus.c_str());
			ImGui::PopTextWrapPos();
		}

		ImGui::EndGroup();
	}
	ImGui::End();
}

void SceneEditorUI::drawMenuBar()
{
	if (!ImGui::BeginMainMenuBar())
		return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N"))
			newScene();
		// Scenes and projects are distinct files - a scene is a .vscene, a project a
		// .vproj - so they never get confused. Opening a scene loads it INTO the
		// current project; opening a project switches the whole workspace. Each
		// loader also rejects the wrong file type.
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O") && confirmDiscardIfDirty())
		{
			const std::string path = pickFile("Open Scene", {"*.vscene"}, "Scene",
				engine::core::PathProvider::getScenes().string());
			if (!path.empty())
				openScene(path);
		}
		if (ImGui::MenuItem("Open Project...") && confirmDiscardIfDirty())
		{
			const std::string path = pickFile("Open Project", {"*.vproj"}, "Project",
				engine::core::PathProvider::getAssetRoot().string());
			if (!path.empty())
				openProject(path);
		}
		// Pull a scene (and everything it uses) out of another project into this one.
		if (ImGui::MenuItem("Import Scene from Project...", nullptr, false, !m_projectPath.empty()) && confirmDiscardIfDirty())
		{
			const std::string path = pickFile("Import Scene from Project", {"*.vscene"}, "Scene");
			if (!path.empty())
				importSceneFromProject(path);
		}
		if (ImGui::BeginMenu("Open Recent", !m_recentScenes.empty() || !m_recentProjects.empty()))
		{
			ImGui::TextDisabled("Scenes");
			if (m_recentScenes.empty())
				ImGui::TextDisabled("  (none)");
			for (size_t i = 0; i < m_recentScenes.size() && i < 5; ++i)
			{
				if (ImGui::MenuItem(m_recentScenes[i].c_str()))
				{
					if (confirmDiscardIfDirty())
						openScene(m_recentScenes[i]);
					break; // openScene reorders m_recentScenes; stop iterating
				}
			}
			ImGui::Separator();
			ImGui::TextDisabled("Projects");
			if (m_recentProjects.empty())
				ImGui::TextDisabled("  (none)");
			for (size_t i = 0; i < m_recentProjects.size() && i < 5; ++i)
			{
				if (ImGui::MenuItem(m_recentProjects[i].c_str()))
				{
					if (confirmDiscardIfDirty())
						openProject(m_recentProjects[i]);
					break; // openProject reorders m_recentProjects; stop iterating
				}
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Import Model..."))
		{
			const std::string picked = pickFile("Import Model",
				{"*.obj", "*.gltf", "*.glb", "*.fbx"}, "Model files",
				engine::core::PathProvider::getAssetRoot().string());
			if (!picked.empty())
			{
				// Import into the project (if external) then add it, so the scene stores
				// a portable asset:// path rather than an absolute one.
				requestImport(picked, "models", [this](const std::string &token)
				{
					auto resources = m_engine.getResourceManager();
					auto sceneManager = m_engine.getSceneManager();
					auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
					if (!resources || !resources->m_modelManager || !scene || !scene->getRoot())
						return;
					const std::filesystem::path path = engine::core::PathProvider::resolveEnginePath(token);
					if (auto model = resources->m_modelManager->createModel(path))
					{
						auto node = std::make_shared<engine::scene::nodes::ModelRenderNode>(path);
						node->setModel(model.value()->getHandle());
						node->setName(path.stem().string());
						auto parent = m_state.selected.lock();
						(parent ? parent : scene->getRoot())->addChild(node->asNode(), false);
						m_state.selected = node;
						m_state.sceneDirty = true;
						m_assetStatus = "Imported " + token;
					}
					else
					{
						m_assetStatus = "Failed to import: " + token;
					}
				});
			}
		}
		ImGui::Separator();
		// Save persists the whole PROJECT: the active scene plus project.vproj (its
		// scene list, material library and settings). A scene always belongs to a
		// project, so saving stores both together.
		if (ImGui::MenuItem("Save Project + Scene", "Ctrl+S"))
			saveProject();
		if (ImGui::MenuItem("Save Scene As..."))
		{
			m_saveAsPath = m_state.currentScenePath.empty()
				? std::string("scenes/Untitled/scene.vscene")
				: m_state.currentScenePath.string();
			m_openSaveAsPopup = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Export Self-Contained..."))
		{
			const std::string fileName = m_state.currentScenePath.empty()
				? std::string("scene.vscene")
				: m_state.currentScenePath.filename().string();
			// Open the dialog in the scenes folder by default.
			const std::string defaultPath = (engine::core::PathProvider::getScenes() / fileName).string();
			const std::string destination = pickSaveFile("Export Self-Contained Scene", defaultPath.c_str(), {"*.vscene"}, "Scene");
			if (!destination.empty())
			{
				auto sceneManager = m_engine.getSceneManager();
				auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
				if (scene && engine::scene::SceneSerializer::exportSelfContained(*scene, destination))
					m_assetStatus = "Exported self-contained scene to " + destination;
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Keyboard Shortcuts..."))
			m_openShortcutsPopup = true;
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Project"))
	{
		ImGui::TextDisabled("%s", m_project.name.empty() ? "(unnamed project)" : m_project.name.c_str());
		ImGui::Separator();
		if (ImGui::MenuItem("New Project"))
			newProject();
		if (ImGui::MenuItem("Save Project"))
			saveProject();
		ImGui::TextDisabled("(open a project via File > Open)");
		ImGui::Separator();
		// The scenes that make up this project; selecting one loads it INTO the
		// current project (the project's materials + settings stay in place).
		if (ImGui::BeginMenu("Scenes", !m_project.scenes.empty()))
		{
			const std::string activeToken = m_state.currentScenePath.empty()
				? std::string()
				: engine::core::PathProvider::toEnginePath(m_state.currentScenePath);
			for (const auto &sceneToken : m_project.scenes)
			{
				const bool isActive = (sceneToken == activeToken);
				if (ImGui::MenuItem(sceneToken.c_str(), nullptr, isActive) && !isActive && confirmDiscardIfDirty())
				{
					openScene(engine::core::PathProvider::resolveEnginePath(sceneToken));
					break; // openScene may append to m_project.scenes
				}
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Build Game...", nullptr, false, !m_building))
			buildGame();
		if (m_building)
			ImGui::TextDisabled("  building... (see Log)");
		if (ImGui::MenuItem("Open Build Folder", nullptr, false, !m_building))
		{
			const std::filesystem::path out =
				engine::core::PathProvider::getLibraryRoot() / "examples" / "build" / "runtime_player" / "Windows" / "Release";
			std::error_code ec;
			if (std::filesystem::exists(out, ec))
				showInExplorer(out);
			else
				m_assetStatus = "No build yet - use Build Game first.";
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		ImGui::MenuItem("Viewport", nullptr, &m_showViewport);
		ImGui::MenuItem("Hierarchy", nullptr, &m_showHierarchy);
		ImGui::MenuItem("Inspector", nullptr, &m_showInspector);
		ImGui::MenuItem("Materials", nullptr, &m_showMaterials);
		ImGui::MenuItem("Shaders", nullptr, &m_showShaders);
		ImGui::MenuItem("Assets", nullptr, &m_showAssets);
		ImGui::MenuItem("Settings", nullptr, &m_showSettings);
		ImGui::MenuItem("Log", nullptr, &m_showLog);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout"))
		{
			m_showViewport = m_showHierarchy = m_showInspector = m_showAssets = m_showLog = true;
			m_showShaders = m_showMaterials = true;
			m_layoutInitialized = false;  // buildDefaultLayout re-runs next frame
			m_forceLayoutRebuild = true;  // override the saved imgui.ini layout
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	// Workspace status: the open project owns the active scene. A scene folder is
	// named after the scene (scene.json inside it), so prefer the folder name.
	const std::string projectName = m_project.name.empty() ? "(unnamed project)" : m_project.name;
	std::string sceneName = "untitled";
	if (!m_state.currentScenePath.empty())
	{
		const std::string stem = m_state.currentScenePath.stem().string();
		sceneName = (stem == "scene") ? m_state.currentScenePath.parent_path().filename().string() : stem;
	}
	ImGui::TextDisabled("Project: %s", projectName.c_str());
	ImGui::TextDisabled("|");
	ImGui::TextDisabled("Scene: %s%s", sceneName.c_str(), m_state.sceneDirty ? " *" : "");

	ImGui::EndMainMenuBar();
}

void SceneEditorUI::buildDefaultLayout(unsigned int dockspaceId)
{
	// WorkSize can be (0,0) on the very first frame before the work area is
	// computed; DockBuilderSetNodeSize asserts on a non-positive size. Fall back
	// to the full viewport size, then a fixed size, so the build is always valid.
	ImVec2 size = ImGui::GetMainViewport()->WorkSize;
	if (size.x < 1.0f || size.y < 1.0f)
		size = ImGui::GetMainViewport()->Size;
	if (size.x < 1.0f || size.y < 1.0f)
		size = ImVec2(1280.0f, 720.0f);

	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, size);

	ImGuiID center = dockspaceId;
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
	ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);

	ImGui::DockBuilderDockWindow("Hierarchy", left);
	ImGui::DockBuilderDockWindow("Materials", leftBottom); // lower-left
	ImGui::DockBuilderDockWindow("Shaders", leftBottom);   // tab alongside Materials
	ImGui::DockBuilderDockWindow("Inspector", right);
	ImGui::DockBuilderDockWindow("Settings", right); // tab alongside Inspector
	ImGui::DockBuilderDockWindow("Assets", bottom);
	ImGui::DockBuilderDockWindow("Log", bottom); // tab alongside Assets
	ImGui::DockBuilderDockWindow("Viewport", center);
	ImGui::DockBuilderFinish(dockspaceId);
}

void SceneEditorUI::drawViewport()
{
	if (!m_showViewport)
		return;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Viewport", &m_showViewport);

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	m_state.viewportSize = glm::uvec2(
		static_cast<uint32_t>(avail.x > 1.0f ? avail.x : 1.0f),
		static_cast<uint32_t>(avail.y > 1.0f ? avail.y : 1.0f)
	);

	// Screen position of the rendered image - the gizmo overlay rect must match.
	const ImVec2 imagePos = ImGui::GetCursorScreenPos();

	if (auto renderer = m_engine.getRenderer().lock())
	{
		auto output = renderer->getCameraOutputTexture(m_state.editorCameraId);
		if (output && output->getWidth() > 0)
			ImGui::Image(toImTextureID(output->getTextureView()), avail);
		else
			ImGui::TextUnformatted("Rendering...");
	}

	// No invisible button over the image: ImGuizmo reads the mouse directly, and
	// an ImGui button covering the viewport would steal the press and block the
	// gizmo from dragging. Camera nav is allowed while hovering, suppressed while
	// the gizmo is in use.
	m_state.allowCameraInput = ImGui::IsWindowHovered() && !ImGuizmo::IsUsing();

	auto sceneManager = m_engine.getSceneManager();
	auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
	auto camera = scene ? scene->getMainCamera() : nullptr;
	auto selectedNode = m_state.selected.lock();
	auto selectedSpatial = selectedNode ? selectedNode->asSpatialNode() : nullptr;

	if (camera && selectedSpatial)
	{
		ImGuizmo::SetOrthographic(!camera->isPerspective());
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(imagePos.x, imagePos.y, avail.x, avail.y);

		glm::mat4 view = camera->getViewMatrix();
		glm::mat4 projection = camera->getProjectionMatrix();
		auto &transform = selectedSpatial->getTransform();
		glm::mat4 world = transform.getWorldMatrix();

		float snapValues[3] = {0.0f, 0.0f, 0.0f};
		const float *snap = nullptr;
		if (m_gizmoSnap)
		{
			const float amount = (m_gizmoOperation == ImGuizmo::ROTATE) ? m_snapRotate
				: (m_gizmoOperation == ImGuizmo::SCALE) ? m_snapScale
				: m_snapTranslate;
			snapValues[0] = snapValues[1] = snapValues[2] = amount;
			snap = snapValues;
		}

		if (m_gizmoActive && ImGuizmo::Manipulate(
				glm::value_ptr(view),
				glm::value_ptr(projection),
				static_cast<ImGuizmo::OPERATION>(m_gizmoOperation),
				static_cast<ImGuizmo::MODE>(m_gizmoMode),
				glm::value_ptr(world),
				nullptr,
				snap))
		{
			// Manipulation happens in world space; convert back to the node's
			// parent-relative local transform before writing the components.
			glm::mat4 parentWorld(1.0f);
			if (auto *parent = transform.getParent())
				parentWorld = parent->getWorldMatrix();
			const glm::mat4 local = glm::inverse(parentWorld) * world;

			float translation[3], rotation[3], scale[3];
			ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), translation, rotation, scale);
			transform.setLocalPosition(glm::vec3(translation[0], translation[1], translation[2]));
			transform.setLocalEulerAngles(glm::vec3(rotation[0], rotation[1], rotation[2]));
			transform.setLocalScale(glm::vec3(scale[0], scale[1], scale[2]));
			m_state.sceneDirty = true;
		}
	}

	const bool toolbarHovered = drawGizmoToolbar(imagePos.x, imagePos.y);

	// Camera selector (top-right overlay): free-fly editor camera, or look through
	// any CameraNode in the scene. The controller reads m_state.viewCamera.
	bool cameraComboHovered = false;
	{
		std::vector<std::shared_ptr<engine::scene::nodes::CameraNode>> cameras;
		if (scene && scene->getRoot())
			collectCameras(scene->getRoot(), m_state.editorCameraId, cameras);

		const float comboWidth = 200.0f;
		ImGui::SetCursorScreenPos(ImVec2(imagePos.x + avail.x - comboWidth - 8.0f, imagePos.y + 8.0f));
		ImGui::SetNextItemWidth(comboWidth);

		auto current = m_state.viewCamera.lock();
		const std::string preview = current ? nodeLabel(*current) : std::string("Editor (free-fly)");
		if (ImGui::BeginCombo("##viewCamera", preview.c_str()))
		{
			if (ImGui::Selectable("Editor (free-fly)", !current))
				m_state.viewCamera.reset();
			for (const auto &cam : cameras)
			{
				ImGui::PushID(static_cast<int>(cam->getId()));
				if (ImGui::Selectable(nodeLabel(*cam).c_str(), current == cam))
					m_state.viewCamera = cam;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		cameraComboHovered = ImGui::IsItemHovered();
	}

	// Click to pick: only an empty-image click (not a toolbar button, not the
	// camera selector, not the gizmo) selects. Computed after the overlays + gizmo
	// so their hover state is current this frame.
	if (camera && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !toolbarHovered && !cameraComboHovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
	{
		const ImVec2 mouse = ImGui::GetMousePos();
		const float ndcX = avail.x > 0.0f ? (mouse.x - imagePos.x) / avail.x * 2.0f - 1.0f : 0.0f;
		const float ndcY = avail.y > 0.0f ? 1.0f - (mouse.y - imagePos.y) / avail.y * 2.0f : 0.0f;
		const glm::mat4 invViewProj = glm::inverse(camera->getProjectionMatrix() * camera->getViewMatrix());
		glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 0.5f, 1.0f);
		if (glm::abs(farPoint.w) > 1e-6f)
			farPoint /= farPoint.w;
		const glm::vec3 rayOrigin = camera->getPosition();
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) - rayOrigin);

		float bestT = 1e30f;
		std::shared_ptr<engine::scene::nodes::Node> picked;
		if (scene->getRoot())
			pickRecursive(scene->getRoot(), rayOrigin, rayDir, m_state.editorCameraId, bestT, picked);
		if (picked)
			m_state.selected = picked;
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

bool SceneEditorUI::drawGizmoToolbar(float originX, float originY)
{
	ImGui::SetCursorScreenPos(ImVec2(originX + 8.0f, originY + 8.0f));
	const float size = ImGui::GetFrameHeight() + 6.0f;
	bool hovered = false;

	if (iconButton("view", drawViewIcon, !m_gizmoActive,
			("View / Pan (" + shortcutText(binding(m_shortcuts, EditorAction::ViewTool)) + ")").c_str(), size))
		m_gizmoActive = false;
	hovered |= ImGui::IsItemHovered();
	ImGui::SameLine();
	if (iconButton("move", drawMoveIcon, m_gizmoActive && m_gizmoOperation == ImGuizmo::TRANSLATE,
			("Move (" + shortcutText(binding(m_shortcuts, EditorAction::Move)) + ")").c_str(), size))
	{
		m_gizmoActive = true;
		m_gizmoOperation = ImGuizmo::TRANSLATE;
	}
	hovered |= ImGui::IsItemHovered();
	ImGui::SameLine();
	if (iconButton("rotate", drawRotateIcon, m_gizmoActive && m_gizmoOperation == ImGuizmo::ROTATE,
			("Rotate (" + shortcutText(binding(m_shortcuts, EditorAction::Rotate)) + ")").c_str(), size))
	{
		m_gizmoActive = true;
		m_gizmoOperation = ImGuizmo::ROTATE;
	}
	hovered |= ImGui::IsItemHovered();
	ImGui::SameLine();
	if (iconButton("scale", drawScaleIcon, m_gizmoActive && m_gizmoOperation == ImGuizmo::SCALE,
			("Scale (" + shortcutText(binding(m_shortcuts, EditorAction::Scale)) + ")").c_str(), size))
	{
		m_gizmoActive = true;
		m_gizmoOperation = ImGuizmo::SCALE;
	}
	hovered |= ImGui::IsItemHovered();
	ImGui::SameLine();
	const bool worldSpace = (m_gizmoMode == ImGuizmo::WORLD);
	if (iconButton("space", worldSpace ? drawWorldIcon : drawLocalIcon, false,
			(std::string(worldSpace ? "World" : "Local") + " space (" + shortcutText(binding(m_shortcuts, EditorAction::ToggleSpace)) + ")").c_str(), size))
		m_gizmoMode = worldSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	hovered |= ImGui::IsItemHovered();
	ImGui::SameLine();
	if (iconButton("snap", drawSnapIcon, m_gizmoSnap, "Snap to grid / angle / step", size))
		m_gizmoSnap = !m_gizmoSnap;
	hovered |= ImGui::IsItemHovered();
	if (m_gizmoSnap)
	{
		// Inline editor for the active operation's snap amount.
		float *value = (m_gizmoOperation == ImGuizmo::ROTATE) ? &m_snapRotate
			: (m_gizmoOperation == ImGuizmo::SCALE) ? &m_snapScale
			: &m_snapTranslate;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70.0f);
		ImGui::DragFloat("##snapValue", value, 0.05f, 0.0f, 360.0f, "%.2f");
		hovered |= ImGui::IsItemHovered();
	}

	return hovered;
}

void SceneEditorUI::processShortcuts()
{
	// Never eat text input; and while flying (holding the right mouse button) the
	// WASD/QE keys drive the camera, so suppress tool/edit hotkeys - matching Unity,
	// where scene-view shortcuts are inactive during fly navigation.
	if (ImGui::GetIO().WantTextInput || ImGui::IsMouseDown(ImGuiMouseButton_Right))
		return;

	auto pressed = [this](EditorAction a) { return shortcutPressed(m_shortcuts[static_cast<size_t>(a)]); };

	if (pressed(EditorAction::ViewTool))
		m_gizmoActive = false;
	if (pressed(EditorAction::Move))
	{
		m_gizmoActive = true;
		m_gizmoOperation = ImGuizmo::TRANSLATE;
	}
	if (pressed(EditorAction::Rotate))
	{
		m_gizmoActive = true;
		m_gizmoOperation = ImGuizmo::ROTATE;
	}
	if (pressed(EditorAction::Scale))
	{
		m_gizmoActive = true;
		m_gizmoOperation = ImGuizmo::SCALE;
	}
	if (pressed(EditorAction::ToggleSpace))
		m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

	if (pressed(EditorAction::FocusSelected))
		focusSelected();

	if (pressed(EditorAction::Duplicate))
	{
		if (auto selected = m_state.selected.lock(); selected && selected->getParent() && selected->isSerializable())
		{
			if (auto clone = engine::scene::SceneSerializer::cloneNode(*selected))
			{
				// Safe to mutate here: this runs before the hierarchy tree is drawn.
				selected->getParent()->addChild(clone, false);
				m_state.selected = clone;
				m_state.sceneDirty = true;
			}
		}
	}

	if (pressed(EditorAction::DeleteSelected))
	{
		if (auto selected = m_state.selected.lock(); selected && selected->getParent())
			m_nodeToDelete = selected; // applied after the tree is drawn (applyDeferredHierarchyOps)
	}

	if (pressed(EditorAction::NewScene))
		newScene();
	if (pressed(EditorAction::OpenScene) && confirmDiscardIfDirty())
	{
		const std::string path = pickFile("Open Scene", {"*.vscene"}, "Scene", engine::core::PathProvider::getScenes().string());
		if (!path.empty())
			openScene(path);
	}
	if (pressed(EditorAction::SaveProject))
		saveProject();
}

void SceneEditorUI::focusSelected()
{
	// Only in free-fly mode: while looking through a scene camera the editor
	// camera mirrors it and must not be repositioned.
	if (!m_state.viewCamera.expired())
		return;
	auto selected = m_state.selected.lock();
	auto spatial = selected ? selected->asSpatialNode() : nullptr;
	if (!spatial)
		return;
	auto sceneManager = m_engine.getSceneManager();
	auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
	auto camera = scene ? scene->getMainCamera() : nullptr;
	if (!camera)
		return;

	const engine::math::AABB box = nodeWorldAABB(selected, spatial->getTransform().getWorldMatrix());
	const glm::vec3 center = box.center();
	const float radius = glm::max(0.25f, glm::length(box.size()) * 0.5f);
	const float fovY = glm::radians(camera->getFov());
	const float distance = radius / std::tan(fovY * 0.5f) + radius;

	auto &cameraTransform = camera->getTransform();
	cameraTransform.setWorldPosition(center - cameraTransform.forward() * distance);
}

void SceneEditorUI::drawShortcutsPopup()
{
	if (m_openShortcutsPopup)
	{
		ImGui::OpenPopup("Keyboard Shortcuts");
		m_openShortcutsPopup = false;
	}
	if (!ImGui::BeginPopupModal("Keyboard Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	// While awaiting a rebind, capture the next non-modifier key together with the
	// modifier state held at that moment, then persist.
	if (m_rebindAction >= 0)
	{
		const ImGuiIO &io = ImGui::GetIO();
		for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
		{
			const auto key = static_cast<ImGuiKey>(k);
			if (!isModifierKey(key) && ImGui::IsKeyPressed(key, false))
			{
				m_shortcuts[m_rebindAction] = Shortcut{static_cast<int>(key), io.KeyCtrl, io.KeyShift, io.KeyAlt};
				m_rebindAction = -1;
				saveShortcuts();
				break;
			}
		}
	}

	ImGui::TextDisabled("Click a binding, then press the new key (hold Ctrl / Shift / Alt to include them).");
	ImGui::Separator();
	for (size_t i = 0; i < m_shortcuts.size(); ++i)
	{
		ImGui::TextUnformatted(kActionDefs[i].name);
		ImGui::SameLine(170.0f);
		const bool waiting = (m_rebindAction == static_cast<int>(i));
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::Button(waiting ? "press a key..." : shortcutText(m_shortcuts[i]).c_str(), ImVec2(180.0f, 0.0f)))
			m_rebindAction = waiting ? -1 : static_cast<int>(i);
		ImGui::PopID();
	}

	ImGui::Separator();
	if (ImGui::Button("Reset to Unity defaults"))
	{
		for (size_t i = 0; i < m_shortcuts.size(); ++i)
			m_shortcuts[i] = kActionDefs[i].def;
		m_rebindAction = -1;
		saveShortcuts();
	}

	// Camera navigation is not rebindable; it follows Unity's hold-right-mouse model.
	ImGui::SeparatorText("Camera (hold Right Mouse)");
	auto refRow = [](const char *action, const char *keys) {
		ImGui::TextUnformatted(action);
		ImGui::SameLine(170.0f);
		ImGui::TextDisabled("%s", keys);
	};
	refRow("Look around", "Right Mouse + drag");
	refRow("Fly", "W / A / S / D");
	refRow("Down / Up", "Q / E  (or Shift / Space)");
	refRow("Zoom", "Mouse Wheel");

	ImGui::Separator();
	if (ImGui::Button("Close"))
	{
		m_rebindAction = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void SceneEditorUI::drawHierarchy()
{
	if (!m_showHierarchy)
		return;
	ImGui::Begin("Hierarchy", &m_showHierarchy);
	auto sceneManager = m_engine.getSceneManager();
	auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
	if (scene && scene->getRoot())
	{
		if (ImGui::SmallButton("+ Add"))
			ImGui::OpenPopup("AddNodePopup");
		if (ImGui::BeginPopup("AddNodePopup"))
		{
			// Add under the selection, or the root when nothing is selected.
			auto parent = m_state.selected.lock();
			drawAddNodeMenu(parent ? parent : scene->getRoot());
			ImGui::EndPopup();
		}
		ImGui::Separator();
		drawNodeTree(scene->getRoot());
	}
	ImGui::End();
}

void SceneEditorUI::drawNodeTree(const std::shared_ptr<engine::scene::nodes::Node> &node)
{
	if (!node)
		return;
	// Editor-only helper nodes (the editor camera + its controller) are flagged
	// non-serializable; they are not part of the user's scene, so keep them out
	// of the hierarchy.
	if (!node->isSerializable())
		return;

	const auto children = node->getChildren();
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (children.empty())
		flags |= ImGuiTreeNodeFlags_Leaf;
	if (m_state.selected.lock() == node)
		flags |= ImGuiTreeNodeFlags_Selected;

	const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<intptr_t>(node->getId())), flags, "%s", nodeLabel(*node).c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		m_state.selected = node;

	// Drag to reparent: remember the dragged node; the drop target records the
	// reparent, applied after the tree is drawn.
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		m_dragNode = node;
		const uint64_t id = node->getId();
		ImGui::SetDragDropPayload("EDITOR_NODE", &id, sizeof(id));
		ImGui::TextUnformatted(nodeLabel(*node).c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (ImGui::AcceptDragDropPayload("EDITOR_NODE"))
		{
			m_reparentChild = m_dragNode;
			m_reparentTarget = node;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem())
	{
		m_state.selected = node;
		if (ImGui::BeginMenu("Add Child"))
		{
			drawAddNodeMenu(node);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Delete"))
			m_nodeToDelete = node;
		ImGui::EndPopup();
	}

	if (open)
	{
		for (const auto &child : children)
			drawNodeTree(child);
		ImGui::TreePop();
	}
}

void SceneEditorUI::drawAddNodeMenu(const std::shared_ptr<engine::scene::nodes::Node> &parent)
{
	// Data-driven from the registry, so custom project node types appear here too.
	// "Model" is handled as a dedicated submenu (default primitives + custom).
	for (const auto *info : engine::scene::NodeTypeRegistry::instance().all())
	{
		if (info->typeName == "Model")
			continue;
		if (ImGui::MenuItem(info->displayName.c_str()))
		{
			m_addChildParent = parent;
			m_addChildType = info->typeName;
		}
	}

	if (ImGui::BeginMenu("Model"))
	{
		if (ImGui::BeginMenu("Defaults"))
		{
			if (ImGui::MenuItem("Sphere"))
			{
				m_addModelParent = parent;
				m_addModelPath = "resource://sphere.obj";
			}
			if (ImGui::MenuItem("Cube"))
			{
				m_addModelParent = parent;
				m_addModelPath = "resource://cube.obj";
			}
			if (ImGui::MenuItem("Cylinder"))
			{
				m_addModelParent = parent;
				m_addModelPath = "resource://cylinder.obj";
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Custom..."))
		{
			const std::string picked = pickFile("Select model", {"*.obj", "*.gltf", "*.glb", "*.fbx"}, "Model files",
				engine::core::PathProvider::getAssetRoot().string());
			if (!picked.empty())
			{
				// Import the mesh into the project (if external), then queue the add so
				// the scene stores a portable asset:// path.
				std::weak_ptr<engine::scene::nodes::Node> parentWeak = parent;
				requestImport(picked, "models", [this, parentWeak](const std::string &token)
				{
					m_addModelParent = parentWeak;
					m_addModelPath = token;
				});
			}
		}
		ImGui::EndMenu();
	}
}

void SceneEditorUI::applyDeferredHierarchyOps()
{
	if (!m_addChildType.empty())
	{
		if (auto parent = m_addChildParent.lock())
		{
			if (const auto *info = engine::scene::NodeTypeRegistry::instance().find(m_addChildType); info && info->factory)
			{
				auto child = info->factory();
				child->setName(info->displayName); // friendly default for a newly-added node
				parent->addChild(child, false);
				m_state.selected = child;
				m_state.sceneDirty = true;
			}
		}
		m_addChildType.clear();
		m_addChildParent.reset();
	}

	if (!m_addModelPath.empty())
	{
		if (auto parent = m_addModelParent.lock())
		{
			auto resources = m_engine.getResourceManager();
			const std::filesystem::path path = engine::core::PathProvider::resolveEnginePath(m_addModelPath);
			if (resources && resources->m_modelManager)
			{
				if (auto model = resources->m_modelManager->createModel(path))
				{
					auto child = std::make_shared<engine::scene::nodes::ModelRenderNode>(path);
					child->setModel(model.value()->getHandle());
					child->setName(path.stem().string());
					parent->addChild(child->asNode(), false);
					m_state.selected = child;
					m_state.sceneDirty = true;
				}
				else
				{
					m_assetStatus = "Failed to load model: " + m_addModelPath;
				}
			}
		}
		m_addModelPath.clear();
		m_addModelParent.reset();
	}

	if (auto node = m_replaceWithPlain.lock())
	{
		// Swap a placeholder for a plain Spatial node, keeping its transform,
		// name, and children but dropping the missing type's preserved data.
		if (auto *parent = node->getParent())
		{
			auto replacement = std::make_shared<engine::scene::nodes::SpatialNode>(node->getName().value_or("Node"));
			if (auto spatial = std::dynamic_pointer_cast<engine::scene::nodes::SpatialNode>(node))
			{
				auto &src = spatial->getTransform();
				replacement->getTransform().setLocalPosition(src.getLocalPosition());
				replacement->getTransform().setLocalEulerAngles(src.getLocalEulerAngles());
				replacement->getTransform().setLocalScale(src.getLocalScale());
			}
			const auto children = node->getChildren(); // copy: addChild mutates node's list
			for (const auto &child : children)
				replacement->addChild(child, false);
			parent->addChild(replacement, false);
			parent->removeChild(node);
			m_state.selected = replacement;
			m_state.sceneDirty = true;
		}
		m_replaceWithPlain.reset();
	}

	if (auto child = m_reparentChild.lock())
	{
		if (auto target = m_reparentTarget.lock(); target && target != child)
		{
			// addChild detaches from the old parent and rejects cycles, so a
			// drop onto self or a descendant is safely ignored.
			target->addChild(child, true);
			m_state.sceneDirty = true;
		}
		m_reparentChild.reset();
		m_reparentTarget.reset();
		m_dragNode.reset();
	}

	if (auto node = m_nodeToDelete.lock())
	{
		// Protect the editor camera so the viewport keeps rendering.
		if (node->getId() != m_state.editorCameraId)
		{
			if (auto *parent = node->getParent())
				parent->removeChild(node);
			if (m_state.selected.lock() == node)
				m_state.selected.reset();
			m_state.sceneDirty = true;
		}
		m_nodeToDelete.reset();
	}
}

void SceneEditorUI::drawInspector()
{
	using namespace engine::scene::nodes;
	using namespace engine::rendering;

	if (!m_showInspector)
		return;
	ImGui::Begin("Inspector", &m_showInspector);
	auto selected = m_state.selected.lock();
	if (!selected)
	{
		ImGui::TextDisabled("No node selected");
		ImGui::End();
		return;
	}

	std::string name = selected->getName().value_or("");
	if (ImGui::InputText("Name", &name))
	{
		selected->setName(name);
		m_state.sceneDirty = true;
	}
	bool enabled = selected->isEnabled();
	if (ImGui::Checkbox("Enabled", &enabled))
	{
		if (enabled)
			selected->enable();
		else
			selected->disable();
		m_state.sceneDirty = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("id %llu", static_cast<unsigned long long>(selected->getId()));
	ImGui::TextDisabled("Type: %s", nodeTypeName(*selected).c_str());
	ImGui::Separator();

	if (auto placeholder = std::dynamic_pointer_cast<PlaceholderNode>(selected))
	{
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Missing type: %s", placeholder->getMissingType().c_str());
		ImGui::TextWrapped("This node's type is not registered in this build. Its data is preserved and "
		                   "restored automatically when the scene is opened where the type exists.");
		if (ImGui::Button("Replace with Spatial Node"))
			m_replaceWithPlain = selected;
		ImGui::SameLine();
		if (ImGui::Button("Delete"))
			m_nodeToDelete = selected;
		ImGui::Separator();
	}

	if (auto spatial = selected->asSpatialNode())
	{
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto &transform = spatial->getTransform();
			glm::vec3 position = transform.getLocalPosition();
			glm::vec3 euler = transform.getLocalEulerAngles();
			glm::vec3 scale = transform.getLocalScale();
			if (ImGui::DragFloat3("Position", &position.x, 0.05f)) { transform.setLocalPosition(position); m_state.sceneDirty = true; }
			if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) { transform.setLocalEulerAngles(euler); m_state.sceneDirty = true; }
			if (ImGui::DragFloat3("Scale", &scale.x, 0.05f)) { transform.setLocalScale(scale); m_state.sceneDirty = true; }
		}
	}

	if (auto light = std::dynamic_pointer_cast<LightNode>(selected))
	{
		if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const char *typeNames[] = {"Ambient", "Directional", "Point", "Spot"};
			int currentType = static_cast<int>(light->getLightType());
			if (ImGui::Combo("Type", &currentType, typeNames, 4))
			{
				// Switch type but carry over the shared color + intensity.
				const glm::vec3 color = light->getColor();
				const float intensity = light->getIntensity();
				switch (currentType)
				{
				case 1: { DirectionalLight d; d.color = color; d.intensity = intensity; light->setLight(Light(d)); break; }
				case 2: { PointLight p;       p.color = color; p.intensity = intensity; light->setLight(Light(p)); break; }
				case 3: { SpotLight s;        s.color = color; s.intensity = intensity; light->setLight(Light(s)); break; }
				default: { AmbientLight a;    a.color = color; a.intensity = intensity; light->setLight(Light(a)); break; }
				}
				m_state.sceneDirty = true;
			}
			glm::vec3 color = light->getColor();
			if (ImGui::ColorEdit3("Color", &color.x)) { light->setColor(color); m_state.sceneDirty = true; }
			float intensity = light->getIntensity();
			if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 100.0f)) { light->setIntensity(intensity); m_state.sceneDirty = true; }
			if (light->getLightType() != Light::Type::Ambient)
			{
				bool castShadows = light->getCastShadows();
				if (ImGui::Checkbox("Cast Shadows", &castShadows)) { light->setCastShadows(castShadows); m_state.sceneDirty = true; }
			}
			bool changed = false;
			std::visit(
				[&](auto &l)
				{
					using T = std::decay_t<decltype(l)>;
					if constexpr (!std::is_same_v<T, AmbientLight>)
						changed |= ImGui::DragFloat("Range", &l.range, 0.1f, 0.1f, 1000.0f);
					if constexpr (std::is_same_v<T, SpotLight>)
						changed |= ImGui::DragFloat("Spot Angle", &l.spotAngle, 0.01f, 0.01f, 1.5f);
				},
				light->getLight().getData());
			if (changed)
				m_state.sceneDirty = true;
		}
	}

	if (auto camera = std::dynamic_pointer_cast<CameraNode>(selected))
	{
		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float fov = camera->getFov();
			if (ImGui::DragFloat("FOV", &fov, 0.5f, 10.0f, 170.0f)) { camera->setFov(fov); m_state.sceneDirty = true; }
			float nearPlane = camera->getNear();
			float farPlane = camera->getFar();
			if (ImGui::DragFloat("Near", &nearPlane, 0.01f, 0.001f, farPlane)) { camera->setNearFar(nearPlane, farPlane); m_state.sceneDirty = true; }
			if (ImGui::DragFloat("Far", &farPlane, 1.0f, nearPlane, 100000.0f)) { camera->setNearFar(nearPlane, farPlane); m_state.sceneDirty = true; }
			bool perspective = camera->isPerspective();
			if (ImGui::Checkbox("Perspective", &perspective)) { camera->setPerspective(perspective); m_state.sceneDirty = true; }
			glm::vec4 background = camera->getBackgroundColor();
			if (ImGui::ColorEdit4("Background", &background.x)) { camera->setBackgroundColor(background); m_state.sceneDirty = true; }

			ImGui::SeparatorText("Skybox / Environment");
			bool skybox = camera->isSkyboxEnabled();
			if (ImGui::Checkbox("Skybox", &skybox)) { camera->setSkyboxEnabled(skybox); m_state.sceneDirty = true; }

			// The environment map (HDR) drives both the skybox and image-based lighting.
			std::string envPath;
			if (auto env = camera->getEnvironmentTexture(); env && env->valid())
				if (auto tex = env->get(); tex && *tex && !(*tex)->getFilePath().empty())
					envPath = engine::core::PathProvider::toEnginePath((*tex)->getFilePath());
			ImGui::TextDisabled("Environment: %s", envPath.empty() ? "(none)" : envPath.c_str());
			{
				const char *hdrFilters[] = {"*.hdr", "*.exr", "*.png", "*.jpg"};
				const std::vector<const char *> filters(hdrFilters, hdrFilters + IM_ARRAYSIZE(hdrFilters));
				if (pathInput("cameraEnv", m_cameraEnvPathBuffer, "Environment map (.hdr) - or drop a file here",
						filters, engine::core::PathProvider::getAssetRoot().string()) && !m_cameraEnvPathBuffer.empty())
				{
					// Import the map into the project (if external) then apply it, so the
					// scene stores a portable asset:// token rather than an absolute path.
					const std::filesystem::path source = engine::core::PathProvider::resolveEnginePath(m_cameraEnvPathBuffer);
					std::weak_ptr<CameraNode> camWeak = camera;
					requestImport(source, "textures", [this, camWeak](const std::string &token)
					{
						auto cam = camWeak.lock();
						auto resources = m_engine.getResourceManager();
						if (!cam || !resources || !resources->m_textureManager)
							return;
						const std::filesystem::path path = engine::core::PathProvider::resolveEnginePath(token);
						if (auto tex = resources->m_textureManager->createTextureFromFile(path.string()))
						{
							cam->setEnvironmentTexture((*tex)->getHandle());
							m_state.sceneDirty = true;
							m_assetStatus = "Environment = " + token;
						}
						else
						{
							m_assetStatus = "Failed to load environment map";
						}
					});
				}
			}
			bool irradiance = camera->isIrradianceEnabled();
			if (ImGui::Checkbox("Image-based lighting (IBL)", &irradiance)) { camera->setIrradianceEnabled(irradiance); m_state.sceneDirty = true; }
			float iblIntensity = camera->getIrradianceIntensity();
			if (ImGui::DragFloat("IBL Intensity", &iblIntensity, 0.01f, 0.0f, 4.0f)) { camera->setIrradianceIntensity(iblIntensity); m_state.sceneDirty = true; }
		}
	}

	if (auto model = std::dynamic_pointer_cast<ModelRenderNode>(selected))
	{
		if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextUnformatted("Path:");
			ImGui::SameLine();
			if (model->getModelPath().empty())
				ImGui::TextDisabled("(none)");
			else
				pathLabel(model->getModelPath(), "modelPathMenu");
			int layer = static_cast<int>(model->getRenderLayer());
			if (ImGui::DragInt("Render Layer", &layer, 1.0f, 0, 64)) { model->setRenderLayer(static_cast<uint32_t>(std::max(0, layer))); m_state.sceneDirty = true; }
			ImGui::SeparatorText("Material");
			drawMaterialEditor(*model);
		}
	}

	// Reflected script fields: any node exposing reflect() fields shows them here,
	// driven by the same one-line-per-field list the game serializes.
	{
		CountReflector counter;
		selected->reflect(counter);
		if (counter.count > 0)
		{
			ImGui::SeparatorText("Fields");
			ImGuiReflector fields;
			selected->reflect(fields);
			if (fields.changed)
				m_state.sceneDirty = true;
		}
	}

	// A project script the editor did not compile is a PlaceholderNode: edit its
	// saved fields as raw JSON here so they still round-trip and reach the built
	// game (where the real type and its defaults live).
	if (auto placeholder = std::dynamic_pointer_cast<engine::scene::nodes::PlaceholderNode>(selected))
	{
		ImGui::SeparatorText(("Script: " + placeholder->getMissingType()).c_str());
		ImGui::TextDisabled("Type not compiled into the editor; fields are edited as raw\nvalues and take effect in the built game.");
		nlohmann::json props;
		try { props = nlohmann::json::parse(placeholder->getPreservedProps()); }
		catch (...) { props = nlohmann::json::object(); }
		bool changed = false;
		if (props.is_object())
		{
			for (auto it = props.begin(); it != props.end(); ++it)
			{
				const std::string key = it.key();
				nlohmann::json &val = it.value();
				ImGui::PushID(key.c_str());
				if (val.is_boolean())
				{
					bool b = val.get<bool>();
					if (ImGui::Checkbox(key.c_str(), &b)) { val = b; changed = true; }
				}
				else if (val.is_string())
				{
					std::string s = val.get<std::string>();
					if (ImGui::InputText(key.c_str(), &s)) { val = s; changed = true; }
				}
				else if (val.is_number())
				{
					float f = val.get<float>();
					if (ImGui::DragFloat(key.c_str(), &f, 0.05f)) { val = f; changed = true; }
				}
				else if (val.is_array() && val.size() >= 2 && val.size() <= 4 && val[0].is_number())
				{
					float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
					const int n = static_cast<int>(val.size());
					for (int i = 0; i < n; ++i) v[i] = val[i].get<float>();
					if (ImGui::DragScalarN(key.c_str(), ImGuiDataType_Float, v, n, 0.05f))
					{
						for (int i = 0; i < n; ++i) val[i] = v[i];
						changed = true;
					}
				}
				else
				{
					ImGui::TextDisabled("%s (unsupported type)", key.c_str());
				}
				ImGui::PopID();
			}
		}
		if (changed)
		{
			placeholder->setPreservedProps(props.dump());
			m_state.sceneDirty = true;
		}
	}

	ImGui::End();
}

void SceneEditorUI::drawMaterialEditor(engine::scene::nodes::ModelRenderNode &modelNode)
{
	using namespace engine::rendering;

	auto modelOpt = modelNode.getModel().get();
	if (!modelOpt)
	{
		ImGui::TextDisabled("Model not loaded yet");
		return;
	}
	auto &submeshes = (*modelOpt)->getSubmeshes();
	if (submeshes.empty())
	{
		ImGui::TextDisabled("Model has no submeshes");
		return;
	}

	auto resources = m_engine.getResourceManager();
	if (!resources || !resources->m_materialManager)
		return;

	// The inspector only SELECTS which material the node uses. A material owns its
	// shader, textures and properties - all edited in the Materials panel - so this
	// is a searchable picker plus shortcuts to create a new one or jump to editing.
	auto currentHandle = submeshes[0].material;
	auto currentOpt = currentHandle.valid() ? currentHandle.get() : std::optional<std::shared_ptr<Material>>{};
	const std::string currentName = (currentOpt && *currentOpt)
		? (*currentOpt)->getName().value_or("(unnamed)")
		: std::string();

	auto materials = resources->m_materialManager->getAll();
	std::sort(materials.begin(), materials.end(), [](const auto &a, const auto &b)
			  { return a->getName().value_or("") < b->getName().value_or(""); });
	std::vector<std::string> names;
	names.reserve(materials.size());
	for (const auto &material : materials)
		if (material)
			names.push_back(material->getName().value_or("(unnamed)"));

	std::string chosen;
	if (searchableCombo("Material", currentName, names, chosen))
	{
		for (const auto &material : materials)
		{
			if (material && material->getName().value_or("(unnamed)") == chosen)
			{
				for (auto &submesh : submeshes)
					submesh.material = material->getHandle();
				m_selectedMaterial = material;
				m_state.sceneDirty = true;
				break;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+ New"))
	{
		if (auto created = resources->m_materialManager->createPBRMaterial(m_newMaterialName, PBRProperties{}, {}))
		{
			(*created)->setShader(m_newMaterialShader);
			for (auto &submesh : submeshes)
				submesh.material = (*created)->getHandle();
			m_selectedMaterial = *created;
			m_showMaterials = true;
			m_state.sceneDirty = true;
		}
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Create a new PBR material and assign it to this node");

	if (currentOpt && *currentOpt)
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("Edit"))
		{
			m_selectedMaterial = *currentOpt;
			m_showMaterials = true;
		}
		ImGui::TextDisabled("Shader: %s", (*currentOpt)->getShader().c_str());
	}
	else
	{
		ImGui::TextDisabled("No material - pick one above or create a new one.");
	}
}

void SceneEditorUI::pathLabel(const std::filesystem::path &absolutePath, const char *id)
{
	const std::string enginePath = engine::core::PathProvider::toEnginePath(absolutePath);
	ImGui::TextUnformatted(enginePath.c_str());
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Right-click to copy");
	if (ImGui::BeginPopupContextItem(id))
	{
		copyPathMenuItems(absolutePath);
		ImGui::EndPopup();
	}
}

void SceneEditorUI::requestImport(const std::filesystem::path &source, const std::string &defaultSubdir, std::function<void(const std::string &)> apply)
{
	using engine::core::PathProvider;
	std::error_code ec;
	if (source.empty() || !std::filesystem::exists(source, ec))
	{
		m_assetStatus = "File not found: " + source.string();
		return;
	}
	// Already under a known root: reference it by its token directly, no prompt.
	if (PathProvider::isUnderAssets(source) || PathProvider::isUnderResources(source))
	{
		if (apply)
			apply(PathProvider::toEnginePath(source));
		return;
	}
	// Outside the project: offer to import it (the "Import Into Project" modal).
	m_pendingImportSource = source;
	m_pendingImportSubdir = defaultSubdir;
	m_pendingImportApply = std::move(apply);
	m_openImportPopup = true;
}

void SceneEditorUI::requestTextureAssign(const std::shared_ptr<engine::rendering::Material> &material, const char *slot, const std::string &pathOrToken)
{
	using engine::core::PathProvider;
	if (!material || pathOrToken.empty())
		return;

	const bool isToken = pathOrToken.rfind(PathProvider::kAssetScheme, 0) == 0 ||
						 pathOrToken.rfind(PathProvider::kResourceScheme, 0) == 0;
	const std::filesystem::path source = isToken ? PathProvider::resolveEnginePath(pathOrToken) : std::filesystem::path(pathOrToken);

	const std::string slotName = slot;
	requestImport(source, "textures", [this, material, slotName](const std::string &token)
	{
		auto resources = m_engine.getResourceManager();
		if (!resources || !resources->m_textureManager)
			return;
		const std::filesystem::path path = engine::core::PathProvider::resolveEnginePath(token);
		if (auto texture = resources->m_textureManager->createTextureFromFile(path.string()))
		{
			material->setTexture(slotName, (*texture)->getHandle(), engine::rendering::defaultColorSpaceForSlot(slotName));
			m_state.sceneDirty = true;
			m_assetStatus = slotName + " = " + token;
		}
		else
		{
			m_assetStatus = "Failed to load texture: " + path.filename().string();
		}
	});
}

void SceneEditorUI::drawFileGrid(const std::filesystem::path &root, bool editable)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::exists(m_assetBrowsePath, ec))
	{
		ImGui::TextDisabled("(missing) %s", m_assetBrowsePath.generic_string().c_str());
		return;
	}

	// Cached listing for the current folder (shares the browser generation).
	DirListing &listing = m_dirCache[m_assetBrowsePath.generic_string()];
	if (listing.generation != m_browserGeneration)
	{
		listing.dirs.clear();
		listing.files.clear();
		for (fs::directory_iterator it(m_assetBrowsePath, ec), end; it != end && !ec; it.increment(ec))
		{
			std::error_code entryEc;
			if (it->is_directory(entryEc))
				listing.dirs.push_back(it->path());
			else
				listing.files.push_back(it->path());
		}
		std::sort(listing.dirs.begin(), listing.dirs.end());
		std::sort(listing.files.begin(), listing.files.end());
		listing.generation = m_browserGeneration;
	}

	// Combined order (dirs first, then files) backs shift-range selection.
	std::vector<fs::path> ordered;
	ordered.reserve(listing.dirs.size() + listing.files.size());
	ordered.insert(ordered.end(), listing.dirs.begin(), listing.dirs.end());
	ordered.insert(ordered.end(), listing.files.begin(), listing.files.end());

	auto isSelected = [&](const fs::path &p)
	{ return std::find(m_assetSelection.begin(), m_assetSelection.end(), p) != m_assetSelection.end(); };
	auto selectOnly = [&](const fs::path &p)
	{
		m_assetSelection.assign(1, p);
		m_selectionAnchor = p;
	};
	auto onClick = [&](const fs::path &p)
	{
		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyShift && !m_selectionAnchor.empty())
		{
			auto indexOf = [&](const fs::path &q) -> int
			{
				for (size_t i = 0; i < ordered.size(); ++i)
					if (ordered[i] == q) return static_cast<int>(i);
				return -1;
			};
			int a = indexOf(m_selectionAnchor);
			int b = indexOf(p);
			if (a < 0 || b < 0) { selectOnly(p); return; }
			if (a > b) std::swap(a, b);
			m_assetSelection.clear();
			for (int i = a; i <= b; ++i)
				m_assetSelection.push_back(ordered[i]);
		}
		else if (io.KeyCtrl)
		{
			auto it = std::find(m_assetSelection.begin(), m_assetSelection.end(), p);
			if (it != m_assetSelection.end())
				m_assetSelection.erase(it);
			else
				m_assetSelection.push_back(p);
			m_selectionAnchor = p;
		}
		else
		{
			selectOnly(p);
		}
	};

	ImGui::BeginChild("##assetGrid", ImVec2(0.0f, 0.0f), true);
	const float cell = m_assetIconSize;
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float availW = ImGui::GetContentRegionAvail().x;
	const int perRow = std::max(1, static_cast<int>((availW + spacing) / (cell + spacing)));
	const int maxChars = std::max(4, static_cast<int>(cell / 7.0f));

	int columnIndex = 0;
	const auto drawEntry = [&](const fs::path &path, bool isDir)
	{
		ImGui::PushID(path.string().c_str());
		ImGui::BeginGroup();

		const bool sel = isSelected(path);
		if (sel)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.45f, 0.78f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.52f, 0.86f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.40f, 0.72f, 1.0f));
		}
		ImGui::Button("##icon", ImVec2(cell, cell));
		if (sel)
			ImGui::PopStyleColor(3);

		const bool hovered = ImGui::IsItemHovered();
		const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		const ImVec2 mn = ImGui::GetItemRectMin();
		const ImVec2 mx = ImGui::GetItemRectMax();
		const ImVec2 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
		if (isDir)
			drawFolderGlyph(ImGui::GetWindowDrawList(), center, cell * 0.28f, IM_COL32(232, 196, 110, 255));
		else
			drawFileGlyph(ImGui::GetWindowDrawList(), center, cell * 0.28f, ImGui::GetColorU32(ImGuiCol_Text));

		// Drag a file out (to assign to an input field / texture slot elsewhere).
		if (!isDir && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
		{
			const std::string enginePath = engine::core::PathProvider::toEnginePath(path);
			ImGui::SetDragDropPayload("ASSET_PATH", enginePath.c_str(), enginePath.size() + 1);
			ImGui::TextUnformatted(enginePath.c_str());
			ImGui::EndDragDropSource();
		}
		// Drop onto a folder to move (within assets) or import (from resources) into it.
		if (isDir && editable && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
			{
				const std::string enginePath(static_cast<const char *>(payload->Data));
				const fs::path src = engine::core::PathProvider::resolveEnginePath(enginePath);
				const fs::path dest = path / src.filename();
				std::error_code moveEc;
				if (engine::core::PathProvider::isUnderAssets(src))
					fs::rename(src, dest, moveEc);
				else
					fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, moveEc);
				m_assetStatus = moveEc ? ("Move failed: " + moveEc.message())
									   : ("Moved into " + path.filename().string());
				++m_browserGeneration;
			}
			ImGui::EndDragDropTarget();
		}

		if (doubleClicked)
		{
			if (isDir)
				assetNavigateTo(path);
			else if (editable)
				showInExplorer(path);
			else
				openExternally(path, m_externalEditor == 1);
		}
		else if (clicked)
		{
			onClick(path);
		}

		if (ImGui::BeginPopupContextItem("entryctx"))
		{
			if (!isSelected(path))
				selectOnly(path);
			const bool multi = m_assetSelection.size() > 1;
			if (!isDir && !multi && ImGui::MenuItem("Open"))
				openExternally(path, m_externalEditor == 1);
			if (ImGui::MenuItem("Copy"))
			{
				m_clipboard = m_assetSelection;
				m_clipboardCut = false;
			}
			if (editable && ImGui::MenuItem("Cut"))
			{
				m_clipboard = m_assetSelection;
				m_clipboardCut = true;
			}
			if (isDir && editable && !m_clipboard.empty() && ImGui::MenuItem("Paste Into"))
				pasteClipboard(path);
			if (editable && !multi && ImGui::MenuItem("Rename"))
			{
				m_pendingRenameSource = path;
				m_renameBuffer = path.filename().string();
				m_openRenamePopup = true;
			}
			if (editable && ImGui::MenuItem(multi ? "Delete Selected" : "Delete"))
			{
				m_pendingDeletes = m_assetSelection;
				m_openDeletePopup = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Show in Explorer"))
				showInExplorer(path);
			copyPathMenuItems(path);
			ImGui::EndPopup();
		}

		if (hovered)
			ImGui::SetTooltip("%s", path.filename().string().c_str());

		std::string name = path.filename().string();
		if (static_cast<int>(name.size()) > maxChars)
			name = name.substr(0, maxChars - 1) + "~";
		const float offset = (cell - ImGui::CalcTextSize(name.c_str()).x) * 0.5f;
		if (offset > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
		ImGui::TextUnformatted(name.c_str());
		ImGui::EndGroup();
		ImGui::PopID();

		if (++columnIndex % perRow != 0)
			ImGui::SameLine();
	};

	for (const auto &dir : listing.dirs)
		drawEntry(dir, true);
	for (const auto &file : listing.files)
		drawEntry(file, false);

	// Click empty space clears the selection.
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
		m_assetSelection.clear();

	// Empty-area context menu: paste / new folder / reveal.
	if (ImGui::BeginPopupContextWindow("assetWindowCtx",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (editable && !m_clipboard.empty() && ImGui::MenuItem("Paste"))
			pasteClipboard(m_assetBrowsePath);
		if (editable && ImGui::MenuItem("New Folder"))
		{
			const fs::path base = m_assetBrowsePath / "New Folder";
			fs::path dir = base;
			int n = 1;
			std::error_code mkEc;
			while (fs::exists(dir, mkEc))
				dir = fs::path(base.string() + " " + std::to_string(++n));
			fs::create_directory(dir, mkEc);
			++m_browserGeneration;
		}
		if (ImGui::MenuItem("Show in Explorer"))
			showInExplorer(m_assetBrowsePath);
		ImGui::EndPopup();
	}

	ImGui::EndChild();
}

void SceneEditorUI::drawLog()
{
	if (!m_showLog)
		return;
	ImGui::Begin("Log", &m_showLog);
	if (!m_logStore)
	{
		ImGui::TextDisabled("Log capture is not available.");
		ImGui::End();
		return;
	}

	// Toolbar: minimum level, text filter, clear, copy, auto-scroll.
	const char *levelNames[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical"};
	ImGui::SetNextItemWidth(110.0f);
	ImGui::Combo("Level", &m_logMinLevel, levelNames, IM_ARRAYSIZE(levelNames));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##logFilter", "filter...", &m_logFilter);
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		m_logStore->clear();
		m_logSelectedSeq = 0;
	}
	ImGui::SameLine();
	const bool copyRequested = ImGui::Button("Copy");
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &m_logAutoScroll);

	m_logStore->copyInto(m_logCache);

	const auto toLower = [](std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	};
	const std::string needle = toLower(m_logFilter);
	const auto matches = [&](const LogEntry &entry) {
		if (entry.level < m_logMinLevel)
			return false;
		if (needle.empty())
			return true;
		return toLower(entry.message).find(needle) != std::string::npos || toLower(entry.logger).find(needle) != std::string::npos;
	};

	std::vector<int> rows;
	rows.reserve(m_logCache.size());
	for (int i = 0; i < static_cast<int>(m_logCache.size()); ++i)
		if (matches(m_logCache[i]))
			rows.push_back(i);

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
		ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##logTable", 4, tableFlags))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 92.0f, 0);
		ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 52.0f, 1);
		ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthFixed, 120.0f, 2);
		ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 0.0f, 3);
		ImGui::TableHeadersRow();

		// Sort the filtered index list by the active column; ties keep time order.
		if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0)
		{
			const ImGuiTableColumnSortSpecs &spec = sortSpecs->Specs[0];
			const bool ascending = spec.SortDirection != ImGuiSortDirection_Descending;
			std::sort(rows.begin(), rows.end(), [&](int a, int b) {
				const LogEntry &ea = m_logCache[a];
				const LogEntry &eb = m_logCache[b];
				int cmp = 0;
				switch (spec.ColumnUserID)
				{
				case 1: cmp = ea.level - eb.level; break;
				case 2: cmp = ea.logger.compare(eb.logger); break;
				case 3: cmp = ea.message.compare(eb.message); break;
				default: cmp = (ea.seq < eb.seq) ? -1 : (ea.seq > eb.seq ? 1 : 0); break;
				}
				if (cmp == 0)
					cmp = (ea.seq < eb.seq) ? -1 : 1;
				return ascending ? (cmp < 0) : (cmp > 0);
			});
		}

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows.size()));
		while (clipper.Step())
		{
			for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
			{
				const LogEntry &entry = m_logCache[rows[r]];
				ImGui::TableNextRow();
				ImGui::PushID(r);

				ImGui::TableSetColumnIndex(0);
				const bool selected = (m_logSelectedSeq == entry.seq);
				if (ImGui::Selectable("##logrow", selected, ImGuiSelectableFlags_SpanAllColumns))
					m_logSelectedSeq = entry.seq;
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Copy message"))
						ImGui::SetClipboardText(entry.message.c_str());
					if (ImGui::MenuItem("Copy line"))
					{
						const std::string line = entry.time + "  [" + logLevelShort(entry.level) + "] " +
							(entry.logger.empty() ? std::string() : entry.logger + ": ") + entry.message;
						ImGui::SetClipboardText(line.c_str());
					}
					ImGui::EndPopup();
				}
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::TextUnformatted(entry.time.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(logLevelColor(entry.level), "%s", logLevelShort(entry.level));

				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(entry.logger.c_str());

				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(entry.message.c_str());

				ImGui::PopID();
			}
		}

		// Stick to the newest line only when already scrolled to the bottom.
		if (m_logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndTable();
	}

	if (copyRequested)
	{
		std::string out;
		for (int idx : rows)
		{
			const LogEntry &entry = m_logCache[idx];
			out += entry.time;
			out += "  [";
			out += logLevelShort(entry.level);
			out += "] ";
			if (!entry.logger.empty())
			{
				out += entry.logger;
				out += ": ";
			}
			out += entry.message;
			out += "\n";
		}
		ImGui::SetClipboardText(out.c_str());
	}

	ImGui::End();
}

void SceneEditorUI::drawSettings()
{
	if (!m_showSettings)
		return;
	ImGui::Begin("Settings", &m_showSettings);

	ImGui::SeparatorText("Project");
	ImGui::InputText("Name", &m_project.name);
	{
		const char *preview = m_project.startupScene.empty() ? "(first scene)" : m_project.startupScene.c_str();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("Startup scene", preview))
		{
			for (const auto &scenePath : m_project.scenes)
				if (ImGui::Selectable(scenePath.c_str(), scenePath == m_project.startupScene))
					m_project.startupScene = scenePath;
			ImGui::EndCombo();
		}
	}
	ImGui::TextDisabled("%d scene(s) in project", static_cast<int>(m_project.scenes.size()));
	ImGui::Checkbox("Override engine settings for the current scene", &m_overrideSceneSettings);
	ImGui::TextDisabled("Apply pushes to the running engine and stores to the %s.",
		m_overrideSceneSettings ? "current scene" : "project");

	ImGui::SeparatorText("Frame");
	ImGui::Checkbox("VSync", &m_settings.enableVSync);
	ImGui::Checkbox("Limit frame rate", &m_settings.limitFrameRate);
	ImGui::DragFloat("Target FPS", &m_settings.targetFrameRate, 1.0f, 15.0f, 480.0f, "%.0f");
	ImGui::Checkbox("Show frame stats", &m_settings.showFrameStats);

	ImGui::SeparatorText("Physics");
	ImGui::Checkbox("Run physics", &m_settings.runPhysics);
	ImGui::DragInt("Max substeps", &m_settings.maxSubSteps, 1.0f, 1, 20);

	ImGui::SeparatorText("Rendering");
	const char *msaaItems[] = {"Off", "2x", "4x", "8x"};
	const int msaaValues[] = {1, 2, 4, 8};
	int msaaIndex = (m_settings.msaaSampleCount >= 8) ? 3 : (m_settings.msaaSampleCount >= 4) ? 2 : (m_settings.msaaSampleCount >= 2) ? 1 : 0;
	if (ImGui::Combo("MSAA", &msaaIndex, msaaItems, IM_ARRAYSIZE(msaaItems)))
		m_settings.msaaSampleCount = msaaValues[msaaIndex];

	ImGui::SeparatorText("Window");
	ImGui::Checkbox("Fullscreen", &m_settings.fullscreen);
	ImGui::Checkbox("Resizable", &m_settings.resizableWindow);
	ImGui::DragInt("Width", &m_settings.windowWidth, 1.0f, 320, 7680);
	ImGui::DragInt("Height", &m_settings.windowHeight, 1.0f, 240, 4320);

	ImGui::SeparatorText("Audio");
	ImGui::Checkbox("Enable audio", &m_settings.enableAudio);
	ImGui::SliderFloat("Master volume", &m_settings.masterVolume, 0.0f, 1.0f);

	ImGui::Separator();
	if (ImGui::Button("Apply"))
	{
		m_engine.setOptions(m_settings);
		auto sceneManager = m_engine.getSceneManager();
		auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
		if (m_overrideSceneSettings)
		{
			if (scene)
				scene->setSettingsOverride(engine::settings::toJson(m_settings).dump());
		}
		else
		{
			m_project.settings = m_settings;
			if (scene)
				scene->setSettingsOverride(""); // inherit the project settings
		}
		m_state.sceneDirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Revert"))
		m_settings = m_engine.getOptions();

	ImGui::End();
}

void SceneEditorUI::drawShaders()
{
	if (!m_showShaders)
		return;
	ImGui::Begin("Shaders", &m_showShaders);

	auto context = m_engine.getContext();
	auto resources = m_engine.getResourceManager();
	if (!context || !resources || !resources->m_materialManager)
	{
		ImGui::TextDisabled("Engine not ready");
		ImGui::End();
		return;
	}

	using namespace engine::rendering;
	const std::vector<std::string> shaderNames = context->shaderRegistry().getShaderNames();

	// ---- Shaders -------------------------------------------------------------
	if (ImGui::CollapsingHeader("Shaders", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginListBox("##shaderlist", ImVec2(-1.0f, 110.0f)))
		{
			for (const auto &name : shaderNames)
			{
				ImGui::PushID(name.c_str());
				ImGui::Selectable(name.c_str(), false);
				if (ImGui::BeginPopupContextItem("shaderctx"))
				{
					const std::filesystem::path wgsl =
						engine::core::PathProvider::getResource("shaders/" + name + ".wgsl");
					std::error_code ec;
					if (std::filesystem::exists(wgsl, ec))
					{
						if (ImGui::MenuItem("Open .wgsl"))
							openExternally(wgsl, m_externalEditor == 1);
						copyPathMenuItems(wgsl);
					}
					else
					{
						ImGui::TextDisabled("(no .wgsl on disk)");
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		ImGui::SeparatorText("New Shader");
		ImGui::TextDisabled("Starts from a known-good template (engine includes + lighting\nalready filled in) and registers it in the shader picker.");
		ImGui::InputText("Name##shader", &m_newShaderName);
		ImGui::RadioButton("Material##st", &m_newShaderType, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Post-process##st", &m_newShaderType, 1);
		if (ImGui::Button("Create Shader") && !m_newShaderName.empty())
		{
			namespace fs = std::filesystem;
			const bool material = (m_newShaderType == 0);
			// Templates ARE the known-good engine shaders, copied to a new file -
			// valid by construction; the user customizes from there.
			const fs::path reference = engine::core::PathProvider::getResource(
				material ? "shaders/PBR_Lit_Shader.wgsl" : "shaders/postprocess_vignette.wgsl");
			const fs::path target = engine::core::PathProvider::getResource("shaders/" + m_newShaderName + ".wgsl");

			std::error_code ec;
			fs::copy_file(reference, target, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				m_shaderStatus = "Copy failed: " + ec.message();
			}
			else
			{
				webgpu::ShaderDescriptor desc;
				desc.name = m_newShaderName;
				desc.path = target;
				if (material)
				{
					// Mirror the engine PBR material-slot overlay so the reflected
					// @group(2) textures map to the right material slots.
					desc.type = ShaderType::Lit;
					webgpu::BindGroupMeta mtl;
					mtl.bindings[2] = {MaterialTextureSlots::DIFFUSE,   glm::vec3(1.0f, 1.0f, 1.0f)};
					mtl.bindings[3] = {MaterialTextureSlots::NORMAL,    glm::vec3(0.5f, 0.5f, 1.0f)};
					mtl.bindings[4] = {MaterialTextureSlots::AMBIENT,   glm::vec3(1.0f, 1.0f, 1.0f)};
					mtl.bindings[5] = {MaterialTextureSlots::ROUGHNESS, glm::vec3(1.0f, 1.0f, 1.0f)};
					mtl.bindings[6] = {MaterialTextureSlots::METALLIC,  glm::vec3(0.0f, 0.0f, 0.0f)};
					mtl.bindings[7] = {MaterialTextureSlots::EMISSIVE,  glm::vec3(0.0f, 0.0f, 0.0f)};
					desc.groups[2] = mtl;
				}
				else
				{
					// Post-process: input texture + sampler at @group(4).
					desc.type = ShaderType::Unlit;
					desc.vertexLayout = VertexLayout::None;
					desc.enableDepth = false;
					desc.cullBackFaces = false;
					desc.groups[4] = {m_newShaderName + "_Input", BindGroupType::Custom, BindGroupReuse::PerFrame, {}};
				}

				auto info = context->shaderFactory().buildFromDescriptor(desc);
				if (info && info->isValid())
				{
					context->shaderRegistry().registerShader(info, true);
					m_shaderStatus = "Created '" + m_newShaderName + "' - now in the shader picker. Edit " + target.filename().string() + " to customize.";
				}
				else
				{
					m_shaderStatus = "Shader build/validation failed - see the Log";
				}
			}
		}
		if (!m_shaderStatus.empty())
			ImGui::TextWrapped("%s", m_shaderStatus.c_str());
	}

	ImGui::End();
}

void SceneEditorUI::drawMaterials()
{
	if (!m_showMaterials)
		return;
	ImGui::Begin("Materials", &m_showMaterials);

	using namespace engine::rendering;
	auto resources = m_engine.getResourceManager();
	auto context = m_engine.getContext();
	if (!resources || !resources->m_materialManager || !resources->m_textureManager || !context)
	{
		ImGui::TextDisabled("Engine not ready");
		ImGui::End();
		return;
	}

	const std::vector<std::string> shaderNames = context->shaderRegistry().getShaderNames();

	// Material list (filterable).
	auto materials = resources->m_materialManager->getAll();
	std::sort(materials.begin(), materials.end(), [](const auto &a, const auto &b)
			  { return a->getName().value_or("") < b->getName().value_or(""); });

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##matfilter", "filter materials...", &m_materialFilter);
	const std::string needle = toLowerCopy(m_materialFilter);

	auto selected = m_selectedMaterial.lock();
	if (ImGui::BeginListBox("##matlist", ImVec2(-1.0f, 120.0f)))
	{
		for (const auto &material : materials)
		{
			if (!material)
				continue;
			const std::string name = material->getName().value_or("(unnamed)");
			if (!needle.empty() && toLowerCopy(name).find(needle) == std::string::npos)
				continue;
			ImGui::PushID(material.get());
			if (ImGui::Selectable((name + "   [" + material->getShader() + "]").c_str(), material == selected))
			{
				m_selectedMaterial = material;
				selected = material;
			}
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}

	// Create a new material (it starts with the chosen shader assigned).
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("Name##newmat", &m_newMaterialName);
	std::string chosenNewShader;
	if (shaderCombo("Shader##newmat", m_newMaterialShader, shaderNames, chosenNewShader))
		m_newMaterialShader = chosenNewShader;
	if (ImGui::Button("New PBR") && !m_newMaterialName.empty())
	{
		if (auto created = resources->m_materialManager->createPBRMaterial(m_newMaterialName, PBRProperties{}, {}))
		{
			(*created)->setShader(m_newMaterialShader);
			m_selectedMaterial = *created;
			selected = *created;
			m_state.sceneDirty = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("New Unlit") && !m_newMaterialName.empty())
	{
		if (auto created = resources->m_materialManager->createMaterial<UnlitProperties>(m_newMaterialName, UnlitProperties{}, m_newMaterialShader, {}))
		{
			m_selectedMaterial = *created;
			selected = *created;
			m_state.sceneDirty = true;
		}
	}

	ImGui::Separator();

	if (!selected)
	{
		ImGui::TextDisabled("Select or create a material to edit it.");
		ImGui::End();
		return;
	}

	// Edit the selected material: name, the shader it uses, properties, textures.
	Material &mat = *selected;

	std::string name = mat.getName().value_or("");
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputText("Name", &name))
	{
		mat.setName(name);
		m_state.sceneDirty = true;
	}

	// A material USES a shader - it is assigned to the material here, not the
	// other way around.
	std::string chosenShader;
	if (shaderCombo("Shader", mat.getShader(), shaderNames, chosenShader))
	{
		mat.setShader(chosenShader);
		m_state.sceneDirty = true;
	}

	if (mat.getPropertiesType() == std::type_index(typeid(PBRProperties)))
	{
		PBRProperties props = mat.getProperties<PBRProperties>();
		bool changed = false;
		changed |= ImGui::ColorEdit4("Base Color", &props.diffuse.x);
		changed |= ImGui::DragFloat("Metallic", &props.metallic, 0.01f, 0.0f, 1.0f);
		changed |= ImGui::DragFloat("Roughness", &props.roughness, 0.01f, 0.0f, 1.0f);
		if (changed)
		{
			mat.setProperties(props);
			m_state.sceneDirty = true;
		}
	}
	else if (mat.getPropertiesType() == std::type_index(typeid(UnlitProperties)))
	{
		UnlitProperties props = mat.getProperties<UnlitProperties>();
		if (ImGui::ColorEdit4("Color", &props.color.x))
		{
			mat.setProperties(props);
			m_state.sceneDirty = true;
		}
	}

	// Texture slots come from the chosen shader's @group(Material) layout, so the
	// rows always match whatever textures that shader declares.
	ImGui::SeparatorText("Textures");
	const char *imageFilters[] = {"*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp", "*.hdr", "*.dds", "*.ktx", "*.ktx2"};
	std::vector<std::string> slots;
	if (auto shaderInfo = context->shaderRegistry().getShader(mat.getShader()))
		if (auto materialLayout = shaderInfo->getBindGroupLayout(BindGroupType::Material))
			for (const auto &binding : materialLayout->getBindings())
				if (binding.type == BindingType::MaterialTexture && binding.materialSlotName && !binding.materialSlotName->empty())
					slots.push_back(*binding.materialSlotName);

	if (slots.empty())
	{
		ImGui::TextDisabled("This shader declares no material textures.");
	}
	else
	{
		const std::vector<const char *> filters(imageFilters, imageFilters + IM_ARRAYSIZE(imageFilters));
		const std::string assetDir = engine::core::PathProvider::getAssetRoot().string();
		for (const auto &slot : slots)
		{
			ImGui::PushID(slot.c_str());
			ImGui::Text("%s", slot.c_str());
			ImGui::SameLine(150.0f);
			ImGui::TextDisabled(mat.hasTexture(slot) ? "set" : "none");
			if (mat.hasTexture(slot))
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Clear"))
				{
					mat.removeTexture(slot);
					m_state.sceneDirty = true;
				}
			}
			if (pathInput(slot.c_str(), m_texturePathBuffers[slot],
					"Texture path, asset:// / resource://, or drop a file here", filters, assetDir))
			{
				requestTextureAssign(selected, slot.c_str(), m_texturePathBuffers[slot]);
				m_texturePathBuffers[slot] = engine::core::PathProvider::toEnginePath(
					engine::core::PathProvider::resolveEnginePath(m_texturePathBuffers[slot]));
			}
			ImGui::PopID();
		}
	}
	if (!m_assetStatus.empty())
		ImGui::TextWrapped("%s", m_assetStatus.c_str());

	ImGui::Separator();
	auto selectedNode = m_state.selected.lock();
	auto modelNode = std::dynamic_pointer_cast<engine::scene::nodes::ModelRenderNode>(selectedNode);
	ImGui::BeginDisabled(!modelNode);
	if (ImGui::Button("Assign to Selected Node"))
	{
		if (auto modelOpt = modelNode->getModel().get())
		{
			for (auto &submesh : (*modelOpt)->getSubmeshes())
				submesh.material = selected->getHandle();
			m_state.sceneDirty = true;
			m_assetStatus = "Assigned '" + selected->getName().value_or("material") + "' to " +
				selectedNode->getName().value_or("node");
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Delete Material"))
	{
		resources->m_materialManager->remove(selected);
		m_selectedMaterial.reset();
		m_state.sceneDirty = true;
	}
	if (!modelNode)
		ImGui::TextDisabled("Select a model node in the Hierarchy to assign this material to it.");

	ImGui::End();
}

void SceneEditorUI::assetNavigateTo(const std::filesystem::path &target)
{
	if (target == m_assetBrowsePath)
		return;
	m_navBack.push_back(m_assetBrowsePath);
	m_navForward.clear();
	m_assetBrowsePath = target;
	m_assetSelection.clear();
	m_selectionAnchor.clear();
}

void SceneEditorUI::pasteClipboard(const std::filesystem::path &destFolder)
{
	namespace fs = std::filesystem;
	for (const auto &src : m_clipboard)
	{
		std::error_code ec;
		if (!fs::exists(src, ec))
			continue;
		fs::path dest = destFolder / src.filename();
		// Copying into the same folder would clobber the source; make a copy name.
		if (!m_clipboardCut && dest == src)
			dest = destFolder / (src.stem().string() + " copy" + src.extension().string());
		if (m_clipboardCut)
			fs::rename(src, dest, ec);
		else if (fs::is_directory(src, ec))
			fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
		else
			fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
		if (ec)
			m_assetStatus = "Paste failed: " + ec.message();
	}
	if (m_clipboardCut)
	{
		m_clipboard.clear();
		m_clipboardCut = false;
	}
	m_assetSelection.clear();
	++m_browserGeneration;
}

void SceneEditorUI::drawAssets()
{
	if (!m_showAssets)
		return;
	ImGui::Begin("Assets", &m_showAssets);

	const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// Root selector: engine resources (read-only) vs project assets (editable).
	const int previousRoot = m_assetGridRoot;
	ImGui::RadioButton("Resources", &m_assetGridRoot, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Assets", &m_assetGridRoot, 1);
	const bool editable = (m_assetGridRoot == 1);
	const std::filesystem::path root = editable
		? engine::core::PathProvider::getAssetRoot()
		: engine::core::PathProvider::getResourceRoot();
	if (m_assetGridRoot != previousRoot)
	{
		// Switching roots resets navigation into the new root.
		m_navBack.clear();
		m_navForward.clear();
		m_assetBrowsePath = root;
		m_assetSelection.clear();
	}

	// Keep the current folder inside the selected root (also covers first frame).
	{
		const std::string rel = m_assetBrowsePath.empty()
			? std::string()
			: m_assetBrowsePath.lexically_relative(root).generic_string();
		if (m_assetBrowsePath.empty() || rel.empty() || rel.rfind("..", 0) == 0)
			m_assetBrowsePath = root;
	}

	// Navigation: Back / Forward / Up + breadcrumb.
	ImGui::BeginDisabled(m_navBack.empty());
	if (ImGui::ArrowButton("##back", ImGuiDir_Left))
	{
		m_navForward.push_back(m_assetBrowsePath);
		m_assetBrowsePath = m_navBack.back();
		m_navBack.pop_back();
		m_assetSelection.clear();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back");
	ImGui::SameLine();
	ImGui::BeginDisabled(m_navForward.empty());
	if (ImGui::ArrowButton("##fwd", ImGuiDir_Right))
	{
		m_navBack.push_back(m_assetBrowsePath);
		m_assetBrowsePath = m_navForward.back();
		m_navForward.pop_back();
		m_assetSelection.clear();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward");
	ImGui::SameLine();
	ImGui::BeginDisabled(m_assetBrowsePath == root);
	if (ImGui::ArrowButton("##up", ImGuiDir_Up))
		assetNavigateTo(m_assetBrowsePath.parent_path());
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up");
	ImGui::SameLine();
	ImGui::TextUnformatted(editable ? "Assets" : "Resources");
	const std::string crumb = m_assetBrowsePath.lexically_relative(root).generic_string();
	if (!crumb.empty() && crumb != ".")
	{
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextDisabled("/%s", crumb.c_str());
	}

	// Right-aligned view controls (reload, auto-refresh, icon size, open-with).
	ImGui::SameLine();
	const float controlsWidth = 360.0f;
	const float lineAvail = ImGui::GetContentRegionAvail().x;
	if (lineAvail > controlsWidth)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (lineAvail - controlsWidth));
	if (ImGui::SmallButton("Reload"))
		++m_browserGeneration;
	ImGui::SameLine();
	ImGui::Checkbox("Auto", &m_browserAutoRefresh);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-refresh folder listing");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::SliderFloat("##icon", &m_assetIconSize, 48.0f, 144.0f, "%.0f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	const char *editors[] = {"OS default", "VS Code"};
	ImGui::Combo("##openwith", &m_externalEditor, editors, IM_ARRAYSIZE(editors));
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open files with");

	if (m_browserAutoRefresh)
	{
		const double now = ImGui::GetTime();
		if (now - m_browserLastRefresh > 2.0)
		{
			++m_browserGeneration;
			m_browserLastRefresh = now;
		}
	}

	// Keyboard shortcuts, active only while the Assets panel is focused and not typing.
	if (focused && !ImGui::GetIO().WantTextInput)
	{
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && m_assetBrowsePath != root)
			assetNavigateTo(m_assetBrowsePath.parent_path());
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !m_assetSelection.empty())
		{
			m_clipboard = m_assetSelection;
			m_clipboardCut = false;
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && editable && !m_assetSelection.empty())
		{
			m_clipboard = m_assetSelection;
			m_clipboardCut = true;
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && editable && !m_clipboard.empty())
			pasteClipboard(m_assetBrowsePath);
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && editable && !m_assetSelection.empty())
		{
			m_pendingDeletes = m_assetSelection;
			m_openDeletePopup = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && editable && !m_assetSelection.empty())
		{
			m_pendingRenameSource = m_assetSelection.front();
			m_renameBuffer = m_pendingRenameSource.filename().string();
			m_openRenamePopup = true;
		}
	}

	if (!m_assetStatus.empty())
		ImGui::TextDisabled("%s", m_assetStatus.c_str());
	ImGui::Separator();

	drawFileGrid(root, editable);

	ImGui::End();
}

void SceneEditorUI::saveScene(const std::filesystem::path &path)
{
	auto sceneManager = m_engine.getSceneManager();
	auto scene = sceneManager ? sceneManager->getActiveScene() : nullptr;
	if (!scene)
	{
		spdlog::warn("SceneEditorUI: no active scene to save");
		return;
	}
	if (SceneSerializer::save(*scene, path))
	{
		m_state.currentScenePath = path;
		m_state.sceneDirty = false;
	}
}

void SceneEditorUI::newScene()
{
	if (!confirmDiscardIfDirty())
		return;
	auto sceneManager = m_engine.getSceneManager();
	if (!sceneManager)
		return;

	auto scene = std::make_shared<engine::scene::Scene>();
	if (!scene->getRoot())
		scene->setRoot(std::make_shared<engine::scene::nodes::Node>());

	// Register first so the scene's root has an engine context before nodes are
	// added (addChild starts them with that context).
	sceneManager->registerScene("Untitled", scene);
	setupEditorCamera(*scene, m_state);

	// Default lighting so a fresh scene is not pitch black.
	auto sun = std::make_shared<engine::scene::nodes::LightNode>();
	sun->setName("Sun");
	engine::rendering::DirectionalLight sunData;
	sunData.intensity = 2.0f;
	sun->setLight(engine::rendering::Light(sunData));
	sun->getTransform().setLocalEulerAngles(glm::vec3(50.0f, -30.0f, 0.0f));
	scene->getRoot()->addChild(sun->asNode());

	auto ambient = std::make_shared<engine::scene::nodes::LightNode>();
	ambient->setName("Ambient");
	engine::rendering::AmbientLight ambientData;
	ambientData.intensity = 0.15f;
	ambient->setLight(engine::rendering::Light(ambientData));
	scene->getRoot()->addChild(ambient->asNode());

	sceneManager->loadScene("Untitled");

	m_state.currentScenePath.clear();
	m_state.sceneDirty = false;
	m_state.selected.reset();
	m_state.viewCamera.reset();
	m_assetStatus = "New scene";
}

void SceneEditorUI::openScene(const std::filesystem::path &path)
{
	auto sceneManager = m_engine.getSceneManager();
	if (!sceneManager)
		return;

	// Guard against opening a project file as a scene (a project lists "scenes"
	// and has no node "root"). This is what corrupted the project's scene list.
	{
		std::ifstream peek(path, std::ios::binary);
		nlohmann::json j;
		if (peek)
		{
			try { peek >> j; } catch (...) {}
		}
		if (!j.contains("root") && (j.contains("scenes") || j.contains("settings")))
		{
			m_assetStatus = "'" + path.filename().string() + "' is a project - use File > Open Project.";
			return;
		}
	}

	auto scene = engine::scene::SceneSerializer::load(path);
	if (!scene)
	{
		m_assetStatus = "Failed to open " + path.string();
		return;
	}
	if (!scene->getRoot())
		scene->setRoot(std::make_shared<engine::scene::nodes::Node>());

	std::string name = path.stem().string();
	if (name.empty())
		name = "Scene";
	// Register first so the scene's root has an engine context before
	// setupEditorCamera adds nodes (addChild starts them with that context).
	sceneManager->registerScene(name, scene);
	setupEditorCamera(*scene, m_state);
	sceneManager->loadScene(name);
	applySceneSettings(*scene); // project defaults + this scene's override

	m_state.currentScenePath = path;
	m_state.sceneDirty = false;
	m_state.selected.reset();
	m_state.viewCamera.reset();

	// Opening a scene loads it INTO the current project; track it in the project's
	// scene list so it shows under Project > Scenes and is saved with the project.
	const std::string token = engine::core::PathProvider::toEnginePath(path);
	if (std::find(m_project.scenes.begin(), m_project.scenes.end(), token) == m_project.scenes.end())
		m_project.scenes.push_back(token);

	addRecentScene(path);
	m_assetStatus = "Opened " + path.filename().string();
}

bool SceneEditorUI::confirmDiscardIfDirty()
{
	if (!m_state.sceneDirty)
		return true;
	const FullscreenDialogGuard dialogGuard;
	return tinyfd_messageBox("Unsaved changes", "Discard unsaved changes to the current scene?", "yesno", "warning", 0) == 1;
}

void SceneEditorUI::addRecentScene(const std::filesystem::path &path)
{
	const std::string entry = path.generic_string();
	m_recentScenes.erase(std::remove(m_recentScenes.begin(), m_recentScenes.end(), entry), m_recentScenes.end());
	m_recentScenes.insert(m_recentScenes.begin(), entry);
	if (m_recentScenes.size() > 10)
		m_recentScenes.resize(10);
	saveRecentScenes();
}

void SceneEditorUI::loadRecentScenes()
{
	m_recentScenes.clear();
	std::ifstream in(engine::core::PathProvider::getConfigs("recent_scenes.txt"));
	std::string line;
	while (std::getline(in, line) && m_recentScenes.size() < 10)
	{
		if (!line.empty())
			m_recentScenes.push_back(line);
	}
}

void SceneEditorUI::saveRecentScenes()
{
	const std::filesystem::path file = engine::core::PathProvider::getConfigs("recent_scenes.txt");
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::trunc);
	for (const auto &entry : m_recentScenes)
		out << entry << "\n";
}

void SceneEditorUI::openPath(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		m_assetStatus = "Cannot open " + path.string();
		return;
	}
	nlohmann::json j;
	try
	{
		in >> j;
	}
	catch (...)
	{
		m_assetStatus = "Not valid JSON: " + path.filename().string();
		return;
	}

	// A scene file has a node "root"; a project file lists "scenes"/"settings".
	// Dispatch by content so one Open button handles either (both are .json today).
	if (j.contains("root"))
		openScene(path);
	else if (j.contains("scenes") || j.contains("settings") || j.contains("startupScene"))
		openProject(path);
	else
		m_assetStatus = "Unrecognized file (not a scene or project): " + path.filename().string();
}

void SceneEditorUI::addRecentProject(const std::filesystem::path &path)
{
	const std::string entry = path.generic_string();
	m_recentProjects.erase(std::remove(m_recentProjects.begin(), m_recentProjects.end(), entry), m_recentProjects.end());
	m_recentProjects.insert(m_recentProjects.begin(), entry);
	if (m_recentProjects.size() > 10)
		m_recentProjects.resize(10);
	saveRecentProjects();
}

void SceneEditorUI::loadRecentProjects()
{
	m_recentProjects.clear();
	std::ifstream in(engine::core::PathProvider::getConfigs("recent_projects.txt"));
	std::string line;
	while (std::getline(in, line) && m_recentProjects.size() < 10)
	{
		if (!line.empty())
			m_recentProjects.push_back(line);
	}
}

void SceneEditorUI::saveRecentProjects()
{
	const std::filesystem::path file = engine::core::PathProvider::getConfigs("recent_projects.txt");
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::trunc);
	for (const auto &entry : m_recentProjects)
		out << entry << "\n";
}

void SceneEditorUI::loadShortcuts()
{
	// One line per binding: "<actionIndex> <key> <ctrl> <shift> <alt>". Missing or
	// out-of-range entries keep the default assigned in the constructor.
	std::ifstream in(engine::core::PathProvider::getConfigs("shortcuts.txt"));
	if (!in)
		return;
	int index = 0, key = 0, ctrl = 0, shift = 0, alt = 0;
	while (in >> index >> key >> ctrl >> shift >> alt)
	{
		if (index >= 0 && index < static_cast<int>(m_shortcuts.size()))
			m_shortcuts[index] = Shortcut{key, ctrl != 0, shift != 0, alt != 0};
	}
}

void SceneEditorUI::saveShortcuts()
{
	const std::filesystem::path file = engine::core::PathProvider::getConfigs("shortcuts.txt");
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::trunc);
	for (size_t i = 0; i < m_shortcuts.size(); ++i)
	{
		const Shortcut &s = m_shortcuts[i];
		out << i << " " << s.key << " " << (s.ctrl ? 1 : 0) << " " << (s.shift ? 1 : 0) << " " << (s.alt ? 1 : 0) << "\n";
	}
}

void SceneEditorUI::applySceneSettings(engine::scene::Scene &scene)
{
	engine::GameEngineOptions effective = m_project.settings;
	const std::string &overrideJson = scene.getSettingsOverride();
	m_overrideSceneSettings = !overrideJson.empty();
	if (m_overrideSceneSettings)
	{
		try
		{
			engine::settings::fromJson(nlohmann::json::parse(overrideJson), effective);
		}
		catch (...)
		{
		}
	}
	m_engine.setOptions(effective);
	m_settings = effective;
}

void SceneEditorUI::newProject()
{
	if (!confirmDiscardIfDirty())
		return;

	const std::string picked = pickSaveFile("New Project", "MyGame.vproj", {"*.vproj"}, "Project");
	if (picked.empty())
		return;

	namespace fs = std::filesystem;
	fs::path projectFile(picked);
	if (projectFile.extension() != ".vproj")
		projectFile.replace_extension(".vproj");
	const fs::path projectDir = projectFile.parent_path();

	// Scaffold the portable project folder: somewhere to keep scenes, the material
	// library, imported textures/models, and the compiled-in C++ scripts.
	std::error_code ec;
	for (const char *sub : {"assets/scenes", "assets/materials", "assets/textures", "assets/models", "scripts"})
		fs::create_directories(projectDir / sub, ec);

	// Point the asset root at the new project before creating/saving anything, so
	// its scene and materials are written inside the project folder.
	engine::core::PathProvider::setAssetRoot(projectDir / "assets");

	m_project = engine::Project{};
	m_project.name = projectFile.stem().string();
	m_project.settings = m_engine.getOptions();
	m_projectPath = projectFile;
	m_overrideSceneSettings = false;
	m_settings = m_project.settings;

	newScene();	   // a fresh default scene inside the new project
	saveProject(); // write the .vproj + the default scene into the project folder
	addRecentProject(projectFile);
	m_showWelcome = false;
	m_assetStatus = "Created project '" + m_project.name + "'";
}

void SceneEditorUI::openProject(const std::filesystem::path &path)
{
	// Guard against opening a scene file as a project (a scene has a node "root").
	{
		std::ifstream peek(path, std::ios::binary);
		nlohmann::json j;
		if (peek)
		{
			try { peek >> j; } catch (...) {}
		}
		if (j.contains("root"))
		{
			m_assetStatus = "'" + path.filename().string() + "' is a scene - use File > Open Scene.";
			return;
		}
	}

	auto loaded = engine::ProjectSerializer::load(path);
	if (!loaded)
	{
		m_assetStatus = "Failed to open project: " + path.string();
		return;
	}
	m_project = *loaded;
	m_projectPath = path;
	// Repoint the asset root at this project's folder so every "asset://" token
	// (scenes, materials, textures) resolves inside the project we just opened.
	engine::core::PathProvider::setAssetRoot(path.parent_path() / "assets");
	m_showWelcome = false;
	m_engine.setOptions(m_project.settings);
	m_settings = m_project.settings;
	addRecentProject(path);

	// Load the project material library first so scene material references resolve.
	if (auto resources = m_engine.getResourceManager(); resources && resources->m_materialManager && resources->m_textureManager)
	{
		for (const auto &token : m_project.materials)
		{
			const std::filesystem::path file = engine::core::PathProvider::resolveEnginePath(token);
			engine::resources::MaterialSerializer::load(file, *resources->m_materialManager, *resources->m_textureManager);
		}
	}

	std::string startup = m_project.startupScene;
	if (startup.empty() && !m_project.scenes.empty())
		startup = m_project.scenes.front();
	if (!startup.empty())
		openScene(engine::core::PathProvider::resolveEnginePath(startup));
	m_assetStatus = "Opened project '" + m_project.name + "'";
}

void SceneEditorUI::importSceneFromProject(const std::filesystem::path &sourceScene)
{
	if (m_projectPath.empty())
	{
		m_assetStatus = "Open or create a project before importing a scene.";
		return;
	}

	ImportReport report = SceneImporter::importScene(sourceScene, m_projectPath.parent_path(), m_project);
	m_importSummary = report.summary();
	m_importHadClashes = !report.scriptClashes.empty();
	m_openImportReportPopup = true;
	if (!report.ok)
	{
		m_assetStatus = "Import failed: " + report.error;
		return;
	}

	// Load the (possibly newly imported) material library so the scene resolves its
	// references, then open the written scene through the normal path.
	if (auto resources = m_engine.getResourceManager(); resources && resources->m_materialManager && resources->m_textureManager)
	{
		for (const auto &token : m_project.materials)
		{
			const std::filesystem::path file = engine::core::PathProvider::resolveEnginePath(token);
			engine::resources::MaterialSerializer::load(file, *resources->m_materialManager, *resources->m_textureManager);
		}
	}

	openScene(report.importedScenePath);
	m_state.sceneDirty = true; // the project's scene/material/script lists changed
	m_assetStatus = "Imported '" + report.importedScenePath.filename().string() + "'";
}

void SceneEditorUI::saveProject()
{
	if (m_projectPath.empty())
	{
		const std::string defaultPath = (engine::core::PathProvider::getAssetRoot() / "game.vproj").string();
		const std::string picked = pickSaveFile("Save Project", defaultPath.c_str(), {"*.vproj"}, "Project");
		if (picked.empty())
			return;
		m_projectPath = picked;
	}

	// Settings edited in project mode are the project defaults.
	if (!m_overrideSceneSettings)
		m_project.settings = m_settings;

	// A project must reference a startup scene or the built/reopened game renders
	// nothing. If the active scene was never saved to a path, give it a default one
	// under the asset root so saving / building the project is never empty (black).
	auto sceneManager = m_engine.getSceneManager();
	if (sceneManager && sceneManager->getActiveScene())
	{
		if (m_state.currentScenePath.empty())
			m_state.currentScenePath = engine::core::PathProvider::getAssetRoot() / "scenes" / "Main" / "scene.vscene";
		saveScene(m_state.currentScenePath); // writes the scene + sets currentScenePath
		const std::string token = engine::core::PathProvider::toEnginePath(m_state.currentScenePath);
		if (std::find(m_project.scenes.begin(), m_project.scenes.end(), token) == m_project.scenes.end())
			m_project.scenes.push_back(token);
		if (m_project.startupScene.empty())
			m_project.startupScene = token;
	}
	else
	{
		spdlog::warn("Save Project: no active scene to reference");
	}

	// Persist the material library: each material -> assets/materials/<name>.mat.json,
	// recorded in the project so scenes resolve their references on load.
	if (auto resources = m_engine.getResourceManager(); resources && resources->m_materialManager)
	{
		m_project.materials.clear();
		const std::filesystem::path materialsDir = engine::core::PathProvider::getAssetRoot() / "materials";
		for (const auto &material : resources->m_materialManager->getAll())
		{
			if (!material)
				continue;
			const std::string name = material->getName().value_or("Material");
			const std::filesystem::path file = materialsDir / (name + ".mat.json");
			if (engine::resources::MaterialSerializer::save(*material, file))
			{
				const std::string token = engine::core::PathProvider::toEnginePath(file);
				if (std::find(m_project.materials.begin(), m_project.materials.end(), token) == m_project.materials.end())
					m_project.materials.push_back(token);
			}
		}
	}

	// Record the project's custom C++ scripts (compiled into the game by Build
	// Game). Stored project-relative, e.g. "scripts/Rotator.cpp".
	m_project.scripts.clear();
	{
		namespace fs = std::filesystem;
		const fs::path scriptsDir = m_projectPath.parent_path() / "scripts";
		std::error_code ec;
		if (fs::exists(scriptsDir, ec))
		{
			for (fs::directory_iterator it(scriptsDir, ec), end; it != end && !ec; it.increment(ec))
			{
				std::error_code fileEc;
				if (it->is_regular_file(fileEc) && it->path().extension() == ".cpp")
					m_project.scripts.push_back("scripts/" + it->path().filename().generic_string());
			}
			std::sort(m_project.scripts.begin(), m_project.scripts.end());
		}
	}

	if (engine::ProjectSerializer::save(m_project, m_projectPath))
	{
		m_state.sceneDirty = false;
		m_assetStatus = "Saved project to " + m_projectPath.string();
	}
}

void SceneEditorUI::buildGame()
{
	if (m_building)
		return;
	saveProject();
	if (m_projectPath.empty())
	{
		m_assetStatus = "Save the project before building.";
		return;
	}
	if (m_buildThread.joinable())
		m_buildThread.join(); // reclaim a previously finished build thread
	m_building = true;
	const std::filesystem::path projectFile = m_projectPath;
	m_buildThread = std::thread([this, projectFile]() { runBuild(projectFile); });
}

void SceneEditorUI::runBuild(std::filesystem::path projectFile)
{
	namespace fs = std::filesystem;
	const fs::path repoRoot = engine::core::PathProvider::getLibraryRoot();
	const fs::path script = repoRoot / "scripts" / "build-example.bat";
	const fs::path outDir = repoRoot / "examples" / "build" / "runtime_player" / "Windows" / "Release";

	spdlog::info("Build Game: compiling the runtime player (Release)...");

	// Custom C++ scripts: a scripts/ folder next to the project is compiled into the
	// game (the runtime_player CMake reads PROJECT_SCRIPTS_DIR; each .cpp self-registers
	// its node types). Passed via the environment so build-example.bat is untouched.
	const fs::path scriptsDir = projectFile.parent_path() / "scripts";
	std::string envPrefix;
	if (fs::exists(scriptsDir))
	{
		spdlog::info("Build Game: compiling project scripts from '{}'", scriptsDir.generic_string());
		envPrefix = "set \"PROJECT_SCRIPTS_DIR=" + scriptsDir.string() + "\" && ";
	}

#if defined(_WIN32)
	const std::string command = "cmd /c \"" + envPrefix + "\"" + script.string() + "\" runtime_player Release WGPU 2>&1\"";
	FILE *pipe = _popen(command.c_str(), "r");
	if (!pipe)
	{
		spdlog::error("Build Game: could not start the build process.");
		m_building = false;
		return;
	}
	char buffer[512];
	while (std::fgets(buffer, sizeof(buffer), pipe))
	{
		std::string line(buffer);
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		if (!line.empty())
			spdlog::info("[build] {}", line);
	}
	const int code = _pclose(pipe);
	if (code != 0)
	{
		spdlog::error("Build Game: build failed (exit code {}).", code);
		m_building = false;
		return;
	}
#else
	spdlog::error("Build Game is currently implemented for Windows only.");
	m_building = false;
	return;
#endif

	// Stage engine resources + the project's assets next to the produced exe, so
	// the Release game resolves resource:// and asset:// relative to itself.
	std::error_code ec;
	spdlog::info("Build Game: staging data into '{}'...", outDir.generic_string());
	fs::create_directories(outDir / "assets", ec);
	const fs::path resourceRoot = engine::core::PathProvider::getResourceRoot();
	const fs::path assetRoot = engine::core::PathProvider::getAssetRoot();
	if (fs::exists(resourceRoot))
		fs::copy(resourceRoot, outDir / "resources", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	if (fs::exists(assetRoot))
		fs::copy(assetRoot, outDir / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	fs::copy_file(projectFile, outDir / "assets" / "project.json", fs::copy_options::overwrite_existing, ec);
	if (ec)
		spdlog::warn("Build Game: staging completed with errors: {}", ec.message());

	// Warn about absolute asset paths in the staged scenes/materials: they will not
	// resolve on another machine or in the shipped game. Portable data uses only
	// asset:// / resource:// tokens.
	{
		auto looksAbsolute = [](const std::string &s)
		{
			if (s.rfind("asset://", 0) == 0 || s.rfind("resource://", 0) == 0)
				return false;
			if (s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && (s[2] == '/' || s[2] == '\\'))
				return true; // Windows drive letter followed by a slash
			return !s.empty() && (s[0] == '/' || s[0] == '\\');
		};
		std::function<void(const nlohmann::json &, const fs::path &)> scan =
			[&](const nlohmann::json &node, const fs::path &file)
		{
			if (node.is_string())
			{
				const std::string value = node.get<std::string>();
				if (looksAbsolute(value))
					spdlog::warn("Build Game: absolute asset path in '{}': '{}' - not portable; import it into the project.",
						file.filename().generic_string(), value);
			}
			else if (node.is_array() || node.is_object())
				for (const auto &child : node)
					scan(child, file);
		};
		std::error_code scanEc;
		for (fs::recursive_directory_iterator it(outDir / "assets", scanEc), end; it != end && !scanEc; it.increment(scanEc))
		{
			std::error_code fileEc;
			if (!it->is_regular_file(fileEc))
				continue;
			const std::string ext = it->path().extension().string();
			if (ext != ".vscene" && ext != ".json")
				continue;
			std::ifstream in(it->path());
			nlohmann::json parsed;
			try
			{
				in >> parsed;
			}
			catch (...)
			{
				continue;
			}
			scan(parsed, it->path());
		}
	}

	spdlog::info("Build Game: done. Run RuntimePlayer.exe in '{}'.", outDir.generic_string());
	showInExplorer(outDir); // reveal the finished game folder
	m_building = false;
}

} // namespace editor
