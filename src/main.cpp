#define SDL_MAIN_HANDLED
#define WEBGPU_CPP_IMPLEMENTATION
#define WEBGPU_BACKEND_DAWN

#include "sdl2webgpu.h"
#include <SDL2/SDL.h>
#include "webgpu/webgpu.hpp"

#include <cassert>
#include <string>

static WGPUStringView WGPU_STR(std::string str) {
	return WGPUStringView { str.c_str(), str.length() };
} 

using namespace wgpu;

WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;

// We embbed the source of the shader module here
const char *shaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
	var p = vec2f(0.0, 0.0);
	if (in_vertex_index == 0u) {
		p = vec2f(-0.5, -0.5);
	} else if (in_vertex_index == 1u) {
		p = vec2f(0.5, -0.5);
	} else {
		p = vec2f(0.0, 0.5);
	}
	return vec4f(p, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
	return vec4f(0.0, 0.4, 1.0, 1.0);
}
)";

#pragma region Print Informations

void inspectAdapter(const WGPUAdapter &adapter)
{
#ifndef __EMSCRIPTEN__
	WGPULimits supportedLimits = {};
	supportedLimits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
	bool success = wgpuAdapterGetLimits(adapter, &supportedLimits) == WGPUStatus_Success;
#else
	bool success = wgpuAdapterGetLimits(adapter, &supportedLimits);
#endif

	if (success)
	{
		std::cout << "Adapter limits:" << std::endl;
		std::cout << " - maxTextureDimension1D: " << supportedLimits.maxTextureDimension1D << std::endl;
		std::cout << " - maxTextureDimension2D: " << supportedLimits.maxTextureDimension2D << std::endl;
		std::cout << " - maxTextureDimension3D: " << supportedLimits.maxTextureDimension3D << std::endl;
		std::cout << " - maxTextureArrayLayers: " << supportedLimits.maxTextureArrayLayers << std::endl;
	}
#endif // NOT __EMSCRIPTEN__
}

void inspectDevice(WGPUDevice device)
{
	WGPUSupportedFeatures supportedFeatures = {};
	supportedFeatures.features = nullptr; // Ensure the pointer is null initially

	wgpuDeviceGetFeatures(device, &supportedFeatures);

	std::vector<WGPUFeatureName> features(supportedFeatures.featureCount);
	supportedFeatures.features = features.data();

	std::cout << "Device features:" << std::endl;
	std::cout << std::hex;
	for (auto f : features)
	{
		std::cout << " - 0x" << f << std::endl;
	}
	std::cout << std::dec;

	WGPULimits limits = {};
	limits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
	bool success = wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success;
#else
	bool success = wgpuDeviceGetLimits(device, &limits);
#endif

	if (success)
	{
		std::cout << "Device limits:" << std::endl;
		std::cout << " - maxTextureDimension1D: " << limits.maxTextureDimension1D << std::endl;
		std::cout << " - maxTextureDimension2D: " << limits.maxTextureDimension2D << std::endl;
		std::cout << " - maxTextureDimension3D: " << limits.maxTextureDimension3D << std::endl;
		std::cout << " - maxTextureArrayLayers: " << limits.maxTextureArrayLayers << std::endl;
	}
}

void inspectAdapterProperties(const WGPUAdapter &adapter)
{
	WGPUAdapterInfo info = {};
	WGPUStatus status = wgpuAdapterGetInfo(adapter, &info);
	if (status != WGPUStatus_Success)
	{
		printf("Failed to get adapter info.\n");
		return;
	}
	info.nextInChain = nullptr;

	printf("Adapter Information:\n");
	printf("  Name: %s\n", info.device.data);
	printf("  Vendor: %s\n", info.vendor.data);
	printf("  Architecture: %s\n", info.architecture.data);
	printf("  Description: %s\n", info.description.data);
	printf("  Vendor ID: %u\n", info.vendorID);
	printf("  Device ID: %u\n", info.deviceID);
	printf("  Backend Type: %d\n", info.backendType);
	printf("  Adapter Type: %d\n", info.adapterType);

	std::cout << std::hex;
	std::cout << " - adapterType: 0x" << info.adapterType << std::endl;
	std::cout << " - backendType: 0x" << info.backendType << std::endl;
	std::cout << std::dec;
}
#pragma endregion

