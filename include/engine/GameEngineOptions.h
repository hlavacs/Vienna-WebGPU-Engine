#pragma once

namespace engine
{
	
/**
 * @struct GameEngineOptions
 * @brief Configuration options for the game engine.
 *
 * This struct contains various settings that control the behavior of the game engine,
 * including timing, physics, rendering, debugging, and audio options.
 */
struct GameEngineOptions
{
	// Timing
	float fixedDeltaTime = 1.0f / 60.0f;
	float maxDeltaTime = 1.0f / 15.0f;
	float targetFrameRate = 60.0f;
	bool enableVSync = true;
	bool limitFrameRate = false;

	// Physics
	int maxSubSteps = 5;
	bool runPhysics = true;

	// Buffering
	int renderBufferCount = 2; // 2 = double, 3 = triple
	bool runRenderThread = true;

	// Debug
	bool showFrameStats = false;
	bool logSubsystemErrors = true;
	bool enableHotReload = false;

	// Window
	int windowWidth = 1280;
	int windowHeight = 720;
	bool fullscreen = false;
	bool resizableWindow = true;

	// Audio
	bool enableAudio = true;
	float masterVolume = 1.0f;
};

} // namespace engine