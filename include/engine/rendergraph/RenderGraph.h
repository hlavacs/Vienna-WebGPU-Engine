#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <webgpu/webgpu.hpp>

namespace engine::rendergraph
{

/**
 * @brief Type of a render-graph resource.
 *
 * Mirrors what the renderer can wire up to a pass. Buffer + ColorTexture +
 * DepthTexture cover everything the current engine passes touch; expand
 * here when a new pass needs e.g. a storage texture or cube map.
 */
enum class ResourceType : uint8_t
{
	ColorTexture,
	DepthTexture,
	StorageTexture,
	Buffer,
};

/**
 * @brief Descriptor for a transient resource the graph allocates on
 *        compile. External resources (swapchain texture, persistent
 *        buffers) are imported by name + handle instead.
 */
struct ResourceDesc
{
	std::string         name;
	ResourceType        type   = ResourceType::ColorTexture;
	uint32_t            width  = 0;
	uint32_t            height = 0;
	wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
	std::size_t         size   = 0;  ///< Buffer types only.
};

/// Opaque handle for a resource within a RenderGraph. 0 = invalid.
struct ResourceHandle
{
	uint32_t id = 0;
	bool valid() const { return id != 0; }
	bool operator==(const ResourceHandle &o) const { return id == o.id; }
	bool operator!=(const ResourceHandle &o) const { return id != o.id; }
};

/// Opaque handle for a pass within a RenderGraph. 0 = invalid.
struct PassHandle
{
	uint32_t id = 0;
	bool valid() const { return id != 0; }
	bool operator==(const PassHandle &o) const { return id == o.id; }
	bool operator!=(const PassHandle &o) const { return id != o.id; }
};

class RenderGraph;

/**
 * @brief Builder a Pass receives during its `setup()` call.
 *
 * Use it to declare read/write dependencies on already-imported or
 * already-created resources, and to create new transient resources
 * that subsequent passes can read. Declarations are how the graph
 * computes the execution order — `pass A writes X; pass B reads X`
 * implies A runs before B.
 */
class PassBuilder
{
  public:
	PassBuilder(RenderGraph &graph, PassHandle ownerPass);

	/// Mark @p resource as read by this pass. The graph ensures any
	/// upstream writer runs first.
	void read(ResourceHandle resource);

	/// Mark @p resource as written by this pass. Subsequent passes that
	/// read it will be scheduled after this one.
	void write(ResourceHandle resource);

	/// Allocate a new transient resource owned by the graph. Returned
	/// handle is immediately valid to declare further reads/writes
	/// against (the pass that calls create() is implicitly the writer).
	ResourceHandle create(const ResourceDesc &desc);

  private:
	RenderGraph &m_graph;
	PassHandle   m_pass;
};

/**
 * @brief Per-frame context handed to a Pass's `execute()` call.
 *
 * Carries the wgpu encoder so passes can record their commands. Future
 * fields here: resolved resource pointers (textures / buffers / views)
 * looked up by ResourceHandle so passes don't have to stash them in
 * member variables.
 */
struct RenderContext
{
	wgpu::CommandEncoder *encoder = nullptr;
	// Resolved transient/imported resources — keyed by ResourceHandle::id.
	// Populated by the graph's executor before calling each pass.
	std::unordered_map<uint32_t, wgpu::Texture> textures;
	std::unordered_map<uint32_t, wgpu::Buffer>  buffers;
};

/**
 * @brief Base type for every pass in a render graph.
 *
 * Lifecycle:
 *   1. Constructor — store any per-pass config (shader, format, etc.)
 *   2. `setup(builder)` — declare resource dependencies. Called once when
 *      the graph compiles.
 *   3. `execute(ctx)` — record GPU commands. Called once per frame from
 *      the graph's executor, in the compile-determined order.
 */
class Pass
{
  public:
	virtual ~Pass() = default;

	virtual const char *name() const = 0;
	virtual void setup(PassBuilder &builder) = 0;
	virtual void execute(RenderContext &ctx) = 0;
};

/**
 * @brief Adapter that wraps a lambda + a list of declared reads/writes as
 *        a Pass. Lets the renderer migrate to a graph-driven order without
 *        rewriting every pass into its own subclass — the lambda holds
 *        whatever orchestration the legacy code path did.
 *
 * Typical use:
 * @code
 *   auto colorTarget = graph.addImported("Backbuffer", ResourceType::ColorTexture);
 *   graph.addPass(std::make_unique<FunctionPass>(
 *       "Skybox",
 *       std::vector<ResourceHandle>{},                 // reads
 *       std::vector<ResourceHandle>{ colorTarget },    // writes
 *       [&](RenderContext &) {
 *           m_skyboxPass->render(m_frameCache);
 *       }));
 * @endcode
 */
class FunctionPass : public Pass
{
  public:
	using ExecuteFn = std::function<void(RenderContext &)>;

