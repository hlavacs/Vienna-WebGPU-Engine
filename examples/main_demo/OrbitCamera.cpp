#include "OrbitCamera.h"
#include "engine/NodeSystem.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/resources/ResourceManager.h"
#include "engine/core/PathProvider.h"

namespace demo
{

constexpr float PI = 3.14159265358979323846f;

void updateOrbitCamera(OrbitCameraState &state, std::shared_ptr<engine::scene::nodes::CameraNode> camera)
{
	// Normalize azimuth to [0, 2π]
	state.azimuth = fmod(state.azimuth, 2.0f * PI);
	if (state.azimuth < 0)
		state.azimuth += 2.0f * PI;

	// Clamp elevation to avoid gimbal lock
	state.elevation = glm::clamp(state.elevation, -PI / 2.0f + 0.01f, PI / 2.0f - 0.01f);

	// Clamp distance
	state.distance = glm::clamp(state.distance, 0.5f, 20.0f);

	// Convert spherical coordinates to Cartesian
	float x = cos(state.elevation) * sin(state.azimuth);
	float y = sin(state.elevation);
	float z = cos(state.elevation) * cos(state.azimuth);

	glm::vec3 position = state.targetPoint + glm::vec3(x, y, z) * state.distance;

	// Update camera position and look-at
	if (camera)
	{
		camera->getTransform().setLocalPosition(position);
		camera->lookAt(state.targetPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

void updateDragInertia(OrbitCameraState &state, std::shared_ptr<engine::scene::nodes::CameraNode> camera, float deltaTime)
{
	if (!state.active && glm::length(state.velocity) > 1e-4f)
	{
		// Apply inertia
		state.azimuth += state.velocity.x * state.sensitivity * deltaTime;
		state.elevation += state.velocity.y * state.sensitivity * deltaTime;

		// Decay velocity
		state.velocity *= state.inertiaDecay;

		// Update camera position
		updateOrbitCamera(state, camera);
	}
	else if (!state.active)
	{
		// Stop completely when velocity is negligible
		state.velocity = glm::vec2(0.0f);
	}
}

void OrbitCameraController::screenShot()
{
	if (!m_camera)
		return;
	auto engine = this->engine();
	if (!engine)
		return;
	auto gpuContext = engine->gpu();
	if (!gpuContext)
		return;
	auto resourceManager = engine->resources();
	if (!resourceManager)
		return;

	auto textureHandle = m_camera->getRenderTarget();
	std::shared_ptr<engine::rendering::Texture> texture;
	if(!textureHandle.has_value())
	{
		auto imageOpt = resourceManager->m_imageLoader->createEmpty(1u, 1u, std::nullopt);
		if(!imageOpt.has_value())
		{
			spdlog::error("Failed to create image for screenshot");
			return;
		}
		auto image = imageOpt.value();
		auto textureOpt = resourceManager->m_textureManager->createImageTexture(
			image,
			std::nullopt,
			true
		);
		if (textureOpt.has_value())
		{
			texture = textureOpt.value();
			textureHandle = texture->getHandle();
			m_camera->setRenderTarget(textureHandle);
		}
	}
	else
	{
		texture = textureHandle->get().value_or(nullptr);
	}
	
	if (!texture)
	{
		spdlog::error("Failed to get texture for screenshot");
		return;
	}
	
	// Launch async task: request readback, wait for completion, save
	auto textureWeak = std::weak_ptr<engine::rendering::Texture>(texture);
	auto imageLoader = resourceManager->m_imageLoader;
	auto path = engine::core::PathProvider::getLogs("screenshot_" + std::to_string(std::time(nullptr)) + ".png");
	
	m_saveFuture = std::async(std::launch::async, [textureWeak, imageLoader, path]()
	{
		auto texture = textureWeak.lock();
		if (!texture)
		{
			spdlog::error("Texture destroyed before screenshot completed");
			return;
		}
		
		// Request readback
		texture->requestReadback();
		spdlog::info("Screenshot requested, waiting for GPU readback...");
		
		// Poll for readback completion (on background thread)
		while (texture->isReadbackPending())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		
		// Readback complete, save the image
		auto image = texture->getImage();
		if (image)
		{
			imageLoader->saveAsPNG(*image, path);
			spdlog::info("Screenshot saved to: {}", path.string());
		}
		else
		{
			spdlog::error("Failed to get image from texture");
		}
	});
}

OrbitCameraController::OrbitCameraController(const OrbitCameraState &state, std::shared_ptr<engine::scene::nodes::CameraNode> camera) : m_orbitState(state), m_camera(camera)
{
	updateOrbitCamera(m_orbitState, m_camera);
}

void OrbitCameraController::update(float deltaTime)
{
	// Safety check: ensure we have engine context
	if (!engine())
	{
		spdlog::warn("OrbitCameraController: No engine context available");
		return;
	}

	// Safety check: ensure camera is still valid
	if (!m_camera)
	{
		spdlog::warn("OrbitCameraController: Camera is null");
		return;
	}

	auto input = engine()->input();
	if (!input)
		return;

	if (input->isKeyDown(SDL_SCANCODE_U))
	{
		screenShot();
	}

	// Handle mouse drag for camera rotation
	if (input->isMouse(SDL_BUTTON_LEFT))
	{
		if (!m_orbitState.active)
		{
			// Start dragging
			m_orbitState.active = true;
			m_orbitState.startMouse = input->getMousePosition();
			m_orbitState.velocity = glm::vec2(0.0f);
		}
		else
		{
			// Continue dragging
			auto mouseDelta = input->getMouseDelta();
			glm::vec2 delta(static_cast<float>(mouseDelta.x), static_cast<float>(mouseDelta.y));
			m_orbitState.azimuth -= delta.x * 0.005f;
			m_orbitState.elevation += delta.y * 0.005f;
			m_orbitState.velocity = delta * 0.005f;
			updateOrbitCamera(m_orbitState, m_camera);
		}
	}
	else if (m_orbitState.active)
	{
		// Stop dragging
		m_orbitState.active = false;
	}

	// Handle mouse wheel for zoom
	glm::vec2 wheel = input->getMouseWheel();
	if (wheel.y != 0.0f)
	{
		m_orbitState.distance -= wheel.y * m_orbitState.scrollSensitivity;
		updateOrbitCamera(m_orbitState, m_camera);
	}

	// Apply inertia when not dragging
	updateDragInertia(m_orbitState, m_camera, deltaTime);
}

} // namespace demo
