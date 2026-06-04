#include "engine/rendering/MeshPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

namespace engine::rendering
{

MeshPass::MeshPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool MeshPass::initialize()
{
	// MeshPass is the legacy forward path. After the deferred renderer landed
	// it stopped being driven; the canonical Scene consolidation removed the
	// standalone Light/Shadow/Environment bind groups MeshPass was wired
	// against. Keep the class alive (other plumbing still constructs it) but
	// skip the resource setup. Forward-path callers will be migrated to use
	// the Scene bind group when this gets reactivated.
	spdlog::info("MeshPass initialized (no-op; deferred path is active)");
	return true;
}

void MeshPass::render(FrameCache &frameCache)
{
	// MeshPass is the legacy forward path. After the deferred renderer landed
	// it stopped being driven; nothing in Renderer calls render() any more.
	// The class is kept so other plumbing (Renderer holds a unique_ptr) keeps
	// compiling — when forward rendering comes back it'll be rewritten on top
	// of the consolidated Scene bind group.
	(void)frameCache;
}

void MeshPass::cleanup()
{
}

} // namespace engine::rendering