#pragma region Helpers
/**
 * Utility function to get a WebGPU adapter, so that
 *     WGPUAdapter adapter = requestAdapterSync(options);
 * is roughly equivalent to
 *     const adapter = await navigator.gpu.requestAdapter(options);
 */
WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const *options)
{
	// A simple structure holding the local information shared with the
	// onAdapterRequestEnded callback.
	struct UserData
	{
		WGPUAdapter adapter = nullptr;
		bool requestEnded = false;
	};
	UserData userData;

	// Callback called by wgpuInstanceRequestAdapter when the request returns
	auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *userdata2)
	{
		auto &userData = *reinterpret_cast<UserData *>(userdata1);
		if (status == WGPURequestAdapterStatus_Success)
		{
			userData.adapter = adapter;
		}
		else
		{
			std::cerr << "Could not get WebGPU adapter: " << message.data << std::endl;
		}
		userData.requestEnded = true;
	};
	WGPURequestAdapterCallbackInfo callbackInfo = {};
	callbackInfo.callback = onAdapterRequestEnded;
	callbackInfo.userdata1 = &userData;
	callbackInfo.userdata2 = nullptr;
	callbackInfo.mode = WGPUCallbackMode::WGPUCallbackMode_WaitAnyOnly;

	// Call to the WebGPU request adapter procedure
	wgpuInstanceRequestAdapter(
		instance /* equivalent of navigator.gpu */,
		options,
		callbackInfo);

// We wait until userData.requestEnded gets true
#ifdef __EMSCRIPTEN__
	while (!userData.requestEnded)
	{
		emscripten_sleep(100);
	}
#endif // __EMSCRIPTEN__

	assert(userData.requestEnded);

	return userData.adapter;
}

/**
 * Utility function to get a WebGPU device, so that
 *     WGPUDevice device = requestDeviceSync(adapter, options);
 * is roughly equivalent to
 *     const device = await adapter.requestDevice(descriptor);
 * It is very similar to requestAdapter
 */
WGPUDevice requestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor const *descriptor)
{
	struct UserData
	{
		WGPUDevice device = nullptr;
		bool requestEnded = false;
	};
	UserData userData;

	auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata1, void *userdata2)
	{
		auto &userData = *reinterpret_cast<UserData *>(userdata1);
		if (status == WGPURequestDeviceStatus_Success)
		{
			userData.device = device;
		}
		else
		{
			std::cerr << "Could not get WebGPU device: " << message.data << std::endl;
		}
		userData.requestEnded = true;
	};
	WGPURequestDeviceCallbackInfo callbackInfo = {};
	callbackInfo.callback = onDeviceRequestEnded;
	callbackInfo.userdata1 = &userData;
	callbackInfo.userdata2 = nullptr;

	wgpuAdapterRequestDevice(
		adapter,
		descriptor,
		callbackInfo);

#ifdef __EMSCRIPTEN__
	while (!userData.requestEnded)
	{
		emscripten_sleep(100);
	}
#endif // __EMSCRIPTEN__

	assert(userData.requestEnded);

	return userData.device;
}

#pragma endregion

WGPUColorTargetState createColorTargetState()
{
	WGPUBlendState *blendState = new WGPUBlendState();
	blendState->color.srcFactor = WGPUBlendFactor_SrcAlpha;
	blendState->color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
	blendState->color.operation = WGPUBlendOperation_Add;
	blendState->alpha.srcFactor = WGPUBlendFactor_Zero;
	blendState->alpha.dstFactor = WGPUBlendFactor_One;
	blendState->alpha.operation = WGPUBlendOperation_Add;

	WGPUColorTargetState colorTarget{};
	colorTarget.format = surfaceFormat;
	colorTarget.blend = blendState;
	colorTarget.writeMask = WGPUColorWriteMask_All; // We could write to only some of the color channels.

	return colorTarget;
}

