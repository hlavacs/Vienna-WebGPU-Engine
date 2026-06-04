#include "MainDemoImGuiUI.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/Model.h"
#include "engine/resources/MaterialManager.h"
#include "engine/resources/ModelManager.h"
#include "engine/resources/ResourceManager.h"
#include "engine/scene/nodes/ModelRenderNode.h"

namespace demo
{

MainDemoImGuiUI::MainDemoImGuiUI(
	engine::GameEngine &engine
) : m_engine(engine)
{
	m_rootNode = engine.getSceneManager()->getActiveScene()->getRoot();
	for (const auto &child : m_rootNode->getChildrenOfType<engine::scene::nodes::LightNode>())
	{
		m_lightNodes.push_back(child);
	};

	auto renderer = m_engine.getRenderer().lock();
	m_debugShadowCubeArray = renderer->getShadowPass().DEBUG_SHADOW_CUBE_ARRAY;
	m_debugShadow2DArray = renderer->getShadowPass().DEBUG_SHADOW_2D_ARRAY;
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
	ImGui::Begin("Lighting & Camera Controls");

	renderLightingAndCameraControls();
	ImGui::Separator();
	renderMaterialProperties();
	renderLightsSection();
	ImGui::End();
}

void MainDemoImGuiUI::renderPerformanceWindow()
{
	ImGui::Begin("Performance");
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
			reg.clearAll();
			spdlog::info("Manual clearAll dropped every cache entry");
		}
	}
	ImGui::End();
}

void MainDemoImGuiUI::renderShadowDebugWindow()
{
	auto renderer = m_engine.getRenderer().lock();
	if (!renderer || !renderer->getShadowPass().isDebugMode())
		return;

	ImGui::Begin("Shadow Map Debug");

	const int thumbSize = 128;
	const int columns = 3;

	// --- Cube array debug ---
	if (m_debugShadowCubeArray)
	{
		if (ImGui::CollapsingHeader("Cube Shadow Maps"))
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
		if (ImGui::CollapsingHeader("2D Shadow Maps"))
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
}

void MainDemoImGuiUI::renderMaterialProperties()
{
	float windowWidth = ImGui::GetWindowWidth();
	if (ImGui::CollapsingHeader("Material Properties") && m_rootNode)
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
	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
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

	// Free-flying point lights controls & update (SeaKeep-style)
	renderFlockControls();
}

void MainDemoImGuiUI::renderFlockControls()
{
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Scattered Point Lights (Sponza-style)"))
	{
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

		if (m_flockEnabled && !m_flockLights.empty())
		{
			float dt = m_engine.getFrameTime() * 0.001f; // ms -> seconds
			updateFlock(dt);
		}
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

	auto resourceManager = m_engine.getResourceManager();
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
		newLight->setDebugEnabled(true);

		// Marker sphere co-located with the light.
		if (resourceManager)
		{
			std::string instanceName = "sphere_inst_" + std::to_string(newLight->getId());
			auto spherePath = engine::core::PathProvider::getResource("sphere.obj");
			auto maybeModelInst = resourceManager->m_modelManager->createModel(
				spherePath, std::optional<std::string>(instanceName));
			if (maybeModelInst.has_value())
			{
				engine::rendering::PBRProperties props{};
				props.diffuse[0]  = col.r; props.diffuse[1] = col.g; props.diffuse[2] = col.b; props.diffuse[3] = 1.0f;
				// Strong emission so the marker reads as a glowing bulb against
				// the deferred-lit background (deferred composition adds emission
				// post-lighting, so values can sit comfortably above 1 in linear HDR).
				props.emission[0] = col.r * 5.0f; props.emission[1] = col.g * 5.0f; props.emission[2] = col.b * 5.0f;
				props.emission[3] = 1.0f;
				props.roughness   = 1.0f;
				props.metallic    = 0.0f;

				auto maybeMat = resourceManager->m_materialManager->createPBRMaterial(
					"SphereMat_" + instanceName, props, {});
				if (maybeMat.has_value())
				{
					auto matHandle = maybeMat.value()->getHandle();
					for (auto &sm : maybeModelInst.value()->getSubmeshes())
						sm.material = matHandle;
				}

				auto marker = std::make_shared<engine::scene::nodes::ModelRenderNode>(maybeModelInst.value());
				marker->getTransform().setLocalScale(glm::vec3(m_flockMarkerScale));
				// Skip shadow extraction (visual-only marker).
				marker->setCastsShadows(false);
				// keepWorldTransform=false: the marker has no world position of
				// its own, we want it anchored at the light's origin and to
				// follow the light when updateFlock moves it. With the default
				// keepWorld=true, addChild would rewrite local = -lightPos to
				// hold the marker at (0,0,0) - it would then track only the
				// bob delta, not the light's absolute motion.
				newLight->addChild(marker, false);
			}
		}

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
