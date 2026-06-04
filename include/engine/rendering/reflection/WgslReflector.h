#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "engine/rendering/reflection/ShaderReflection.h"
#include "engine/rendering/reflection/WgslLexer.h"

namespace engine::rendering::reflection
{

/// Diagnostic emitted when reflection cannot make sense of the source. Non-fatal
/// for callers that want to handle a partial result; the data they DO get is
/// always self-consistent (the reflector skips constructs it can't resolve
/// rather than emit nonsense).
struct Diagnostic
{
	std::string message;
	uint32_t    line   = 0;
	uint32_t    column = 0;
};

struct ReflectResult
{
	ShaderReflection         reflection;
	std::vector<Diagnostic>  diagnostics;
	[[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

/// Reflects WGSL source into a ShaderReflection. Pure function of the input
/// string - no file I/O, no engine dependencies, fully testable in isolation.
[[nodiscard]] ReflectResult reflectWgsl(std::string_view source, std::string path = {});

} // namespace engine::rendering::reflection