WGPUShaderModule createShaderModule(const WGPUDevice &device)
{
	// Define the shader module descriptor
	WGPUShaderModuleDescriptor shaderDesc = {};
	shaderDesc.label = WGPU_STR("My Shader Module Descriptor");

	// Define the shader source descriptor
	WGPUShaderSourceWGSL shaderSourceDesc = {};
	shaderSourceDesc.chain.next = nullptr;
	shaderSourceDesc.chain.sType = WGPUSType::WGPUSType_ShaderSourceWGSL;
	std::string shaderSourceStr (shaderSource);
	shaderSourceDesc.code = WGPU_STR(shaderSourceStr);

	shaderDesc.nextInChain = &shaderSourceDesc.chain;

	// Create the shader module
	WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

	if (shaderModule == nullptr)
	{
		std::cerr << "Failed to create shader module." << std::endl;
	}

	return shaderModule;
}

WGPURenderPipeline createPipeline(const WGPUDevice &device, WGPUShaderModule &shaderModule)
{
	WGPURenderPipelineDescriptor *pipelineDesc = new WGPURenderPipelineDescriptor();
	pipelineDesc->nextInChain = nullptr;
	// Configure 'pipelineDesc->vertex'
	pipelineDesc->vertex.bufferCount = 0;
	pipelineDesc->vertex.buffers = nullptr;
	pipelineDesc->vertex.module = shaderModule;
	pipelineDesc->vertex.entryPoint = WGPU_STR("vs_main");
	pipelineDesc->vertex.constantCount = 0;
	pipelineDesc->vertex.constants = nullptr;

	// Primitive pipeline state
	// Each sequence of 3 vertices is considered as a triangle
	pipelineDesc->primitive.topology = WGPUPrimitiveTopology_TriangleList;
	// We'll see later how to specify the order in which vertices should be
	// connected. When not specified, vertices are considered sequentially.
	pipelineDesc->primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
	// The face orientation is defined by assuming that when looking
	// from the front of the face, its corner vertices are enumerated
	// in the counter-clockwise (CCW) order.
	pipelineDesc->primitive.frontFace = WGPUFrontFace_CCW;
	// But the face orientation does not matter much because we do not
	// cull (i.e. "hide") the faces pointing away from us (which is often
	// used for optimization).
	pipelineDesc->primitive.cullMode = WGPUCullMode_None;

	// We tell that the programmable fragment shader stage is described
	// by the function called 'fs_main' in the shader module.
	WGPUFragmentState *fragmentState = new WGPUFragmentState();
	fragmentState->module = shaderModule;
	fragmentState->entryPoint = {"fs_main", 7};
	fragmentState->constantCount = 0;
	fragmentState->constants = nullptr;

	// We have only one target because our render pass has only one output color
	// attachment.
	WGPUColorTargetState colorTargetState = createColorTargetState();
	fragmentState->targetCount = 1;
	fragmentState->targets = &colorTargetState;
	pipelineDesc->fragment = fragmentState;

	// Samples per pixel
	pipelineDesc->multisample.count = 1;
	// Default value for the mask, meaning "all bits on"
	pipelineDesc->multisample.mask = ~0u;
	// Default value as well (irrelevant for count = 1 anyways)
	pipelineDesc->multisample.alphaToCoverageEnabled = false;

	pipelineDesc->layout = nullptr;
	WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, pipelineDesc);

	return pipeline;
}

WGPURenderPipeline initializePipeline(const WGPUDevice &device)
{
	auto shaderModule = createShaderModule(device);
	auto pipeline = createPipeline(device, shaderModule);

	// We no longer need to access the shader module
	wgpuShaderModuleRelease(shaderModule);
	return pipeline;
}