	FunctionPass(std::string                 name,
	             std::vector<ResourceHandle> reads,
	             std::vector<ResourceHandle> writes,
	             ExecuteFn                   exec) :
		m_name(std::move(name)),
		m_reads(std::move(reads)),
		m_writes(std::move(writes)),
		m_exec(std::move(exec)) {}

	const char *name() const override { return m_name.c_str(); }

	void setup(PassBuilder &b) override
	{
		for (auto r : m_reads)  if (r.valid()) b.read(r);
		for (auto w : m_writes) if (w.valid()) b.write(w);
	}

	void execute(RenderContext &ctx) override
	{
		if (m_exec) m_exec(ctx);
	}

  private:
	std::string                 m_name;
	std::vector<ResourceHandle> m_reads;
	std::vector<ResourceHandle> m_writes;
	ExecuteFn                   m_exec;
};

/**
 * @brief Container of passes + resources with a compile pass that orders
 *        them by dependency.
 *
 * Workflow:
 *   1. addPass / addImported to register passes and external resources
 *   2. compile() — runs setup() on every pass, resolves the read/write
 *      dependency graph into an execution order
 *   3. execute(ctx) — pumps the compiled order, calling each pass's
 *      execute() with the resolved RenderContext
 *
 * Compiling is deterministic and re-runnable: changing resource
 * dimensions or adding/removing passes is a recompile, not an
 * incremental edit. The expected pattern is "compile once at
 * window resize / pipeline change, execute every frame".
 */
class RenderGraph
{
  public:
	/// Import an external resource (e.g. the swapchain backbuffer, a
	/// persistent UBO). The graph treats it as both readable and
	/// writable — pass declarations against it form the dependency
	/// constraints. Returns the handle to use in connect() calls.
	ResourceHandle addImported(const std::string &name, ResourceType type);

	/// Add a pass; the graph takes ownership. Returns the pass's handle
	/// (useful for diagnostics; not needed for normal flow since edges
	/// are inferred from setup()'s resource declarations).
	PassHandle addPass(std::unique_ptr<Pass> pass);

	/// Compile: run every pass's setup(), build the read/write graph,
	/// topologically sort. Detects cycles (write-after-write where two
	/// passes both write the same resource with no read in between, or
	/// genuine A→B→A loops). Returns true on success.
	struct CompileResult
	{
		bool        success = false;
		std::string error;
	};
	CompileResult compile();

	/// Run every pass in compile-determined order. Caller fills
	/// @p ctx with the resolved resources for this frame.
	void execute(RenderContext &ctx);

	/// Reset state before re-compiling (e.g. after window resize). Passes
	/// stay registered; only the dependency graph + order are cleared.
	void resetCompileState();

	// Internal hooks for PassBuilder — public so the builder can call
	// them without a friend declaration but not part of the user-facing
	// API. Don't call directly.
	void _recordRead (PassHandle pass, ResourceHandle resource);
	void _recordWrite(PassHandle pass, ResourceHandle resource);
	ResourceHandle _recordCreate(PassHandle pass, const ResourceDesc &desc);

	[[nodiscard]] std::size_t passCount()     const { return m_passes.size(); }
	[[nodiscard]] std::size_t resourceCount() const { return m_resources.size(); }
	[[nodiscard]] bool isCompiled() const { return m_compiled; }

	/// Pass names in compiled execution order. Useful for logging the
	/// graph's decision so the renderer can confirm "this matches the
	/// hand-coded sequence I'm migrating from" or print the order in a
	/// debug overlay. Returns empty until compile() succeeds.
	[[nodiscard]] std::vector<std::string> compiledOrder() const;

  private:
	struct StoredPass
	{
		std::unique_ptr<Pass>         pass;
		std::vector<ResourceHandle>   reads;
		std::vector<ResourceHandle>   writes;
	};

	struct StoredResource
	{
		ResourceDesc desc;
		bool         imported = false;
	};

	std::unordered_map<uint32_t, StoredPass>     m_passes;
	std::unordered_map<uint32_t, StoredResource> m_resources;
	std::vector<PassHandle>                      m_executionOrder;

	uint32_t m_nextPassId     = 1;
	uint32_t m_nextResourceId = 1;
	bool     m_compiled       = false;
};

} // namespace engine::rendergraph
