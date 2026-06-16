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

std::vector<PassHandle> RenderGraph::sortedRegistrationOrder(
	const std::unordered_map<uint32_t, StoredPass> &passes)
{
	std::vector<PassHandle> out;
	out.reserve(passes.size());
	for (const auto &kv : passes) out.push_back(PassHandle{kv.first});
	std::sort(out.begin(), out.end(),
	          [](PassHandle a, PassHandle b) { return a.id < b.id; });
	return out;
}

RenderGraph::DependencyEdges RenderGraph::buildDependencyEdges(
	const std::unordered_map<uint32_t, StoredPass> &passes,
	const std::vector<PassHandle>                  &registrationOrder)
{
	DependencyEdges out{};
	std::unordered_map<uint32_t, PassHandle> lastWriter;
	for (PassHandle ph : registrationOrder) out.inDegree[ph.id] = 0;

	for (PassHandle ph : registrationOrder)
	{
		const StoredPass &p = passes.at(ph.id);
		for (ResourceHandle r : p.reads)
		{
			auto it = lastWriter.find(r.id);
			if (it != lastWriter.end() && it->second.id != ph.id)
			{
				if (out.successors[it->second.id].insert(ph.id).second)
				{
					out.inDegree[ph.id] += 1;
				}
			}
		}
		for (ResourceHandle r : p.writes)
		{
			lastWriter[r.id] = ph;
		}
	}
	return out;
}

std::vector<PassHandle> RenderGraph::kahnTopologicalOrder(
	const std::vector<PassHandle> &registrationOrder,
	DependencyEdges               &edges)
{
	std::vector<PassHandle> ready;
	for (PassHandle ph : registrationOrder)
	{
		if (edges.inDegree[ph.id] == 0) ready.push_back(ph);
	}

	std::vector<PassHandle> order;
	while (!ready.empty())
	{
		PassHandle ph = ready.front();
		ready.erase(ready.begin());
		order.push_back(ph);

		auto edgeIt = edges.successors.find(ph.id);
		if (edgeIt == edges.successors.end()) continue;
		std::vector<uint32_t> successors(edgeIt->second.begin(), edgeIt->second.end());
		std::sort(successors.begin(), successors.end());
		for (uint32_t successorId : successors)
		{
			if (--edges.inDegree[successorId] == 0)
			{
				ready.push_back(PassHandle{successorId});
			}
		}
	}
	return order;
}

RenderGraph::CompileResult RenderGraph::compile()
{
	CompileResult result{};
	m_executionOrder.clear();

	// Run each pass's setup(), populating its read/write lists.
	for (auto &kv : m_passes)
	{
		PassHandle  handle{kv.first};
		PassBuilder builder(*this, handle);
		kv.second.pass->setup(builder);
	}

	const auto registrationOrder = sortedRegistrationOrder(m_passes);
	auto       edges             = buildDependencyEdges(m_passes, registrationOrder);
	m_executionOrder             = kahnTopologicalOrder(registrationOrder, edges);

	if (m_executionOrder.size() != m_passes.size())
	{
		std::ostringstream os;
		os << "Cycle in render graph: scheduled " << m_executionOrder.size()
		   << " of " << m_passes.size() << " passes";
		result.error = os.str();
		m_executionOrder.clear();
		return result;
	}

	m_compiled     = true;
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
		if (!it->second.enabled) continue; // toggled off — leave outputs stale
		it->second.pass->execute(ctx);
	}
}

void RenderGraph::setPassEnabled(PassHandle pass, bool enabled)
{
	auto it = m_passes.find(pass.id);
	if (it == m_passes.end()) return;
	it->second.enabled = enabled;
}

bool RenderGraph::isPassEnabled(PassHandle pass) const
{
	auto it = m_passes.find(pass.id);
	if (it == m_passes.end()) return false;
	return it->second.enabled;
}

std::vector<ResourceHandle> RenderGraph::getPassWrites(PassHandle pass) const
{
	auto it = m_passes.find(pass.id);
	if (it == m_passes.end()) return {};
	return it->second.writes;
}

PassHandle RenderGraph::findPassByName(const std::string &name) const
{
	for (const auto &kv : m_passes)
	{
		if (kv.second.pass && kv.second.pass->name() && name == kv.second.pass->name())
		{
			return PassHandle{kv.first};
		}
	}
	return PassHandle{};
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