WGPUTextureView nextTargetView(const WGPUSurface &surface)
{
	// Get the surface texture
	WGPUSurfaceTexture surfaceTexture;
	wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
	if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus::WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus::WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
	{
		return nullptr;
	}

	// Create a view for this surface texture
	WGPUTextureViewDescriptor viewDescriptor;
	viewDescriptor.nextInChain = nullptr;
	viewDescriptor.label = WGPU_STR("Surface texture view");
	viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
	viewDescriptor.dimension = WGPUTextureViewDimension_2D;
	viewDescriptor.baseMipLevel = 0;
	viewDescriptor.mipLevelCount = 1;
	viewDescriptor.baseArrayLayer = 0;
	viewDescriptor.arrayLayerCount = 1;
	viewDescriptor.aspect = WGPUTextureAspect_All;
	WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

	return targetView;
}

void loop(
	const WGPUDevice &device,
	const WGPUAdapter &adapter,
	const WGPURenderPipeline &pipeline,
	const WGPUSurface &surface,
	const WGPUQueue &queue)
{
	// Get the next target texture view
	WGPUTextureView targetView = nextTargetView(surface);
	if (!targetView)
		return;

	// Create a command encoder for the draw call
	WGPUCommandEncoderDescriptor encoderDesc = {};
	encoderDesc.nextInChain = nullptr;
	encoderDesc.label = WGPU_STR("My command encoder");
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

	// Create the render pass that clears the screen with our color
	WGPURenderPassDescriptor renderPassDesc = {};
	renderPassDesc.nextInChain = nullptr;

	// The attachment part of the render pass descriptor describes the target texture of the pass
	WGPURenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
	renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
	renderPassColorAttachment.clearValue = WGPUColor{0.9, 0.1, 0.2, 1.0};
#ifndef WEBGPU_BACKEND_WGPU
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;
	renderPassDesc.depthStencilAttachment = nullptr;
	renderPassDesc.timestampWrites = nullptr;

	WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

	// Select which render pipeline to use
	wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
	// Draw 1 instance of a 3-vertices shape
	wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);

	wgpuRenderPassEncoderEnd(renderPass);
	wgpuRenderPassEncoderRelease(renderPass);

	// Encode and submit the render pass
	WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
	cmdBufferDescriptor.nextInChain = nullptr;
	cmdBufferDescriptor.label = WGPU_STR("Command buffer");
	WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
	wgpuCommandEncoderRelease(encoder);

	std::cout << "Submitting command..." << std::endl;
	wgpuQueueSubmit(queue, 1, &command);
	wgpuCommandBufferRelease(command);
	std::cout << "Command submitted." << std::endl;

	// At the enc of the frame
	wgpuTextureViewRelease(targetView);

#if defined(WEBGPU_BACKEND_DAWN)
	wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
	wgpuDevicePoll(device, false, nullptr);
#endif
}

