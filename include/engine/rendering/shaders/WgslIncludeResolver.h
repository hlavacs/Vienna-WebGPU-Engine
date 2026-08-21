#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace engine::rendering::shaders
{

struct IncludeDiagnostic
{
	std::string           message;
	std::filesystem::path file;
	uint32_t              line = 0;
};

struct IncludeResolveResult
{
	std::string                        finalSource;
	std::vector<std::filesystem::path> dependencies; ///< Absolute paths; includes the entry file.
	std::vector<IncludeDiagnostic>     errors;
};

/**
 * @brief Textual `#include "..."` preprocessor for WGSL.
 *
 * Behavior:
 *  - `#include "path"` lines are replaced with the content of the resolved file.
 *  - Resolution goes through `PathProvider::getResource(name)` by default; a
 *    custom resolver/reader can be injected for tests.
 *  - Each file is included at most once across the entire expansion
 *    (`#pragma once` semantics, by absolute path). Re-inclusions are silently
 *    skipped — duplicate engine-generated headers are normal.
 *  - Recursive includes are followed; cycles are guarded by the dedupe set and
 *    a hard depth limit.
 *  - Every file touched (including the entry source) appears in
 *    `dependencies` for the hot-reload graph.
 *  - Lines that are not `#include` directives pass through unchanged. The WGSL
 *    lexer already eats other `#`-prefixed lines so unexpanded shaders still
 *    parse during authoring.
 */
class WgslIncludeResolver
{
  public:
	using PathResolver = std::function<std::filesystem::path(const std::string &)>;
	using FileReader   = std::function<std::optional<std::string>(const std::filesystem::path &)>;

	/// Construct with PathProvider-backed defaults.
	WgslIncludeResolver();

	/// Construct with injected resolver/reader for tests.
	WgslIncludeResolver(PathResolver resolver, FileReader reader);

	/**
	 * @brief Expand all `#include` directives reachable from @p source.
	 * @param source        WGSL source text to expand.
	 * @param sourcePath    Identity of @p source. Used to seed the
	 *                      pragma-once set and to label diagnostics. Pass the
	 *                      empty path for in-memory sources.
	 */
	[[nodiscard]] IncludeResolveResult expand(const std::string &source,
	                                          const std::filesystem::path &sourcePath) const;

  private:
	PathResolver m_resolve;
	FileReader   m_read;
};

} // namespace engine::rendering::shaders
