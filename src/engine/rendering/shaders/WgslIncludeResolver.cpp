#include "engine/rendering/shaders/WgslIncludeResolver.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

#include "engine/core/PathProvider.h"

namespace engine::rendering::shaders
{

namespace
{

constexpr uint32_t kMaxIncludeDepth = 64;

std::optional<std::string> defaultRead(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/// Resolve a `#include "X"` target.
///
/// Scheme-aware:
///  - `engine://rel`  -> `<engine resources>/shaders/rel` (e.g. core/, lib/, ...)
///  - `assets://rel`  -> `<example assets>/shaders/rel`
///  - no scheme       -> `<engine resources>/rel` (legacy behavior; allows
///                       paths like `shaders/foo.wgsl` to still work, useful
///                       for generated includes the registry emits)
///
/// The `shaders/` prefix is implicit in both URI schemes because every
/// `#include` target is by definition a WGSL file.
std::filesystem::path defaultResolve(const std::string &name)
{
	constexpr std::string_view kEnginePrefix = "engine://";
	constexpr std::string_view kAssetsPrefix = "assets://";

	if (name.compare(0, kEnginePrefix.size(), kEnginePrefix) == 0)
	{
		const std::string rel = name.substr(kEnginePrefix.size());
		return engine::core::PathProvider::getResource("shaders") / rel;
	}
	if (name.compare(0, kAssetsPrefix.size(), kAssetsPrefix) == 0)
	{
		const std::string rel = name.substr(kAssetsPrefix.size());
		return engine::core::PathProvider::getAssets("shaders") / rel;
	}
	return engine::core::PathProvider::getResource(name);
}

/// Strip leading whitespace and look for `#include "..."`. Returns the include
/// target if found; std::nullopt otherwise. The lexer is permissive: any
/// whitespace before `#`, between `#include` and the quoted name, and a
/// trailing CR / `// comment` after the closing quote are tolerated.
std::optional<std::string> parseIncludeDirective(std::string_view line)
{
	size_t i = 0;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
	if (i >= line.size() || line[i] != '#') return std::nullopt;
	++i;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
	constexpr std::string_view kKeyword = "include";
	if (line.compare(i, kKeyword.size(), kKeyword) != 0) return std::nullopt;
	i += kKeyword.size();
	if (i >= line.size() || (line[i] != ' ' && line[i] != '\t')) return std::nullopt;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
	if (i >= line.size() || line[i] != '"') return std::nullopt;
	++i;
	size_t start = i;
	while (i < line.size() && line[i] != '"') ++i;
	if (i >= line.size()) return std::nullopt;
	return std::string(line.substr(start, i - start));
}

struct ExpandContext
{
	const WgslIncludeResolver::PathResolver         &resolve;
	const WgslIncludeResolver::FileReader           &read;
	std::unordered_set<std::filesystem::path::string_type> &seen;
	std::vector<std::filesystem::path>              &deps;
	std::vector<IncludeDiagnostic>                  &errors;
};

void expandInto(const std::string &source,
                const std::filesystem::path &sourcePath,
                std::string &out,
                uint32_t depth,
                ExpandContext &ctx)
{
	if (depth >= kMaxIncludeDepth)
	{
		IncludeDiagnostic d;
		d.message = "Include depth limit exceeded (cycle?)";
		d.file    = sourcePath;
		d.line    = 0;
		ctx.errors.push_back(std::move(d));
		return;
	}

	uint32_t lineNo = 0;
	size_t   cursor = 0;
	while (cursor <= source.size())
	{
		++lineNo;
		const size_t lineEnd = source.find('\n', cursor);
		const size_t end     = (lineEnd == std::string::npos) ? source.size() : lineEnd;
		std::string_view line(source.data() + cursor, end - cursor);

		if (auto target = parseIncludeDirective(line))
		{
			const std::filesystem::path resolved = ctx.resolve(*target);
			const auto key = resolved.lexically_normal().native();
			if (ctx.seen.insert(key).second)
			{
				ctx.deps.push_back(resolved);
				auto contents = ctx.read(resolved);
				if (!contents)
				{
					IncludeDiagnostic d;
					d.message = "Failed to read #include \"" + *target + "\" (resolved to " + resolved.string() + ")";
					d.file    = sourcePath;
					d.line    = lineNo;
					ctx.errors.push_back(std::move(d));
				}
				else
				{
					// Emit a `#line`-style breadcrumb so wgpu diagnostics point at the
					// originating file rather than the post-expansion line number.
					out.append("// >>> include ");
					out.append(resolved.string());
					out.push_back('\n');
					expandInto(*contents, resolved, out, depth + 1, ctx);
					out.append("// <<< end include ");
					out.append(resolved.string());
					out.push_back('\n');
				}
			}
			// else: pragma-once dedupe; emit nothing.
		}
		else
		{
			out.append(line.data(), line.size());
			if (lineEnd != std::string::npos) out.push_back('\n');
		}

		if (lineEnd == std::string::npos) break;
		cursor = lineEnd + 1;
	}
}

} // namespace

WgslIncludeResolver::WgslIncludeResolver()
	: m_resolve(&defaultResolve), m_read(&defaultRead) {}

WgslIncludeResolver::WgslIncludeResolver(PathResolver resolver, FileReader reader)
	: m_resolve(std::move(resolver)), m_read(std::move(reader)) {}

IncludeResolveResult WgslIncludeResolver::expand(const std::string &source,
                                                 const std::filesystem::path &sourcePath) const
{
	IncludeResolveResult result;
	std::unordered_set<std::filesystem::path::string_type> seen;

	if (!sourcePath.empty())
	{
		const auto key = sourcePath.lexically_normal().native();
		seen.insert(key);
		result.dependencies.push_back(sourcePath);
	}

	ExpandContext ctx{m_resolve, m_read, seen, result.dependencies, result.errors};
	expandInto(source, sourcePath, result.finalSource, 0, ctx);
	return result;
}

} // namespace engine::rendering::shaders
