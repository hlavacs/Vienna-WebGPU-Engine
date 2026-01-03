#include "engine/ui/ImGuiManager.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>
#include <SDL.h>
#include <spdlog/spdlog.h>

namespace engine::ui
{

ImGuiManager::~ImGuiManager()
{
	shutdown();
}

bool ImGuiManager::initialize(SDL_Window* window, std::shared_ptr<engine::rendering::webgpu::WebGPUContext> context)
{
	if (m_initialized)
	{
		spdlog::warn("ImGuiManager already initialized");
		return true;
	}

	if (!window)
	{
		spdlog::error("Cannot initialize ImGuiManager: window is null");
		return false;
	}

	if (!context)
	{
		spdlog::error("Cannot initialize ImGuiManager: context is null");
		return false;
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(window, nullptr);

	// Setup WebGPU backend
	wgpu::Device device = context->getDevice();
	WGPUDevice wgpuDevice = device;
	WGPUTextureFormat rtFormat = static_cast<WGPUTextureFormat>(context->getSwapChainFormat());
	WGPUTextureFormat depthFormat = WGPUTextureFormat_Undefined; // No depth for UI rendering
	
	ImGui_ImplWGPU_Init(wgpuDevice, 3, rtFormat, depthFormat);

	m_initialized = true;
	spdlog::info("ImGuiManager initialized");
	return true;
}

void ImGuiManager::shutdown()
{
	if (!m_initialized)
		return;

	// Clear callbacks
	m_frameCallbacks.clear();

	// Shutdown backends
	ImGui_ImplWGPU_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	m_initialized = false;
	spdlog::info("ImGuiManager shut down");}

void ImGuiManager::addFrame(UIFrameCallback callback)
{
	if (callback)
	{
		m_frameCallbacks.push_back(callback);
	}
}

void ImGuiManager::clearFrames()
{
	m_frameCallbacks.clear();
}

void ImGuiManager::render(wgpu::RenderPassEncoder renderPass)
{
	if (!m_initialized || m_frameCallbacks.empty())
		return;

	// Start new ImGui frame
	ImGui_ImplWGPU_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	// Execute all frame callbacks
	for (auto& callback : m_frameCallbacks)
	{
		callback();
	}

	// Render ImGui
	ImGui::Render();
	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData)
	{
		ImGui_ImplWGPU_RenderDrawData(drawData, renderPass);
	}
}

} // namespace engine::ui
