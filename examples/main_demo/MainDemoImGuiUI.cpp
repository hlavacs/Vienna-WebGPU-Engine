#include "MainDemoImGuiUI.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/GBufferPass.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/CacheStats.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/ModelManager.h"
#include "engine/resources/ResourceManager.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/ModelRenderNode.h"

namespace demo
{

MainDemoImGuiUI::MainDemoImGuiUI(
	engine::GameEngine &engine,
	std::shared_ptr<DayNightCycle> dayNightCycle
) : m_engine(engine), m_dayNightCycle(std::move(dayNightCycle))
{
	m_rootNode = engine.getSceneManager()->getActiveScene()->getRoot();
	for (const auto &child : m_rootNode->getChildrenOfType<engine::scene::nodes::LightNode>())
	{
		m_lightNodes.push_back(child);
	};

	auto renderer = m_engine.getRenderer().lock();
	m_debugShadowCubeArray = renderer->getShadowPass().DEBUG_SHADOW_CUBE_ARRAY;
	m_debugShadow2DArray = renderer->getShadowPass().DEBUG_SHADOW_2D_ARRAY;

	registerSettingsPersistence();
}

MainDemoImGuiUI::~MainDemoImGuiUI()
{
	// wgpu raw handles aren't smart-ptr-managed; release them explicitly so
	// the driver doesn't keep the shader module / pipeline objects alive
	// past WebGPUContext shutdown.
	if (m_depthPreviewPipeline)        m_depthPreviewPipeline.release();
	if (m_depthPreviewPipelineLayout)  m_depthPreviewPipelineLayout.release();
	if (m_depthPreviewBindGroupLayout) m_depthPreviewBindGroupLayout.release();
	if (m_depthPreviewShaderModule)    m_depthPreviewShaderModule.release();
}

void MainDemoImGuiUI::render(const std::shared_ptr<engine::scene::SceneManager> &sceneManager)
{
	auto rootNode = m_engine.getSceneManager()->getActiveScene()->getRoot();
	if (m_rootNode != rootNode)
	{
		m_rootNode = rootNode;
		m_lightNodes.clear();
		for (const auto &child : m_rootNode->getChildrenOfType<engine::scene::nodes::LightNode>())
		{
			m_lightNodes.push_back(child);
		};
	}
	// Snapshot panel visibility so we can flag imgui.ini dirty if anything is
	// toggled this frame (via the menu or a window's close button), so the
	// open/closed layout persists to next run.
	const bool visBefore[] = {
		m_showCameraLighting, m_showMaterials, m_showLights, m_showFlock,
		m_showDayNight, m_showPerformance, m_showPassControls, m_showShadowDebug
	};

	// Animate the scattered point-light flock every frame, independent of
	// whether its control panel is open — the panel only edits parameters.
	if (m_flockEnabled && !m_flockLights.empty())
	{
		updateFlock(m_engine.getFrameTime() * 0.001f); // ms -> seconds
	}

	// Keep shadow-map debug rendering in sync with the panel toggle so the
	// debug texture arrays are only populated while the panel is open.
	if (auto renderer = m_engine.getRenderer().lock())
	{
		renderer->getShadowPass().setDebugMode(m_showShadowDebug);
	}

	renderMainMenuBar(sceneManager);

	if (m_showCameraLighting)
	{
		if (ImGui::Begin("Camera & Lighting", &m_showCameraLighting))
			renderLightingAndCameraControls();
		ImGui::End();
	}
	if (m_showMaterials)
	{
		if (ImGui::Begin("Materials", &m_showMaterials))
			renderMaterialProperties();
		ImGui::End();
	}
	if (m_showLights)
	{
		if (ImGui::Begin("Lights", &m_showLights))
			renderLightsSection();
		ImGui::End();
	}
	if (m_showFlock)
	{
		if (ImGui::Begin("Light Flock", &m_showFlock))
			renderFlockControls();
		ImGui::End();
	}
	if (m_dayNightCycle && m_showDayNight)
	{
		renderDayNightWindow();
	}

	if (m_showPerformance)  renderPerformanceWindow();
	if (m_showPassControls) renderPassControlsWindow();
	if (m_showShadowDebug)  renderShadowDebugWindow();

	const bool visAfter[] = {
		m_showCameraLighting, m_showMaterials, m_showLights, m_showFlock,
		m_showDayNight, m_showPerformance, m_showPassControls, m_showShadowDebug
	};
	if (std::memcmp(visBefore, visAfter, sizeof(visBefore)) != 0)
		ImGui::MarkIniSettingsDirty();
}

void MainDemoImGuiUI::renderMainMenuBar(const std::shared_ptr<engine::scene::SceneManager> &sceneManager)
{
	if (!ImGui::BeginMainMenuBar())
		return;

	if (sceneManager && ImGui::BeginMenu("Scene"))
	{
		const std::string active = sceneManager->getActiveSceneName();
		for (const auto &name : sceneManager->getSceneNames())
		{
			const bool selected = (name == active);
			if (ImGui::MenuItem(name.c_str(), nullptr, selected) && !selected)
				sceneManager->loadSceneAsync(name);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Camera & Lighting", nullptr, &m_showCameraLighting);
		ImGui::MenuItem("Materials",         nullptr, &m_showMaterials);
		ImGui::MenuItem("Lights",            nullptr, &m_showLights);
		ImGui::MenuItem("Light Flock",       nullptr, &m_showFlock);
		if (m_dayNightCycle)
			ImGui::MenuItem("Day-Night Cycle", nullptr, &m_showDayNight);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Debug"))
	{
		ImGui::MenuItem("Performance",   nullptr, &m_showPerformance);
		ImGui::MenuItem("Pass Controls", nullptr, &m_showPassControls);
		ImGui::MenuItem("Shadow Maps",   nullptr, &m_showShadowDebug);
		ImGui::Separator();
		if (ImGui::MenuItem("Reload Shaders"))
		{
			if (auto ctx = m_engine.getContext())
				ctx->pipelineManager().reloadAllPipelines();
		}
		ImGui::EndMenu();
	}

	// Right-aligned scene name + FPS readout for at-a-glance status.
	{
		char status[128];
		std::snprintf(status, sizeof(status), "%s  |  %.0f FPS",
			sceneManager ? sceneManager->getActiveSceneName().c_str() : "",
			m_engine.getFPS());
		const float width = ImGui::CalcTextSize(status).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
		ImGui::TextUnformatted(status);
	}

	ImGui::EndMainMenuBar();
}

void MainDemoImGuiUI::renderDayNightWindow()
{
	if (!m_dayNightCycle)
		return;

	if (ImGui::Begin("Day-Night Cycle", &m_showDayNight))
	{
		float hour = m_dayNightCycle->getHour();
		if (ImGui::SliderFloat("Hour of Day", &hour, 0.0f, 24.0f))
			m_dayNightCycle->setHour(hour);

		bool paused = m_dayNightCycle->isPaused();
		if (ImGui::Checkbox("Pause Cycle", &paused))
			m_dayNightCycle->setPaused(paused);

		float cycleDuration = m_dayNightCycle->getCycleDuration();
		if (ImGui::SliderFloat("Cycle Duration (s)", &cycleDuration, 10.0f, 600.0f))
			m_dayNightCycle->setCycleDuration(cycleDuration);
	}
	ImGui::End();
}

std::vector<std::pair<const char *, bool *>> MainDemoImGuiUI::panelVisibilityTable()
{
	return {
		{"CameraLighting", &m_showCameraLighting},
		{"Materials",      &m_showMaterials},
		{"Lights",         &m_showLights},
		{"Flock",          &m_showFlock},
		{"DayNight",       &m_showDayNight},
		{"Performance",    &m_showPerformance},
		{"PassControls",   &m_showPassControls},
		{"ShadowDebug",    &m_showShadowDebug},
	};
}

void MainDemoImGuiUI::applyPanelVisibility(const char *key, bool value)
{
	for (auto &[k, ptr] : panelVisibilityTable())
	{
		if (std::strcmp(k, key) == 0)
		{
			*ptr = value;
			return;
		}
	}
}

void MainDemoImGuiUI::registerSettingsPersistence()
{
	// Persist each panel's open/closed state into imgui.ini (a "[MainDemoUI]
	// [Panels]" block) so the window layout the user leaves behind is restored
	// next run. ImGui reads this back on the first NewFrame, which happens
	// after this constructor — so registering here catches the load.
	ImGuiSettingsHandler handler;
	handler.TypeName = "MainDemoUI";
	handler.TypeHash = ImHashStr("MainDemoUI");
	handler.UserData = this;
	handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *, const char *) -> void *
	{
		return (void *)1; // single implicit entry; state is keyed off UserData
	};
	handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *h, void *, const char *line)
	{
		auto *self = static_cast<MainDemoImGuiUI *>(h->UserData);
		char key[64] = {};
		int  value   = 0;
		if (std::sscanf(line, "%63[^=]=%d", key, &value) == 2)
			self->applyPanelVisibility(key, value != 0);
	};
	handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *h, ImGuiTextBuffer *buf)
	{
		auto *self = static_cast<MainDemoImGuiUI *>(h->UserData);
		buf->appendf("[%s][Panels]\n", h->TypeName);
		for (const auto &[key, ptr] : self->panelVisibilityTable())
			buf->appendf("%s=%d\n", key, *ptr ? 1 : 0);
		buf->append("\n");
	};
	ImGui::AddSettingsHandler(&handler);
}

