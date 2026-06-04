#include "engine/rendergraph/RenderGraph.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace engine::rendergraph
{

// ---------------------------------------------------------------------------
// PassBuilder
// ---------------------------------------------------------------------------

PassBuilder::PassBuilder(RenderGraph &graph, PassHandle ownerPass) :
	m_graph(graph),
	m_pass(ownerPass)
{
}

void PassBuilder::read(ResourceHandle resource)
{
	m_graph._recordRead(m_pass, resource);
}

void PassBuilder::write(ResourceHandle resource)
{
	m_graph._recordWrite(m_pass, resource);
}

ResourceHandle PassBuilder::create(const ResourceDesc &desc)
{
	return m_graph._recordCreate(m_pass, desc);
}

// ---------------------------------------------------------------------------
// RenderGraph: registration
// ---------------------------------------------------------------------------

ResourceHandle RenderGraph::addImported(const std::string &name, ResourceType type)
{
	ResourceHandle h{m_nextResourceId++};
	StoredResource sr{};
	sr.desc.name = name;
	sr.desc.type = type;
	sr.imported  = true;
	m_resources.emplace(h.id, std::move(sr));
	m_compiled = false;
	return h;
}

PassHandle RenderGraph::addPass(std::unique_ptr<Pass> pass)
{
	PassHandle h{m_nextPassId++};
	StoredPass sp{};
	sp.pass = std::move(pass);
	m_passes.emplace(h.id, std::move(sp));
	m_compiled = false;
	return h;
}

void RenderGraph::_recordRead(PassHandle pass, ResourceHandle resource)
{
	auto it = m_passes.find(pass.id);
	if (it == m_passes.end() || !resource.valid()) return;
	it->second.reads.push_back(resource);
}

void RenderGraph::_recordWrite(PassHandle pass, ResourceHandle resource)
{
	auto it = m_passes.find(pass.id);
	if (it == m_passes.end() || !resource.valid()) return;
	it->second.writes.push_back(resource);
}

ResourceHandle RenderGraph::_recordCreate(PassHandle pass, const ResourceDesc &desc)
{
	ResourceHandle h{m_nextResourceId++};
	StoredResource sr{};
	sr.desc     = desc;
	sr.imported = false;
	m_resources.emplace(h.id, std::move(sr));

	// Pass that called create() is the implicit writer.
	auto it = m_passes.find(pass.id);
	if (it != m_passes.end())
	{
		it->second.writes.push_back(h);
	}
	return h;
}

// ---------------------------------------------------------------------------
// RenderGraph: compile
// ---------------------------------------------------------------------------

void RenderGraph::resetCompileState()
{
	m_executionOrder.clear();
	for (auto &kv : m_passes)
	{
		kv.second.reads.clear();
		kv.second.writes.clear();
	}
	// Drop transient resources (keep imported ones — they survive across
	// recompiles by definition).
	for (auto it = m_resources.begin(); it != m_resources.end();)
	{
		if (!it->second.imported)
			it = m_resources.erase(it);
		else
			++it;
	}
	m_compiled = false;
}

RenderGraph::CompileResult RenderGraph::compile()
{
	CompileResult result{};
	m_executionOrder.clear();

	// Step 1: run each pass's setup() through a PassBuilder. This populates
	// the read/write lists on every StoredPass.
	for (auto &kv : m_passes)
	{
		PassHandle  handle{kv.first};
		PassBuilder builder(*this, handle);
		kv.second.pass->setup(builder);
	}

	// Step 2: build a "last writer" map — for each resource, which pass
	// most recently wrote it? Two writers without a reader between them is
	// a write-after-write hazard the graph rejects (the second pass
	// silently clobbers the first — usually a bug).
	std::unordered_map<uint32_t, PassHandle> lastWriter;  // resource id → writer
	// We'll process passes in registration order to detect write/write
	// conflicts before computing the real topological order.
	std::vector<PassHandle> registrationOrder;
	registrationOrder.reserve(m_passes.size());
	for (const auto &kv : m_passes)
		registrationOrder.push_back(PassHandle{kv.first});
	std::sort(registrationOrder.begin(), registrationOrder.end(),
	          [](PassHandle a, PassHandle b) { return a.id < b.id; });

	// Step 3: build edges. For each pass P and each resource R it reads,
	// add an edge "last-writer-of-R → P". For each resource it writes,
	// update lastWriter to P after we've processed P's reads. This is the
	// canonical render-graph dependency rule.
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> edges;       // pass.id → {successor pass.id}
	std::unordered_map<uint32_t, uint32_t>                     inDegree;
	for (PassHandle ph : registrationOrder) inDegree[ph.id] = 0;

	for (PassHandle ph : registrationOrder)
	{
		const StoredPass &p = m_passes.at(ph.id);

		// Reads create dependencies on the prior writer.
		for (ResourceHandle r : p.reads)
		{
			auto it = lastWriter.find(r.id);
			if (it != lastWriter.end() && it->second.id != ph.id)
			{
				auto &succ = edges[it->second.id];
				if (succ.insert(ph.id).second)
				{
					inDegree[ph.id] += 1;
				}
			}
		}

		// Writes update lastWriter. We don't reject write-after-write here
		// outright — the second writer just overwrites. Add a debug log
		// hook if you want to flag those in the future.
		for (ResourceHandle r : p.writes)
		{
			lastWriter[r.id] = ph;
		}
	}

	// Step 4: Kahn's algorithm — repeatedly emit passes with no remaining
	// incoming edges. Use a FIFO so ties are broken by registration order
	// (deterministic + matches the hand-coded sequence when the deps match).
	// std::vector with front()+erase(begin()) is O(n²) in the worst case;
	// fine for the dozens-of-passes range we expect. Swap for std::deque if
	// the graph ever grows larger.
	std::vector<PassHandle> ready;
	for (PassHandle ph : registrationOrder)
	{
		if (inDegree[ph.id] == 0) ready.push_back(ph);
	}

	while (!ready.empty())
	{
		PassHandle ph = ready.front();
		ready.erase(ready.begin());
		m_executionOrder.push_back(ph);

		auto edgeIt = edges.find(ph.id);
		if (edgeIt == edges.end()) continue;
		// Iterate successors in deterministic order — std::unordered_set's
		// iteration is unspecified. Materialise + sort so tie-breaking by
		// registration id holds across runs / compilers.
		std::vector<uint32_t> successors(edgeIt->second.begin(), edgeIt->second.end());
		std::sort(successors.begin(), successors.end());
		for (uint32_t successorId : successors)
		{
			if (--inDegree[successorId] == 0)
			{
				ready.push_back(PassHandle{successorId});
			}
		}
	}

	if (m_executionOrder.size() != m_passes.size())
	{
		std::ostringstream os;
		os << "Cycle in render graph: scheduled " << m_executionOrder.size()
		   << " of " << m_passes.size() << " passes";
		result.error = os.str();
		m_executionOrder.clear();
		return result;
	}

	m_compiled    = true;
	result.success = true;
	return result;
}

// ---------------------------------------------------------------------------
// RenderGraph: execute
// ---------------------------------------------------------------------------

void RenderGraph::execute(RenderContext &ctx)
{
	if (!m_compiled) return;
	for (PassHandle ph : m_executionOrder)
	{
		auto it = m_passes.find(ph.id);
		if (it == m_passes.end()) continue;
		it->second.pass->execute(ctx);
	}
}

std::vector<std::string> RenderGraph::compiledOrder() const
{
	std::vector<std::string> out;
	out.reserve(m_executionOrder.size());
	for (PassHandle ph : m_executionOrder)
	{
		auto it = m_passes.find(ph.id);
		if (it != m_passes.end() && it->second.pass)
		{
			out.emplace_back(it->second.pass->name());
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// Compile-time probe
// ---------------------------------------------------------------------------

namespace
{

// Minimal stub passes to exercise the API at engine-lib build time. No
// runtime effect — purely catches header / API drift before the first
// real pass migrates.
class StubReadWritePass : public Pass
{
  public:
	StubReadWritePass(const char *n, ResourceHandle read_, ResourceHandle write_) :
		m_name(n), m_read(read_), m_write(write_) {}
	const char *name() const override { return m_name; }
	void setup(PassBuilder &b) override
	{
		if (m_read.valid())  b.read(m_read);
		if (m_write.valid()) b.write(m_write);
	}
	void execute(RenderContext &) override {}
  private:
	const char    *m_name;
	ResourceHandle m_read;
	ResourceHandle m_write;
};

[[maybe_unused]] void renderGraphCompileProbe()
{
	RenderGraph g;
	auto color = g.addImported("Backbuffer", ResourceType::ColorTexture);
	auto depth = g.addImported("Depth",      ResourceType::DepthTexture);

	// Geometry pass writes depth + color.
	auto geom  = g.addPass(std::make_unique<StubReadWritePass>("Geometry", ResourceHandle{}, color));
	(void)geom;
	// Postprocess reads color, writes color.
	auto post  = g.addPass(std::make_unique<StubReadWritePass>("Postprocess", color, color));
	(void)post;

	auto compileResult = g.compile();
	(void)compileResult;
	RenderContext ctx{};
	g.execute(ctx);
	(void)depth;
	g.resetCompileState();
}

} // namespace

} // namespace engine::rendergraph
