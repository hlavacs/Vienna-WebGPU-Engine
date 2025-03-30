#pragma once

#include "webgpu/webgpu.hpp"
#include <SDL2/SDL.h>

using namespace wgpu;

// TODO: Work in Progress
class Engine {
public:
	bool initialize();

	void terminate();

	void loop();

	bool isRunning();

private:
	SDL_Texture computeTexture();

	void initPipeline();

private:
	// We put here all the variables that are shared between init and main loop
	SDL_Window *window;
	Device device;
	Queue queue;
	Surface surface;
	std::unique_ptr<ErrorCallback> uncapturedErrorCallbackHandle;
	TextureFormat surfaceFormat = TextureFormat::Undefined;
	RenderPipeline pipeline;
};