void MainDemoImGuiUI::renderPerformanceWindow()
{
	ImGui::Begin("Performance", &m_showPerformance);
	ImGui::Text("FPS: %.1f", m_engine.getFPS());
	ImGui::Text("Frame Time: %.2f ms", m_engine.getFrameTime());

	// Per-pass CPU timings (rolling average over the profiler's window). Times
	// are CPU encode + submit cost - they DON'T include GPU work that runs
	// async after submit. If total CPU << frame time, the bottleneck is GPU.
	if (auto renderer = m_engine.getRenderer().lock())
	{
		const auto &prof = renderer->getProfiler();
		auto entries = prof.getEntries();
		if (!entries.empty() && ImGui::CollapsingHeader("Per-pass timing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const bool gpuEnabled = prof.isGpuTimingEnabled();
			// Hide the GPU columns when no entry has any GPU sample yet -
			// avoids showing a column of zeros when timestamp-query is
			// enabled but the first readback hasn't completed.
			bool gpuHasData = false;
			float totalCpuAvg = 0.0f, totalCpuLast = 0.0f, totalGpuAvg = 0.0f, totalGpuLast = 0.0f;
			for (const auto &e : entries)
			{
				if (e.lastGpuMs > 0.0f || e.averageGpuMs > 0.0f)
					gpuHasData = true;
				if (e.label.rfind("Pass.", 0) == 0) // only sum per-pass rows
				{
					totalCpuAvg += e.averageCpuMs;
					totalCpuLast += e.lastCpuMs;
					totalGpuAvg += e.averageGpuMs;
					totalGpuLast += e.lastGpuMs;
				}
			}
			const bool gpu = gpuEnabled && gpuHasData;

			ImGui::TextDisabled(
				gpuEnabled
					? (gpu ? "CPU (encode+submit) / GPU (real execution)"
							: "GPU readback pending - values appear in 1-3 frames")
					: "CPU only - device lacks timestamp-query"
			);

			// --- Copy to clipboard (TSV) ---
			if (ImGui::Button("Copy table"))
			{
				std::string out;
				out += "Pass\tCPU avg\tCPU last";
				if (gpu) out += "\tGPU avg\tGPU last";
				out += "\n";
				for (const auto &e : entries)
				{
					char line[256];
					if (gpu)
						std::snprintf(line, sizeof(line), "%s\t%.3f\t%.3f\t%.3f\t%.3f\n",
							e.label.c_str(), e.averageCpuMs, e.lastCpuMs, e.averageGpuMs, e.lastGpuMs);
					else
						std::snprintf(line, sizeof(line), "%s\t%.3f\t%.3f\n",
							e.label.c_str(), e.averageCpuMs, e.lastCpuMs);
					out += line;
				}
				char totalLine[256];
				if (gpu)
					std::snprintf(totalLine, sizeof(totalLine), "Pass.Total\t%.3f\t%.3f\t%.3f\t%.3f\n",
						totalCpuAvg, totalCpuLast, totalGpuAvg, totalGpuLast);
				else
					std::snprintf(totalLine, sizeof(totalLine), "Pass.Total\t%.3f\t%.3f\n",
						totalCpuAvg, totalCpuLast);
				out += totalLine;
				ImGui::SetClipboardText(out.c_str());
			}
			ImGui::SameLine();
			ImGui::Text("(%zu rows)", entries.size());

			if (ImGui::BeginTable("ProfilerTable", gpu ? 5 : 3,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable))
			{
				ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_NoSort);
				ImGui::TableSetupColumn("CPU avg");
				ImGui::TableSetupColumn("CPU last");
				if (gpu)
				{
					ImGui::TableSetupColumn("GPU avg", ImGuiTableColumnFlags_DefaultSort);
					ImGui::TableSetupColumn("GPU last");
				}
				ImGui::TableHeadersRow();

				// Sort if user clicked a header
				if (auto *specs = ImGui::TableGetSortSpecs())
				{
					if (specs->SpecsCount > 0)
					{
						const auto &s = specs->Specs[0];
						std::sort(entries.begin(), entries.end(),
							[&](const auto &a, const auto &b) {
								float av = 0, bv = 0;
								switch (s.ColumnIndex)
								{
								case 1: av = a.averageCpuMs; bv = b.averageCpuMs; break;
								case 2: av = a.lastCpuMs;    bv = b.lastCpuMs;    break;
								case 3: av = a.averageGpuMs; bv = b.averageGpuMs; break;
								case 4: av = a.lastGpuMs;    bv = b.lastGpuMs;    break;
								}
								return s.SortDirection == ImGuiSortDirection_Ascending ? av < bv : av > bv;
							});
					}
				}

				for (const auto &e : entries)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.label.c_str());
					ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", e.averageCpuMs);
					ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", e.lastCpuMs);
					if (gpu)
					{
						ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", e.averageGpuMs);
						ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", e.lastGpuMs);
					}
				}
				// Totals row (sum of Pass.* rows only)
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Pass.Total");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", totalCpuAvg);
				ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", totalCpuLast);
				if (gpu)
				{
					ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", totalGpuAvg);
					ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", totalGpuLast);
				}
				ImGui::EndTable();
			}
		}
	}

	// Cache overview. snapshotAll() walks every registered factory cache under
	// one mutex acquire — cheap enough to run each frame the panel is open.
	// "Size" is total entries, "Alive" is entries still holding a resource
	// (for weak-ref caches these can diverge; for the strong-ref factory
	// caches alive == size). Idle window in frames; 0 = never evict.
	if (ImGui::CollapsingHeader("Cache overview"))
	{
		auto &reg = m_engine.getContext()->cacheRegistry();
		const auto snaps = reg.snapshotAll();
		ImGui::TextDisabled("Total caches: %zu", snaps.size());

		if (ImGui::BeginTable("CacheTable", 4,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Cache");
			ImGui::TableSetupColumn("Size");
			ImGui::TableSetupColumn("Alive");
			ImGui::TableSetupColumn("Idle window (frames)");
			ImGui::TableHeadersRow();
			for (const auto &s : snaps)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::Text("%s", s.label ? s.label : "(unnamed)");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", s.size);
				ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", s.alive);
				ImGui::TableSetColumnIndex(3);
				if (s.maxIdleFrames == 0)
				{
					ImGui::TextDisabled("never");
				}
				else
				{
					// Slider per-row so you can tune live without recompiling.
					// 60-frame increments at 0..18000 (5 minutes @ 60fps).
					int frames = static_cast<int>(s.maxIdleFrames);
					ImGui::PushID(s.label);
					if (ImGui::SliderInt("##idle", &frames, 60, 18000, "%d frames"))
					{
						reg.setMaxIdleFramesFor(s.label, static_cast<uint32_t>(frames));
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}

		// Global controls
		static int globalFrames = 1800;
		ImGui::SetNextItemWidth(180);
		ImGui::SliderInt("##all", &globalFrames, 60, 18000, "All: %d frames");
		ImGui::SameLine();
		if (ImGui::Button("Apply to all"))
		{
			reg.setMaxIdleFramesForAll(static_cast<uint32_t>(globalFrames));
		}
		ImGui::SameLine();
		if (ImGui::Button("Run cleanAll now"))
		{
			const std::size_t evicted = reg.cleanAll();
			spdlog::info("Manual cleanAll evicted {} entries", evicted);
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear all caches"))
		{
			// Every cache now goes through SlotCache, so soft-clear is the
			// single verb that does the right thing everywhere: drop each
			// slot's resource pointer, keep the slot + build_fn alive,
			// outstanding handles auto-rebuild on next access. Pipelines
			// also need a shader-source reload first so the rebuild picks
			// up edits-on-disk, not the same WGSL we just had.
			auto ctx = m_engine.getContext();

			// 1. Reload shader sources synchronously. ShaderRegistry slots
			//    get their info replaced in place; pipelines that re-fetch
			//    a shader after their soft-clear pick up the new info.
			ctx->pipelineManager().reloadAllPipelines();

			// 2. Soft-clear every registered cache (Pipeline, Sampler,
			//    Shader, Texture, Mesh, Material, Model). Caches that don't
			//    yet implement softClear fall through to their hard cleanup.
			const std::size_t soft = reg.softClearAll();

			// 3. Drop renderer-side cached bind groups so the next frame
			//    rebuilds them with the fresh factory state. Still needed
			//    until bind-group version-tracking lands (see Skill follow-
			//    ups).
			if (auto renderer = m_engine.getRenderer().lock())
			{
				renderer->resetCachedBindings();
			}
			spdlog::info("Manual clearAll: {} cache(s) soft-cleared, renderer bindings reset", soft);
		}
	}

	if (ImGui::CollapsingHeader("Cache skip counters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextDisabled(
			"Cumulative since session start (or last Reset). Skip rate = "
			"how often each per-frame fingerprint cache hit. Hitting a high "
			"skip rate on static-camera frames is the signal that the "
			"recent optimisations are firing on your current workload."
		);

		using namespace engine::rendering;

		struct Row
		{
			const char *label;
			uint64_t    executed;
			uint64_t    skipped;
		};
		const Row rows[] = {
			{"Cluster compute (GPU dispatches)",
			 CacheStats::clusterDispatchesExecuted.load(std::memory_order_relaxed),
			 CacheStats::clusterDispatchesSkipped.load(std::memory_order_relaxed)},
			{"Scene-light upload (writeBuffer)",
			 CacheStats::sceneLightUploadsExecuted.load(std::memory_order_relaxed),
			 CacheStats::sceneLightUploadsSkipped.load(std::memory_order_relaxed)},
			{"Frustum cull (per camera)",
			 CacheStats::frustumCullsExecuted.load(std::memory_order_relaxed),
			 CacheStats::frustumCullsSkipped.load(std::memory_order_relaxed)},
			{"Scene bind group rebuild",
			 CacheStats::sceneBindGroupRebuilds.load(std::memory_order_relaxed),
			 CacheStats::sceneBindGroupHits.load(std::memory_order_relaxed)},
			{"Skybox bind group rebuild",
			 CacheStats::skyboxBindGroupRebuilds.load(std::memory_order_relaxed),
			 CacheStats::skyboxBindGroupHits.load(std::memory_order_relaxed)},
		};

		if (ImGui::BeginTable("CacheSkipTable", 4,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Cache");
			ImGui::TableSetupColumn("Executed");
			ImGui::TableSetupColumn("Skipped");
			ImGui::TableSetupColumn("Skip %");
			ImGui::TableHeadersRow();
			for (const auto &row : rows)
			{
				const uint64_t total = row.executed + row.skipped;
				const float skipPct = total > 0
					? 100.0f * static_cast<float>(row.skipped) / static_cast<float>(total)
					: 0.0f;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(row.label);
				ImGui::TableSetColumnIndex(1); ImGui::Text("%llu", static_cast<unsigned long long>(row.executed));
				ImGui::TableSetColumnIndex(2); ImGui::Text("%llu", static_cast<unsigned long long>(row.skipped));
				ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f%%", skipPct);
			}
			ImGui::EndTable();
		}

		// Object prepare path is two-dimensional: fast vs slow PLUS how
		// many of the fast-path items also wrote their transform. Showing
		// it as its own line makes the "static scene = 0 transform writes"
		// case immediately obvious.
		const uint64_t fastObjects   = CacheStats::objectsFastPath.load(std::memory_order_relaxed);
		const uint64_t slowObjects   = CacheStats::objectsSlowPath.load(std::memory_order_relaxed);
		const uint64_t writeObjects  = CacheStats::objectTransformWrites.load(std::memory_order_relaxed);
		const uint64_t totalObjects  = fastObjects + slowObjects;
		const float    fastPct       = totalObjects > 0
			? 100.0f * static_cast<float>(fastObjects) / static_cast<float>(totalObjects)
			: 0.0f;
		const float    writePct      = fastObjects > 0
			? 100.0f * static_cast<float>(writeObjects) / static_cast<float>(fastObjects)
			: 0.0f;
		ImGui::Spacing();
		ImGui::Text("Objects prepared: %llu (%.1f%% fast-path)",
			static_cast<unsigned long long>(totalObjects), fastPct);
		ImGui::Text("  of fast-path: %llu wrote transform (%.1f%%)",
			static_cast<unsigned long long>(writeObjects), writePct);

		ImGui::Spacing();
		if (ImGui::Button("Reset counters"))
		{
			CacheStats::reset();
		}
	}

	ImGui::End();
}

void MainDemoImGuiUI::renderShadowDebugWindow()
{
	auto renderer = m_engine.getRenderer().lock();
	if (!renderer)
		return;

	// Refresh the debug texture handles each frame — they're (re)created when
	// shadow debug mode flips on, after this UI captured them at construction.
	m_debugShadow2DArray   = renderer->getShadowPass().DEBUG_SHADOW_2D_ARRAY;
	m_debugShadowCubeArray = renderer->getShadowPass().DEBUG_SHADOW_CUBE_ARRAY;

	ImGui::Begin("Shadow Map Debug", &m_showShadowDebug);

	const int thumbSize = 128;
	const int columns = 3;

	// --- Cube array debug ---
	if (m_debugShadowCubeArray)
	{
		if (ImGui::CollapsingHeader("Cube Shadow Maps", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const int totalLayers = m_debugShadowCubeArray->getTextureViewDescriptor().arrayLayerCount;
			const int numCubes = totalLayers / 6;

			for (int cubeIndex = 0; cubeIndex < numCubes; ++cubeIndex)
			{
				if (ImGui::CollapsingHeader(("Cube " + std::to_string(cubeIndex)).c_str()))
				{
					ImGui::Text("Cube Index: %d", cubeIndex);
					ImGui::Separator();

					ImGui::Columns(columns, nullptr, false);

					for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
					{
						int layerIndex = cubeIndex * 6 + faceIndex;
						ImTextureID faceImguiId = m_debugShadowCubeArray->getTextureView(layerIndex);

						ImGui::Text("Face %d", faceIndex);
						ImGui::Image(faceImguiId, ImVec2((float)thumbSize, (float)thumbSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
						ImGui::NextColumn();
					}

					ImGui::Columns(1);
					ImGui::Separator();
				}
			}
		}
	}
	else
	{
		ImGui::Text("No cube shadow array texture available.");
	}

	// --- 2D array debug ---
	if (m_debugShadow2DArray)
	{
		if (ImGui::CollapsingHeader("2D Shadow Maps", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const int totalLayers = m_debugShadow2DArray->getTextureViewDescriptor().arrayLayerCount;
			ImGui::Columns(columns, nullptr, false);

			for (int layerIndex = 0; layerIndex < totalLayers; ++layerIndex)
			{
				ImTextureID texId = m_debugShadow2DArray->getTextureView(layerIndex);

				ImGui::Text("Layer %d", layerIndex);
				ImGui::Image(texId, ImVec2((float)thumbSize, (float)thumbSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
				ImGui::NextColumn();
			}

			ImGui::Columns(1);
			ImGui::Separator();
		}
	}
	else
	{
		ImGui::Text("No 2D shadow array texture available.");
	}

	ImGui::End();
}

bool MainDemoImGuiUI::ensureDepthPreviewPipeline()
{
	if (m_depthPreviewPipeline)
		return true;

	auto ctx = m_engine.getContext();
	if (!ctx) return false;

	// 1) Shader module — load directly via the shader factory's loader, the
	// same way the IBL bakes do for their one-shot pipelines. We're not
	// going through ShaderRegistry / PipelineManager because this pass
	// doesn't participate in the canonical 0..3 bind-group convention
	// (depth-preview shader is marked @standalone-shader).
	const auto shaderPath = engine::core::PathProvider::getResource("shaders/depth_preview.wgsl");
	m_depthPreviewShaderModule = ctx->shaderFactory().loadShaderModule(shaderPath);
	if (!m_depthPreviewShaderModule)
	{
		spdlog::error("DepthPreview: failed to load shader '{}'", shaderPath.string());
		return false;
	}

	// 2) Bind group layout — single depth texture binding at @group(0)
	// @binding(0). textureLoad doesn't need a sampler, so the layout has
	// exactly one entry.
	wgpu::BindGroupLayoutEntry bglEntry{};
	bglEntry.binding                  = 0;
	bglEntry.visibility               = wgpu::ShaderStage::Fragment;
	bglEntry.texture.sampleType       = wgpu::TextureSampleType::Depth;
	bglEntry.texture.viewDimension    = wgpu::TextureViewDimension::_2D;
	bglEntry.texture.multisampled     = false;

	m_depthPreviewBindGroupLayout = ctx->bindGroupFactory().createBindGroupLayout(
		std::vector<wgpu::BindGroupLayoutEntry>{bglEntry}, "DepthPreview.BindGroupLayout");
	if (!m_depthPreviewBindGroupLayout)
	{
		spdlog::error("DepthPreview: failed to create bind group layout");
		return false;
	}

	// 3) Pipeline layout — single bind group.
	m_depthPreviewPipelineLayout = ctx->pipelineFactory().createPipelineLayout(
		&m_depthPreviewBindGroupLayout, 1);
	if (!m_depthPreviewPipelineLayout)
	{
		spdlog::error("DepthPreview: failed to create pipeline layout");
		return false;
	}

	// 4) Render pipeline — fullscreen triangle, no vertex buffers, no
	// depth, one Unorm color target. Matches the preview-texture format
	// the textureFactory creates below.
	wgpu::ColorTargetState colorTarget{};
	colorTarget.format    = wgpu::TextureFormat::RGBA8Unorm;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;
	colorTarget.blend     = nullptr;

	wgpu::FragmentState fragState{};
	fragState.module        = m_depthPreviewShaderModule;
	fragState.entryPoint    = "fs_main";
	fragState.constantCount = 0;
	fragState.constants     = nullptr;
	fragState.targetCount   = 1;
	fragState.targets       = &colorTarget;

	wgpu::RenderPipelineDescriptor pipeDesc{};
	pipeDesc.label                = "DepthPreview.Pipeline";
	pipeDesc.layout               = m_depthPreviewPipelineLayout;
	pipeDesc.vertex.module        = m_depthPreviewShaderModule;
	pipeDesc.vertex.entryPoint    = "vs_main";
	pipeDesc.vertex.bufferCount   = 0;
	pipeDesc.vertex.buffers       = nullptr;
	pipeDesc.primitive.topology   = wgpu::PrimitiveTopology::TriangleList;
	pipeDesc.primitive.frontFace  = wgpu::FrontFace::CCW;
	pipeDesc.primitive.cullMode   = wgpu::CullMode::None;
	pipeDesc.depthStencil         = nullptr;
	pipeDesc.multisample.count    = 1;
	pipeDesc.multisample.mask     = ~0u;
	pipeDesc.fragment             = &fragState;

	m_depthPreviewPipeline = ctx->pipelineFactory().createRenderPipeline(pipeDesc);
	if (!m_depthPreviewPipeline)
	{
		spdlog::error("DepthPreview: failed to create render pipeline");
		return false;
	}

	return true;
}

bool MainDemoImGuiUI::renderDepthPreviewBlit(
	const std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> &depthSource
)
{
	if (!depthSource) return false;
	if (!ensureDepthPreviewPipeline()) return false;

	auto ctx = m_engine.getContext();
	if (!ctx) return false;

	// (Re)allocate the preview target when the source dimensions change.
	// Identity-fingerprint the source so a soft-clear that swaps the
	// WebGPUTexture wrapper (factory rebuild after Clear All) also forces
	// a fresh preview texture — keeps the preview's bind group's wgpu
	// internal refs pointing at live source views.
	const uint32_t srcW = depthSource->getWidth();
	const uint32_t srcH = depthSource->getHeight();
	const bool needsRealloc =
		!m_depthPreviewTarget ||
		m_depthPreviewSourceFingerprint != depthSource.get() ||
		m_depthPreviewSourceWidth  != srcW ||
		m_depthPreviewSourceHeight != srcH;

	if (needsRealloc)
	{
		// Match the source's aspect ratio at a fixed width, same convention
		// as the colour thumbnails so the depth row reads at the same
		// scale.
		const uint32_t targetW = 256;
		const float    aspect  = srcH > 0 ? float(srcW) / float(srcH) : 1.0f;
		const uint32_t targetH = std::max(1u, uint32_t(targetW / std::max(aspect, 0.01f)));

		m_depthPreviewTarget = ctx->textureFactory().createColorRenderTarget(
			"DepthPreview.Target",
			targetW, targetH,
			wgpu::TextureFormat::RGBA8Unorm,
			WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
		);
		if (!m_depthPreviewTarget) return false;

		m_depthPreviewSourceFingerprint = depthSource.get();
		m_depthPreviewSourceWidth       = srcW;
		m_depthPreviewSourceHeight      = srcH;
	}

	// Build the depth-sampling bind group on the fly. Cheap (one entry); not
	// worth caching across frames given the depth source could move after
	// any GBuffer resize. createBindGroup bumps wgpu's refcount internally,
	// so the textureView value-copy doesn't need explicit refcount management.
	wgpu::BindGroupEntry entry{};
	entry.binding     = 0;
	entry.textureView = depthSource->getTextureView();

	wgpu::BindGroup bindGroup = ctx->bindGroupFactory().createBindGroup(
		m_depthPreviewBindGroupLayout, std::vector<wgpu::BindGroupEntry>{entry});
	if (!bindGroup) return false;

	// Record + submit a one-shot encoder. Submitting from inside the ImGui
	// callback is safe — wgpu sequences queue submissions in order, so
	// this lands before the subsequent UI render pass samples the preview
	// texture.
	wgpu::CommandEncoder encoder = ctx->createCommandEncoder("DepthPreview.Encoder");

	wgpu::RenderPassColorAttachment colorAttach{};
	colorAttach.view       = m_depthPreviewTarget->getTextureView();
	colorAttach.loadOp     = wgpu::LoadOp::Clear;
	colorAttach.storeOp    = wgpu::StoreOp::Store;
	colorAttach.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};

	wgpu::RenderPassDescriptor rpDesc{};
	rpDesc.label                  = "DepthPreview.RenderPass";
	rpDesc.colorAttachmentCount   = 1;
	rpDesc.colorAttachments       = &colorAttach;
	rpDesc.depthStencilAttachment = nullptr;

	wgpu::RenderPassEncoder pass = encoder.beginRenderPass(rpDesc);
	pass.setPipeline(m_depthPreviewPipeline);
	pass.setBindGroup(0, bindGroup, 0, nullptr);
	pass.draw(3, 1, 0, 0); // fullscreen triangle from vertex_index
	pass.end();

	ctx->submitCommandEncoder(encoder, "DepthPreview.Commands");

	// Release the per-frame bind group ref. The wgpu render pass internally
	// retains it through submission, so this just drops our C++ side.
	bindGroup.release();

	return true;
}

void MainDemoImGuiUI::renderPassControlsWindow()
{
	auto renderer = m_engine.getRenderer().lock();
	if (!renderer)
		return;

	ImGui::Begin("Pass Controls", &m_showPassControls);

	ImGui::TextWrapped(
		"Toggle individual passes live. Disabled passes are skipped each "
		"frame; downstream consumers see whatever the last enabled frame "
		"left in the target (great for A/B tests, dangerous for actual "
		"renders). The list order matches the in-engine pipeline order."
	);
	ImGui::Separator();

	auto passes = renderer->getAllPasses();

	// Bulk toggles save 8 clicks for the common "give me JUST the GBuffer"
	// debug intent.
	if (ImGui::Button("Enable all"))
	{
		for (auto *p : passes) p->setEnabled(true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Disable all"))
	{
		for (auto *p : passes) p->setEnabled(false);
	}
	ImGui::SameLine();
	int aliveCount = 0;
	for (auto *p : passes) if (p->isEnabled()) ++aliveCount;
	ImGui::Text("(%d / %d active)", aliveCount, static_cast<int>(passes.size()));

	ImGui::Separator();

	// Per-pass row with a checkbox. ImGui::Checkbox writes through to a bool
	// so we trampoline via a local + setEnabled to keep the API surface
	// pass-side clean.
	for (auto *pass : passes)
	{
		bool enabled = pass->isEnabled();
		ImGui::PushID(pass);
		if (ImGui::Checkbox(pass->name(), &enabled))
		{
			pass->setEnabled(enabled);
			spdlog::info("Pass '{}' {}", pass->name(), enabled ? "enabled" : "disabled");
		}
		ImGui::PopID();
	}

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Stage Preview (G-Buffer)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextWrapped(
			"Live thumbnails of each G-buffer slot as the deferred pipeline "
			"writes them. Same pattern as Shadow Map Debug: each ImGui::Image "
			"samples the texture view directly, no extra pass needed. Empty "
			"if GBufferPass is disabled above (the textures keep their "
			"contents from the last enabled frame)."
		);

		auto *gbufPass = renderer->getGBufferPass();
		if (gbufPass)
		{
			auto &gbuf = gbufPass->getGBuffer();
			const float aspect = gbuf.getHeight() > 0
				? static_cast<float>(gbuf.getWidth()) / static_cast<float>(gbuf.getHeight())
				: 1.0f;
			const float thumbW = 256.0f;
			const float thumbH = thumbW / std::max(aspect, 0.01f);
			const ImVec2 thumbSize{thumbW, thumbH};

			const char *slotLabels[] = {
				"Position (world.xyz, viewDepth.w)",
				"Normal (world)",
				"Albedo (RGB) + roughness (A)",
				"Material (metallic / AO / flags)",
				"Emission (HDR linear)",
			};

			// Black backdrop drawn under every thumbnail. The G-buffer slots
			// pack non-coverage data into the alpha channel (RT3 alpha =
			// materialType id ≈ small int / 255; RT4 alpha = unused ≈ 0),
			// so ImGui::Image's straight-alpha blend reveals the panel
			// background through the geometry pixels of those slots. Drawing
			// a filled black rect at the image position first means the
			// transparent areas reveal black instead, which reads as
			// "this slot's alpha is data, not coverage" without us needing
			// a separate alpha-force blit pass.
			auto drawBlackBackdrop = [&](const ImVec2 &size)
			{
				const ImVec2 p0 = ImGui::GetCursorScreenPos();
				const ImVec2 p1{p0.x + size.x, p0.y + size.y};
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));
			};

			auto &colors = gbuf.getColorTextures();
			for (size_t i = 0; i < colors.size(); ++i)
			{
				if (!colors[i]) continue;
				const char *label = i < std::size(slotLabels) ? slotLabels[i] : "(unnamed)";
				ImGui::Text("RT%zu: %s", i, label);
				drawBlackBackdrop(thumbSize);
				// Same pattern the shadow debug uses: wgpu::TextureView is
				// constructible-to-ImTextureID via the imgui-wgpu shim.
				ImTextureID texId = colors[i]->getTextureView();
				ImGui::Image(
					texId,
					thumbSize, ImVec2(0, 0), ImVec2(1, 1),
					ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0)
				);
				ImGui::Separator();
			}

			// Depth: render through a linearise-blit pass that reads the
			// Depth32Float source via textureLoad (no sampler needed —
			// avoids the "filterable-Float vs Depth" sample-type mismatch
			// that crashes ImGui::Image on a raw depth view) and writes
			// grayscale to an RGBA8Unorm preview texture. The blit's
			// command encoder submits inside this UI callback; wgpu
			// sequences submissions in order so the preview lands before
			// the subsequent UI render pass samples it.
			if (auto depth = gbuf.getDepthTexture())
			{
				if (renderDepthPreviewBlit(depth) && m_depthPreviewTarget)
				{
					ImGui::Text("Depth (near = white)");
					ImTextureID depthPreviewId = m_depthPreviewTarget->getTextureView();
					ImGui::Image(
						depthPreviewId,
						thumbSize, ImVec2(0, 0), ImVec2(1, 1),
						ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0)
					);
				}
				else
				{
					ImGui::TextDisabled("Depth: preview blit not available.");
				}
			}
		}
		else
		{
			ImGui::TextDisabled("No GBufferPass available.");
		}
	}

	ImGui::End();
}

void MainDemoImGuiUI::renderLightingAndCameraControls()
{
	// Shader reload button
	if (ImGui::Button("Reload Shaders (F5)"))
	{
		m_engine.getContext()->shaderRegistry().reloadAllShaders();
		m_engine.getContext()->pipelineManager().reloadAllPipelines();
	}
	ImGui::SameLine();
	// Debug rendering toggle
	static bool showDebugRendering = false;
	static bool showDebugShadowMaps = false;
	static bool prevDebugState = false;
	ImGui::Checkbox("Debug Rendering", &showDebugRendering);
	ImGui::Checkbox("Debug Shadow Maps", &showDebugShadowMaps);
	if (showDebugRendering != prevDebugState)
	{
		for (auto &light : m_lightNodes)
		{
			if (light)
				light->setDebugEnabled(showDebugRendering);
		}
		if (m_rootNode)
		{
			for (auto &child : m_rootNode->getChildren())
			{
				if (child->isSpatial())
				{
					child->setDebugEnabled(showDebugRendering);
				}
			}
		}
		prevDebugState = showDebugRendering;
	}
	m_engine.getRenderer().lock()->getShadowPass().setDebugMode(showDebugShadowMaps);

	// Environment controls: skybox visibility and IBL contribution are
	// independent toggles (see Renderer.cpp comment near irradianceEnabled).
	// "Visible" draws the sky behind everything; "Lighting" feeds the
	// diffuse irradiance + GGX-prefiltered specular into PBR shading. With
	// raw HDR equirects, lighting on top of direct lights tends to read as
	// "everything shiny" on rough surfaces, so the demo starts with the
	// lighting half off — toggle it on to A/B the env's contribution.
	if (auto scene = m_engine.getSceneManager()->getActiveScene())
	{
		auto cameras = scene->getActiveCameras();
		if (!cameras.empty() && cameras[0])
		{
			auto &cam = cameras[0];
			ImGui::SeparatorText("Environment");
			bool skyVisible = cam->isSkyboxEnabled();
			if (ImGui::Checkbox("Skybox visible", &skyVisible))
				cam->setSkyboxEnabled(skyVisible);
			bool iblLighting = cam->isIrradianceEnabled();
			if (ImGui::Checkbox("IBL lighting (skybox lights scene)", &iblLighting))
				cam->setIrradianceEnabled(iblLighting);
			float iblIntensity = cam->getIrradianceIntensity();
			if (ImGui::SliderFloat("Env intensity", &iblIntensity, 0.0f, 2.0f))
				cam->setIrradianceIntensity(iblIntensity);
		}
	}
}

void MainDemoImGuiUI::renderMaterialProperties()
{
	float windowWidth = ImGui::GetWindowWidth();
	// Single-topic window — content shown directly (no redundant header).
	if (m_rootNode)
	{
		auto children = m_rootNode->getChildrenOfType<engine::scene::nodes::ModelRenderNode>();
		std::set<engine::rendering::MaterialHandle> materials;
		for (auto &child : children)
		{
			auto modelHandle = child->getModel();
			auto firstModelOpt = modelHandle.get();
			if (firstModelOpt.has_value())
			{
				auto firstModel = firstModelOpt.value();
				for (auto const &sm : firstModel->getSubmeshes())
				{
					materials.emplace(sm.material);
				}
			}
		}
		for (const auto &materialHandle : materials)
		{
			ImGui::PushID(materialHandle.id());
			auto materialOpt = materialHandle.get();
			ImGui::Separator();
			if (!materialOpt.has_value())
			{
				ImGui::Text("Material not found in manager");
				continue;
			}
			auto material = materialOpt.value();
			auto text = std::string("Material Handle: ") + material->getName().value_or("Unnamed");
			ImGui::Indent();
			if (ImGui::CollapsingHeader(text.c_str()))
			{
				auto materialProperties = material->getProperties<engine::rendering::PBRProperties>();
				bool materialsChanged = false;
				materialsChanged |= ImGui::ColorEdit4("Diffuse (Kd)", &materialProperties.diffuse[0]);
				materialsChanged |= ImGui::ColorEdit4("Emission (Ke)", &materialProperties.emission[0]);
				materialsChanged |= ImGui::ColorEdit4("Transmittance (Kt)", &materialProperties.transmittance[0]);
				materialsChanged |= ImGui::ColorEdit4("Ambient (Ka)", &materialProperties.ambient[0]);
				materialsChanged |= ImGui::SliderFloat("Roughness (Pr)", &materialProperties.roughness, 0.0f, 1.0f);
				materialsChanged |= ImGui::SliderFloat("Metallic (Pm)", &materialProperties.metallic, 0.0f, 1.0f);
				materialsChanged |= ImGui::SliderFloat("IOR (Ni)", &materialProperties.ior, 0.0f, 5.0f);
				if (materialsChanged)
				{
					material->setProperties(materialProperties);
				}
				for (const auto &[textureSlot, textureHandle] : material->getTextures())
				{
					ImGui::PushID(textureSlot.c_str());

					// Slot name (label)
					ImGui::TextUnformatted(textureSlot.c_str());

					if (textureHandle.valid())
					{
						auto textureOpt = textureHandle.get();
						if (textureOpt.has_value())
						{
							auto texture = textureOpt.value();
							ImTextureID imguiTex = getOrCreateImGuiTexture(textureHandle);

							ImVec2 thumbSize(windowWidth - 64.0f, 32.0f);

							ImGui::Image(imguiTex, thumbSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));

							if (ImGui::IsItemHovered())
							{
								auto texturePathStr = texture->getFilePath();
								std::filesystem::path texturePath(texturePathStr);
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(texturePath.filename().string().c_str());
								ImGui::Separator();
								ImGui::Text("Size: %u x %u", texture->getWidth(), texture->getHeight());
								ImGui::TextWrapped("%s", texturePath.string().c_str());
								ImGui::EndTooltip();
							}
						}
						else
						{
							// Missing texture in manager
							ImGui::Dummy(ImVec2(windowWidth - 64.0f, 32.0f));
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted("Texture not found");
								ImGui::EndTooltip();
							}
						}
					}

					ImGui::PopID();
				}
			}
			ImGui::Unindent();
			ImGui::PopID();
		}
	}
}

void MainDemoImGuiUI::renderLightsSection()
{
	// Single-topic window — content shown directly (no redundant header).
	{
		// Display light count with limit
		ImGui::Text("Lights: %zu / 512", m_lightNodes.size());
		ImGui::SameLine();
		if (m_lightNodes.size() >= 512)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(MAX)");
		}

		// Add Light button with limit check
		const bool canAddLight = m_lightNodes.size() < 512;
		if (!canAddLight) ImGui::BeginDisabled();
		
		if (ImGui::Button("Add Light"))
		{
			if (m_lightNodes.size() < 512)
			{
				auto newLight = std::make_shared<engine::scene::nodes::LightNode>();
				// Default to directional light if first, point light otherwise
				if (m_lightNodes.empty())
				{
					engine::rendering::DirectionalLight directionalData;
					directionalData.color = glm::vec3(1.0f);
					directionalData.intensity = 1.0f;
					newLight->getLight().setData(directionalData);

					float pitch = 140.0f, yaw = -30.0f, roll = 0.0f;
					glm::quat rot = glm::quat(glm::radians(glm::vec3(pitch, yaw, roll)));
					newLight->getTransform().setLocalRotation(rot);
					m_lightDirectionsUI[m_lightNodes.size()] = glm::vec3(pitch, yaw, roll);
				}
				else
				{
					engine::rendering::PointLight pointData;
					pointData.color = glm::vec3(1.0f);
					pointData.intensity = 1.0f;
					newLight->getLight().setData(pointData);
					// Place new point lights near scene center by default
					newLight->getTransform().setLocalPosition(glm::vec3(0.0f, 5.0f, 0.0f));
				}
				m_rootNode->addChild(newLight);
				m_lightNodes.push_back(newLight);
				spdlog::info("Added light node ({} / 512)", m_lightNodes.size());
			}
			else
			{
				spdlog::warn("Maximum light count (512) reached");
			}
		}
		
		if (!canAddLight) ImGui::EndDisabled();
		
		if (!canAddLight)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Max lights reached");
		}
		for (size_t i = 0; i < m_lightNodes.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			auto &light = m_lightNodes[i];
			if (!light)
			{
				ImGui::PopID();
				continue;
			}
			const char *lightTypeNames[] = {"Ambient", "Directional", "Point", "Spot"};
			bool open = ImGui::TreeNodeEx(("Light " + std::to_string(i)).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::SameLine(ImGui::GetWindowWidth() - 70);
			bool shouldRemove = false;
			if (ImGui::SmallButton("Remove"))
			{
				shouldRemove = true;
			}
			if (open)
			{
				int currentType = static_cast<int>(light->getLightType());
				if (ImGui::Combo("Type", &currentType, lightTypeNames, 4))
				{
					// Change light type by creating new light data
					switch (currentType)
					{
					case 0: // Ambient
					{
						engine::rendering::AmbientLight ambientData;
						ambientData.color = light->getColor();
						ambientData.intensity = light->getIntensity();
						light->getLight().setData(ambientData);
						break;
					}
					case 1: // Directional
					{
						engine::rendering::DirectionalLight directionalData;
						directionalData.color = light->getColor();
						directionalData.intensity = light->getIntensity();
						light->getLight().setData(directionalData);
						break;
					}
					case 2: // Point
					{
						engine::rendering::PointLight pointData;
						pointData.color = light->getColor();
						pointData.intensity = light->getIntensity();
						light->getLight().setData(pointData);
						break;
					}
					case 3: // Spot
					{
						engine::rendering::SpotLight spotData;
						spotData.color = light->getColor();
						spotData.intensity = light->getIntensity();
						light->getLight().setData(spotData);
						break;
					}
					}
				}
				glm::vec3 color = light->getColor();
				if (ImGui::ColorEdit3("Color", glm::value_ptr(color)))
				{
					light->setColor(color);
				}
				float intensity = light->getIntensity();
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 100.0f))
				{
					light->setIntensity(intensity);
				}
				auto &lightTransform = light->getTransform();
				glm::vec3 position = lightTransform.getLocalPosition();
				if (m_lightDirectionsUI.find(i) == m_lightDirectionsUI.end())
				{
					// We store Euler angles separately for ImGui.
					// Converting from quaternion every frame is unstable because
					// Euler representations are not unique and can cause angle jumps
					// and slider jitter in the UI.
					glm::quat rotation = lightTransform.getRotation();
					glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(rotation));
					m_lightDirectionsUI[i] = eulerAngles;
				}
				glm::vec3 &angles = m_lightDirectionsUI[i];
				if (!light->getLight().isAmbient() && !light->getLight().isDirectional())
				{
					if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
					{
						lightTransform.setLocalPosition(position);
					}
				}
				if (light->getLight().isDirectional() || light->getLight().isSpot())
				{
					if (ImGui::DragFloat3("Direction (degrees)", glm::value_ptr(angles), 0.5f))
					{
						glm::quat rot = glm::quat(glm::radians(angles));
						lightTransform.setLocalRotation(rot);
					}
				}
				if (light->getLight().isSpot())
				{
					// Access spot light data directly
					auto &spotData = light->getLight().asSpot();
					float spotAngleDegrees = glm::degrees(spotData.spotAngle) * 2.0f; // Full cone angle
					if (ImGui::SliderFloat("Cone Angle (degrees)", &spotAngleDegrees, 1.0f, 180.0f))
					{
						spotData.spotAngle = glm::radians(spotAngleDegrees / 2.0f);
					}
					float spotSoftness = spotData.spotSoftness;
					if (ImGui::SliderFloat("Edge Softness", &spotSoftness, 0.0f, 0.99f, "%.2f"))
					{
						spotData.spotSoftness = spotSoftness;
					}
				}

				// Shadow casting controls (for directional, point, and spot lights)
				if (!light->getLight().isAmbient())
				{
					bool castShadows = light->getCastShadows();
					if (ImGui::Checkbox("Cast Shadows", &castShadows))
					{
						light->setCastShadows(castShadows);
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			if (shouldRemove)
			{
				if (light->getParent())
					light->getParent()->removeChild(light);
				m_lightNodes.erase(m_lightNodes.begin() + i);
				m_lightDirectionsUI.erase(i);
				std::map<size_t, glm::vec3> newDirectionsUI;
				for (const auto &[idx, ang] : m_lightDirectionsUI)
				{
					if (idx > i)
						newDirectionsUI[idx - 1] = ang;
					else
						newDirectionsUI[idx] = ang;
				}
				m_lightDirectionsUI = newDirectionsUI;
				break;
			}
		}
	}

}

void MainDemoImGuiUI::renderFlockControls()
{
	ImGui::TextWrapped(
		"Sponza-style scattered point lights — the clustered-shading stress "
		"test. Tune the parameters, then Spawn / Re-apply. Animation runs "
		"every frame while Enable is checked, even if this panel is closed."
	);
	ImGui::Separator();

	ImGui::Checkbox("Enable", &m_flockEnabled);
	ImGui::SliderInt("Count", &m_flockAmount, 0, 1000);
	ImGui::SliderFloat("Intensity", &m_flockIntensity, 0.0f, 500.0f);
	ImGui::SliderFloat("Range",     &m_flockRange,     0.5f, 30.0f);
	ImGui::SliderFloat("Marker scale", &m_flockMarkerScale, 0.01f, 1.0f);
	ImGui::SliderFloat("Bob amplitude", &m_flockBobAmplitude, 0.0f, 5.0f);
	ImGui::SliderFloat("Bob speed",     &m_flockBobSpeed,     0.0f, 5.0f);
	ImGui::InputFloat3("Center", glm::value_ptr(m_flockCenter));
	ImGui::InputFloat3("Extent", glm::value_ptr(m_flockExtent));

	if (ImGui::Button("Spawn / Re-apply"))
	{
		// Easiest way to re-randomise positions / colours on parameter change.
		clearFlock();
		if (m_flockAmount > 0)
			spawnFlock(m_flockAmount);
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		clearFlock();
		m_flockEnabled = false;
	}
}

// Saturated HSV-based palette gives a Sponza-style rainbow scatter rather than
// the muted pastels the old `0.5 + rand*0.5` produced.
static glm::vec3 hsvToRgb(float h, float s, float v)
{
	const float c = v * s;
	const float hp = h * 6.0f;
	const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
	glm::vec3 rgb;
	if      (hp < 1.0f) rgb = {c, x, 0};
	else if (hp < 2.0f) rgb = {x, c, 0};
	else if (hp < 3.0f) rgb = {0, c, x};
	else if (hp < 4.0f) rgb = {0, x, c};
	else if (hp < 5.0f) rgb = {x, 0, c};
	else                rgb = {c, 0, x};
	return rgb + glm::vec3(v - c);
}

void MainDemoImGuiUI::spawnFlock(int amount)
{
	amount = std::min(1000, std::max(0, amount));

	const int current = static_cast<int>(m_flockLights.size());
	if (amount == current)
		return;

	if (amount < current)
	{
		for (int i = amount; i < current; ++i)
		{
			auto node = m_flockLights[i];
			if (node && node->getParent())
				node->getParent()->removeChild(node);
		}
		m_flockLights.resize(amount);
		m_flockOrigins.resize(amount);
		m_flockNoisePhases.resize(amount);
		return;
	}

	spdlog::info("Spawning scattered lights: current={}, target={}", current, amount);

	for (int i = current; i < amount; ++i)
	{
		auto newLight = std::make_shared<engine::scene::nodes::LightNode>();

		// HSV cycle gives evenly distributed saturated hues across the count.
		float hue = static_cast<float>(i) / static_cast<float>(std::max(amount, 1));
		glm::vec3 col = hsvToRgb(hue, 1.0f, 1.0f);

		engine::rendering::PointLight pointData;
		pointData.color       = col;
		pointData.intensity   = m_flockIntensity;
		pointData.range       = m_flockRange;
		pointData.castShadows = false;
		newLight->getLight().setData(pointData);

		// Uniform scatter inside the configured extent box around center.
		auto frand = [](float lo, float hi) {
			return lo + (hi - lo) * (static_cast<float>(rand()) / RAND_MAX);
		};
		glm::vec3 pos = m_flockCenter + glm::vec3(
			frand(-m_flockExtent.x, m_flockExtent.x),
			frand(-m_flockExtent.y, m_flockExtent.y),
			frand(-m_flockExtent.z, m_flockExtent.z)
		);
		newLight->getTransform().setLocalPosition(pos);
		if (m_rootNode)
			m_rootNode->addChild(newLight);

		// No gizmo on scattered lights: spawning N of them used to add N
		// debug-line-drawer enables AND N marker-sphere ModelRenderNodes
		// (each forcing its own Model → Mesh → Material due to per-light
		// color), which inflated the MeshFactory / MaterialFactory caches.
		// Lights still emit photons; they just don't paint themselves.

		m_flockLights.push_back(newLight);
		m_lightNodes.push_back(newLight);
		m_flockOrigins.push_back(pos);
		m_flockNoisePhases.push_back(frand(0.0f, 2.0f * 3.14159265f));
	}

	m_flockEnabled = true;
	spdlog::info("Scattered lights spawn complete. total={}", m_flockLights.size());
}

void MainDemoImGuiUI::clearFlock()
{
	for (auto &node : m_flockLights)
	{
		if (node && node->getParent())
			node->getParent()->removeChild(node);
	}
	m_flockLights.clear();
	m_flockOrigins.clear();
	m_flockNoisePhases.clear();
}

void MainDemoImGuiUI::updateFlock(float deltaSeconds)
{
	// Per-light independent sin-based bobbing around the static origin. Cheaper
	// and more readable than boids - the Sponza-style demo wants the eye to
	// register a stable scatter of glowing bulbs, not a swarm.
	for (size_t i = 0; i < m_flockLights.size(); ++i)
	{
		auto &node = m_flockLights[i];
		if (!node || i >= m_flockOrigins.size() || i >= m_flockNoisePhases.size())
			continue;

		float &phase = m_flockNoisePhases[i];
		phase += deltaSeconds * m_flockBobSpeed;

		glm::vec3 bob(
			std::sin(phase),
			std::sin(phase * 1.37f + 1.0f),
			std::sin(phase * 1.73f + 2.0f)
		);
		node->getTransform().setLocalPosition(m_flockOrigins[i] + bob * m_flockBobAmplitude);
	}
}

ImTextureID MainDemoImGuiUI::getOrCreateImGuiTexture(engine::rendering::TextureHandle textureHandle)
{
	auto it = m_imguiTextureCache.find(textureHandle);
	if (it != m_imguiTextureCache.end())
		return it->second;

	auto textureOpt = textureHandle.get();
	if (!textureOpt.has_value())
		return nullptr;

	auto gpuTexture = m_engine.getContext()->textureFactory().createFromHandle(textureHandle);
	auto textureView = gpuTexture->getTextureView();
	ImTextureID imguiId = (ImTextureID)textureView;

	m_imguiTextureCache[textureHandle] = imguiId;
	return imguiId;
}

} // namespace demo