int main()
{
	SDL_SetMainReady();

	WGPUInstanceDescriptor desc = {};
	desc.nextInChain = nullptr;

// We create the instance using this descriptor
#ifdef WEBGPU_BACKEND_EMSCRIPTEN
	WGPUInstance instance = wgpuCreateInstance(nullptr);
#else  //  WEBGPU_BACKEND_EMSCRIPTEN
	WGPUInstance instance = wgpuCreateInstance(&desc);
#endif //  WEBGPU_BACKEND_EMSCRIPTEN

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		std::cerr << "Could not initialize SDL! Error: " << SDL_GetError() << std::endl;
		return 1;
	}
	int windowFlags = 0;
	SDL_Window *window = SDL_CreateWindow("Learn WebGPU", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, windowFlags);
	WGPUSurface surface = SDL_GetWGPUSurface(instance, window);

	// ------------------ Adapter -----------------
	std::cout << "Requesting adapter..." << std::endl;

	WGPURequestAdapterOptions adapterOpts = {};
	adapterOpts.nextInChain = nullptr;
	WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);

	wgpuInstanceRelease(instance);

	std::cout << "Got adapter: " << adapter << std::endl;
	inspectAdapter(adapter);
	inspectAdapterProperties(adapter);

	// ------------------ Device -----------------
	std::cout << "Requesting device..." << std::endl;

	WGPUDeviceDescriptor deviceDesc = {};
	deviceDesc.nextInChain = nullptr;
	deviceDesc.label = WGPU_STR("My Device"); // anything works here, that's your call
	deviceDesc.requiredFeatureCount = 0;	  // we do not require any specific feature
	deviceDesc.requiredLimits = nullptr;	  // we do not require any specific limit
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = WGPU_STR("The default queue");

	auto onDeviceLost = [](WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata1, void *userdata2)
	{
		std::cout << "Device lost: reason " << reason;
		if (message.data)
			std::cout << " (" << message.data << ")";
		std::cout << std::endl;
	};

	WGPUDeviceLostCallbackInfo deviceLostCallbackInfo;
	deviceLostCallbackInfo.nextInChain = nullptr;
	deviceLostCallbackInfo.userdata1 = nullptr;
	deviceLostCallbackInfo.userdata2 = nullptr;
	deviceLostCallbackInfo.mode = WGPUCallbackMode::WGPUCallbackMode_WaitAnyOnly;
	deviceLostCallbackInfo.callback = onDeviceLost;

	deviceDesc.deviceLostCallbackInfo = deviceLostCallbackInfo;

	WGPUDevice device = requestDeviceSync(adapter, &deviceDesc);

	std::cout << "Got device: " << device << std::endl;
	inspectDevice(device);

	// ------------------ Queue -----------------
	std::cout << "Requesting queue..." << std::endl;
	WGPUQueue queue = wgpuDeviceGetQueue(device);

	std::cout << "Got queue: " << queue << std::endl;

	auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void *userdata1, void *userdata2)
	{
		std::cout << "Queued work finished with status: " << status << std::endl;
	};

	WGPUQueueWorkDoneCallbackInfo queueWorkDoneCallbackInfo = {};
	queueWorkDoneCallbackInfo.nextInChain = nullptr; // No chained structures
	queueWorkDoneCallbackInfo.mode = WGPUCallbackMode::WGPUCallbackMode_AllowProcessEvents;
	queueWorkDoneCallbackInfo.callback = onQueueWorkDone;
	queueWorkDoneCallbackInfo.userdata1 = nullptr;
	queueWorkDoneCallbackInfo.userdata2 = nullptr;
	wgpuQueueOnSubmittedWorkDone(queue, queueWorkDoneCallbackInfo);

	// -------------------- Pipeline ----------------------
	SurfaceConfiguration config = {};

	// Configuration of the textures created for the underlying swap chain
	config.width = 640;
	config.height = 480;
	config.usage = TextureUsage::RenderAttachment;
	WGPUSurfaceCapabilities capabilities;
	wgpuSurfaceGetCapabilities(surface, adapter, &capabilities);
	surfaceFormat = capabilities.formats[0];
	config.format = surfaceFormat;

	// And we do not need any particular view format:
	config.viewFormatCount = 0;
	config.viewFormats = nullptr;
	config.device = device;
	config.presentMode = PresentMode::Fifo;
	config.alphaMode = CompositeAlphaMode::Auto;

	wgpuSurfaceConfigure(surface, &config);

	WGPURenderPipeline pipeline = initializePipeline(device);
	wgpuAdapterRelease(adapter);

	// ------------------ SDL Window Loop -----------------
	bool shouldClose = false;
	while (!shouldClose)
	{

		// Poll events and handle them.
		// (contrary to GLFW, close event is not automatically managed, and there
		// is no callback mechanism by default.)
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				shouldClose = true;
				break;

			default:
				break;
			}
		}

		loop(device, adapter, pipeline, surface, queue);
	}

	wgpuRenderPipelineRelease(pipeline);
	wgpuSurfaceUnconfigure(surface);
	wgpuQueueRelease(queue);
	wgpuSurfaceRelease(surface);
	wgpuDeviceRelease(device);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return EXIT_SUCCESS;
}