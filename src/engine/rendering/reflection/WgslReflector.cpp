#include "engine/rendering/reflection/WgslReflector.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace engine::rendering::reflection
{

namespace
{
// ---------------------------------------------------------------------------
// Type table (WGSL std140-ish layout)
// ---------------------------------------------------------------------------
// Sizes and alignments below come from the WGSL spec ("Memory Layout of Host
// Shareable Types"). vec3 having align 16 while only 12 bytes of data is the
// historical std140 quirk that bites if you forget it.
struct PrimInfo
{
	WgslPrimitive prim;
	const char   *name;
	uint32_t      size;
	uint32_t      align;
};

constexpr PrimInfo kPrimitives[] = {
	{WgslPrimitive::F32,     "f32",        4,  4},
	{WgslPrimitive::I32,     "i32",        4,  4},
	{WgslPrimitive::U32,     "u32",        4,  4},
	{WgslPrimitive::Bool,    "bool",       4,  4},
	{WgslPrimitive::Vec2F,   "vec2<f32>",  8,  8},
	{WgslPrimitive::Vec3F,   "vec3<f32>", 12, 16},
	{WgslPrimitive::Vec4F,   "vec4<f32>", 16, 16},
	{WgslPrimitive::Vec2F,   "vec2f",      8,  8},
	{WgslPrimitive::Vec3F,   "vec3f",     12, 16},
	{WgslPrimitive::Vec4F,   "vec4f",     16, 16},
	{WgslPrimitive::Vec2I,   "vec2<i32>",  8,  8},
	{WgslPrimitive::Vec3I,   "vec3<i32>", 12, 16},
	{WgslPrimitive::Vec4I,   "vec4<i32>", 16, 16},
	{WgslPrimitive::Vec2U,   "vec2<u32>",  8,  8},
	{WgslPrimitive::Vec3U,   "vec3<u32>", 12, 16},
	{WgslPrimitive::Vec4U,   "vec4<u32>", 16, 16},
	{WgslPrimitive::Mat2x2F, "mat2x2<f32>", 16, 8},
	{WgslPrimitive::Mat2x2F, "mat2x2f",     16, 8},
	{WgslPrimitive::Mat3x3F, "mat3x3<f32>", 48, 16},
	{WgslPrimitive::Mat3x3F, "mat3x3f",     48, 16},
	{WgslPrimitive::Mat4x4F, "mat4x4<f32>", 64, 16},
	{WgslPrimitive::Mat4x4F, "mat4x4f",     64, 16},
};

PrimInfo lookupPrim(const std::string &name)
{
	for (const auto &p : kPrimitives)
	{
		if (name == p.name)
			return p;
	}
	return {WgslPrimitive::Unknown, "", 0, 0};
}

uint32_t alignUp(uint32_t value, uint32_t alignment)
{
	if (alignment <= 1)
		return value;
	return (value + alignment - 1) & ~(alignment - 1);
}

// ---------------------------------------------------------------------------
// Annotation decoder
// ---------------------------------------------------------------------------
// Annotations look like:   bind_group(name="Material", reuse=PerObject)
//                          color_target(0, format="RGBA16Float")
//                          depth(compare="LessEqual", write=false)
//                          cull(mode="None")
// We do shallow argument parsing - good enough for our purposes.
struct AnnotationArgs
{
	std::string                                  name;          // function name (e.g. "bind_group")
	std::vector<std::string>                     positional;
	std::unordered_map<std::string, std::string> named;
};

std::optional<AnnotationArgs> parseAnnotation(const std::string &raw)
{
	AnnotationArgs args;
	size_t i = 0;
	auto skipWs = [&]() { while (i < raw.size() && std::isspace((unsigned char)raw[i])) ++i; };

	skipWs();
	while (i < raw.size() && (std::isalnum((unsigned char)raw[i]) || raw[i] == '_'))
		args.name.push_back(raw[i++]);
	if (args.name.empty())
		return std::nullopt;
	skipWs();
	if (i >= raw.size() || raw[i] != '(')
		return args;            // no arg list - that's allowed
	++i;                        // consume '('

	while (i < raw.size())
	{
		skipWs();
		if (raw[i] == ')')
		{
			++i;
			break;
		}

		// peek for "key="
		size_t save = i;
		std::string key;
		while (i < raw.size() && (std::isalnum((unsigned char)raw[i]) || raw[i] == '_'))
			key.push_back(raw[i++]);
		skipWs();

		std::string value;
		bool isNamed = (!key.empty() && i < raw.size() && raw[i] == '=');
		if (isNamed)
		{
			++i;                // consume '='
			skipWs();
		}
		else
		{
			// Positional - rewind and re-read into `value`.
			i = save;
			key.clear();
		}

		// Read value: quoted string, identifier, number, or bool.
		if (i < raw.size() && raw[i] == '"')
		{
			++i;
			while (i < raw.size() && raw[i] != '"')
				value.push_back(raw[i++]);
			if (i < raw.size()) ++i; // consume closing quote
		}
		else
		{
			while (i < raw.size() && raw[i] != ',' && raw[i] != ')')
				value.push_back(raw[i++]);
			// trim trailing whitespace
			while (!value.empty() && std::isspace((unsigned char)value.back()))
				value.pop_back();
		}

		if (isNamed)
			args.named.emplace(std::move(key), std::move(value));
		else
			args.positional.push_back(std::move(value));

		skipWs();
		if (i < raw.size() && raw[i] == ',')
			++i;                // consume ','
	}
	return args;
}

BindGroupReuse parseReuse(const std::string &s)
{
	if (s == "Global")      return BindGroupReuse::Global;
	if (s == "PerFrame")    return BindGroupReuse::PerFrame;
	if (s == "PerCamera")   return BindGroupReuse::PerCamera;
	if (s == "PerObject")   return BindGroupReuse::PerObject;
	if (s == "PerMaterial") return BindGroupReuse::PerMaterial;
	return BindGroupReuse::PerFrame;
}

BindGroupRole parseRole(const std::string &s)
{
	if (s == "Frame")    return BindGroupRole::Frame;
	if (s == "Scene")    return BindGroupRole::Scene;
	if (s == "Material") return BindGroupRole::Material;
	if (s == "Object")   return BindGroupRole::Object;
	// Old role names collapsed into Scene under the canonical layout.
	if (s == "Light" || s == "Shadow" || s == "Environment" || s == "Cluster")
		return BindGroupRole::Scene;
	return BindGroupRole::Custom;
}

// ---------------------------------------------------------------------------
// Known-struct fallback
// ---------------------------------------------------------------------------
struct KnownStructMeta
{
	const char     *structName;
	const char     *groupName;
	BindGroupReuse  reuse;
	BindGroupRole   role;
};

// Canonical layout (see SKILL.md "Bind group convention"):
//   group 0 Frame    (PerCamera) - FrameUniforms
//   group 1 Scene    (PerCamera) - LightsBuffer / ShadowUniform / EnvironmentUniforms / ClusterCell
//   group 2 Material (PerMaterial) - MaterialUniforms / PBRProperties
//   group 3 Object   (PerObject) - ObjectUniforms (single) or an `array<ObjectInstance>` for GPU-driven
// Custom shader bindings start at @group(20).
constexpr KnownStructMeta kKnownStructs[] = {
	{"FrameUniforms",       "Frame_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Frame},
	{"ObjectUniforms",      "Object_BindGroup",   BindGroupReuse::PerObject,   BindGroupRole::Object},
	{"MaterialUniforms",    "Material_BindGroup", BindGroupReuse::PerMaterial, BindGroupRole::Material},
	{"PBRProperties",       "Material_BindGroup", BindGroupReuse::PerMaterial, BindGroupRole::Material},
	// Everything scene-wide collapses into one bind group. Any of the listed
	// structs (or shadow textures with the matching names below) anchor the
	// group as Scene at @group(1).
	{"LightsBuffer",        "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
	{"LightBuffer",         "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
	{"ShadowUniform",       "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
	{"EnvironmentUniforms", "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
	{"ClusterCell",         "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
	{"ClusterCellAtomic",   "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
	{"ClusterLightList",    "Scene_BindGroup",    BindGroupReuse::PerCamera,   BindGroupRole::Scene},
};

const KnownStructMeta *findKnownStruct(const std::string &structName)
{
	for (const auto &k : kKnownStructs)
	{
		if (structName == k.structName)
			return &k;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
class Parser
{
public:
	Parser(const std::vector<Token> &tokens, ReflectResult &result)
		: m_tokens(tokens), m_result(result)
	{
	}

	void run();

private:
	const std::vector<Token> &m_tokens;
	ReflectResult            &m_result;
	size_t                    m_pos = 0;

	/// Pending annotations attach to the NEXT top-level declaration.
	std::vector<AnnotationArgs> m_pendingAnnotations;

	const Token &cur() const { return m_tokens[m_pos]; }
	const Token &peek(size_t ahead = 0) const
	{
		size_t idx = m_pos + ahead;
		return m_tokens[std::min(idx, m_tokens.size() - 1)];
	}
	bool match(TokenKind kind, const char *text = nullptr)
	{
		if (cur().kind != kind) return false;
		if (text && cur().text != text) return false;
		++m_pos;
		return true;
	}
	bool check(TokenKind kind, const char *text = nullptr) const
	{
		if (cur().kind != kind) return false;
		if (text && cur().text != text) return false;
		return true;
	}
	void advance() { if (m_pos + 1 < m_tokens.size()) ++m_pos; }

	void diag(const std::string &msg)
	{
		Diagnostic d;
		d.message = msg;
		d.line    = cur().line;
		d.column  = cur().column;
		m_result.diagnostics.push_back(std::move(d));
	}

	void parseTopLevel();
	void parseStruct();
	void parseVarDecl(std::vector<AnnotationArgs> attrs, const std::vector<Token> &wgslAttrs);
	void parseFn(std::vector<AnnotationArgs> annotations);
	void skipFunctionBody();
	void skipToSemicolonOrBrace();

	WgslType parseType();
	uint32_t consumeUint();

	// Helpers for resolving struct layouts after first pass.
	void finalizeStructLayouts();
	uint32_t computeStructLayout(StructLayout &s);
	uint32_t typeSize(const WgslType &t);
	uint32_t typeAlign(const WgslType &t);

	std::unordered_map<std::string, size_t> m_structIndex; // name -> index into m_result.reflection.structs

	struct WgslAttr
	{
		std::string name;                       // "group", "binding", "location", "workgroup_size", "builtin"
		std::vector<std::string> args;
	};
	std::vector<WgslAttr> m_pendingWgslAttrs;
};

uint32_t Parser::typeSize(const WgslType &t)
{
	auto info = lookupPrim(t.userTypeName.empty() ? "" : t.userTypeName);
	if (t.primitive != WgslPrimitive::Unknown)
	{
		// Recover primitive size via the spec table.
		for (const auto &p : kPrimitives)
			if (p.prim == t.primitive) return p.size;
		return 0;
	}
	if (!t.userTypeName.empty())
	{
		auto it = m_structIndex.find(t.userTypeName);
		if (it != m_structIndex.end())
			return m_result.reflection.structs[it->second].sizeBytes;
	}
	return 0;
}

uint32_t Parser::typeAlign(const WgslType &t)
{
	if (t.primitive != WgslPrimitive::Unknown)
	{
		for (const auto &p : kPrimitives)
			if (p.prim == t.primitive) return p.align;
		return 1;
	}
	if (!t.userTypeName.empty())
	{
		auto it = m_structIndex.find(t.userTypeName);
		if (it != m_structIndex.end())
			return m_result.reflection.structs[it->second].alignBytes;
	}
	return 1;
}

uint32_t Parser::computeStructLayout(StructLayout &s)
{
	uint32_t offset = 0;
	uint32_t maxAlign = 1;
	s.hasRuntimeArray = false;
	for (auto &f : s.fields)
	{
		uint32_t a = typeAlign(f.type);
		if (a > maxAlign) maxAlign = a;
		offset       = alignUp(offset, a);
		f.offsetBytes = offset;
		uint32_t fsize = typeSize(f.type);
		if (f.type.isArray)
		{
			if (f.type.arrayLength == UINT32_MAX)
			{
				s.hasRuntimeArray = true;
				fsize = 0;          // contributes no size for tail array
			}
			else
			{
				// Array stride = aligned element size.
				uint32_t elemSize  = fsize;
				uint32_t elemAlign = a;
				uint32_t stride    = alignUp(elemSize, elemAlign);
				fsize              = stride * f.type.arrayLength;
			}
		}
		f.sizeBytes = fsize;
		offset     += fsize;
	}
	s.sizeBytes  = alignUp(offset, maxAlign);
	s.alignBytes = maxAlign;
	return s.sizeBytes;
}

void Parser::finalizeStructLayouts()
{
	// One pass usually resolves everything since WGSL forbids forward
	// references. Two passes handles the rare case of nested resolution.
	for (int pass = 0; pass < 2; ++pass)
		for (auto &s : m_result.reflection.structs)
			computeStructLayout(s);

	// Hook layouts back into bindings whose type names match.
	for (auto &bg : m_result.reflection.bindGroups)
	{
		for (auto &b : bg.bindings)
		{
			if (b.structLayout.name.empty())
				continue;
			auto it = m_structIndex.find(b.structLayout.name);
			if (it == m_structIndex.end())
				continue;
			b.structLayout         = m_result.reflection.structs[it->second];
			b.minBindingSize       = b.structLayout.sizeBytes;
		}
	}
}

// ---------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------
void Parser::run()
{
	while (cur().kind != TokenKind::EndOfFile)
	{
		// Collect engine-side `//@...` annotations: they attach to the next decl.
		if (cur().kind == TokenKind::Annotation)
		{
			if (auto a = parseAnnotation(cur().text))
				m_pendingAnnotations.push_back(std::move(*a));
			advance();
			continue;
		}

		// Collect WGSL attributes (`@group(0)`, `@binding(1)`, `@vertex`, ...)
		// also attach to the next decl.
		if (cur().kind == TokenKind::Attribute)
		{
			WgslAttr a;
			a.name = cur().text;
			advance();
			if (check(TokenKind::Punct, "("))
			{
				advance();
				std::string buf;
				int paren = 1;
				while (cur().kind != TokenKind::EndOfFile && paren > 0)
				{
					if (check(TokenKind::Punct, "("))
					{
						++paren;
						buf += cur().text;
						advance();
					}
					else if (check(TokenKind::Punct, ")"))
					{
						--paren;
						if (paren > 0) buf += cur().text;
						advance();
					}
					else if (check(TokenKind::Punct, ","))
					{
						a.args.push_back(buf);
						buf.clear();
						advance();
					}
					else
					{
						if (!buf.empty() && buf.back() != '<' && cur().text != ">" && cur().text != "<")
							buf += " ";
						buf += cur().text;
						advance();
					}
				}
				if (!buf.empty()) a.args.push_back(buf);
			}
			m_pendingWgslAttrs.push_back(std::move(a));
			continue;
		}

		parseTopLevel();
	}

	finalizeStructLayouts();
}

void Parser::parseTopLevel()
{
	if (check(TokenKind::Keyword, "struct"))
	{
		parseStruct();
		m_pendingAnnotations.clear();
		m_pendingWgslAttrs.clear();
	}
	else if (check(TokenKind::Keyword, "var"))
	{
		auto ann   = std::move(m_pendingAnnotations);
		auto wattr = std::vector<Token>{};                  // unused now - attrs are in m_pendingWgslAttrs
		m_pendingAnnotations.clear();
		parseVarDecl(std::move(ann), wattr);
	}
	else if (check(TokenKind::Keyword, "fn"))
	{
		auto ann = std::move(m_pendingAnnotations);
		m_pendingAnnotations.clear();
		parseFn(std::move(ann));
	}
	else if (check(TokenKind::Keyword, "const") || check(TokenKind::Keyword, "let"))
	{
		// Top-level const/let we don't care about; skip to ';'.
		skipToSemicolonOrBrace();
		m_pendingAnnotations.clear();
		m_pendingWgslAttrs.clear();
	}
	else
	{
		// Unknown top-level token; skip a token at a time to avoid getting stuck.
		advance();
	}
}

void Parser::skipToSemicolonOrBrace()
{
	int braces = 0;
	while (cur().kind != TokenKind::EndOfFile)
	{
		if (check(TokenKind::Punct, "{")) ++braces;
		else if (check(TokenKind::Punct, "}"))
		{
			--braces;
			if (braces <= 0)
			{
				advance();
				return;
			}
		}
		else if (braces == 0 && check(TokenKind::Punct, ";"))
		{
			advance();
			return;
		}
		advance();
	}
}

void Parser::skipFunctionBody()
{
	// Expects to be sitting on '{'.
	if (!check(TokenKind::Punct, "{")) return;
	int depth = 0;
	while (cur().kind != TokenKind::EndOfFile)
	{
		if (check(TokenKind::Punct, "{")) ++depth;
		else if (check(TokenKind::Punct, "}"))
		{
			--depth;
			advance();
			if (depth == 0) return;
			continue;
		}
		advance();
	}
}

uint32_t Parser::consumeUint()
{
	if (cur().kind != TokenKind::IntegerLiteral)
		return 0;
	uint32_t v = 0;
	const std::string &s = cur().text;
	auto end = s.data() + s.size();
	// strip trailing u/i suffix if any
	auto realEnd = end;
	if (realEnd > s.data() && (*(realEnd - 1) == 'u' || *(realEnd - 1) == 'i'))
		--realEnd;
	std::from_chars(s.data(), realEnd, v);
	advance();
	return v;
}

WgslType Parser::parseType()
{
	WgslType t;
	if (cur().kind != TokenKind::Identifier && cur().kind != TokenKind::Keyword)
	{
		diag("expected type identifier");
		return t;
	}

	std::string name = cur().text;

	// Handle generic syntax: name<innerType,...>
	advance();
	if (check(TokenKind::Punct, "<"))
	{
		std::string inner = name + "<";
		advance();
		int depth = 1;
		while (cur().kind != TokenKind::EndOfFile && depth > 0)
		{
			if (check(TokenKind::Punct, "<")) { ++depth; inner += cur().text; advance(); }
			else if (check(TokenKind::Punct, ">"))
			{
				--depth;
				if (depth == 0) { inner += ">"; advance(); break; }
				else            { inner += cur().text; advance(); }
			}
			else { inner += cur().text; advance(); }
		}
		name = inner;
	}

	// Array? `array<T, N>` or `array<T>` — we already absorbed it above into `name`.
	if (name.rfind("array<", 0) == 0)
	{
		t.isArray = true;
		// Extract inner type and optional count.
		// Example: "array<LightStruct,5>" or "array<u32>"
		auto innerStart = name.find('<');
		auto innerEnd   = name.rfind('>');
		std::string inner = name.substr(innerStart + 1, innerEnd - innerStart - 1);
		auto comma = inner.find(',');
		std::string elemTypeName;
		if (comma == std::string::npos)
		{
			elemTypeName = inner;
			t.arrayLength = UINT32_MAX;     // runtime-sized
		}
		else
		{
			elemTypeName = inner.substr(0, comma);
			std::string count = inner.substr(comma + 1);
			// trim
			while (!count.empty() && std::isspace((unsigned char)count.front())) count.erase(count.begin());
			while (!count.empty() && std::isspace((unsigned char)count.back()))  count.pop_back();
			// strip u suffix
			if (!count.empty() && (count.back() == 'u' || count.back() == 'i'))
				count.pop_back();
			uint32_t n = 0;
			std::from_chars(count.data(), count.data() + count.size(), n);
			t.arrayLength = n;
		}
		// trim element type
		while (!elemTypeName.empty() && std::isspace((unsigned char)elemTypeName.front())) elemTypeName.erase(elemTypeName.begin());
		while (!elemTypeName.empty() && std::isspace((unsigned char)elemTypeName.back()))  elemTypeName.pop_back();
		auto pi = lookupPrim(elemTypeName);
		if (pi.prim != WgslPrimitive::Unknown)
			t.primitive = pi.prim;
		else
			t.userTypeName = elemTypeName;
		return t;
	}
	if (name.rfind("atomic<", 0) == 0)
	{
		t.isAtomic = true;
		auto innerStart = name.find('<');
		auto innerEnd   = name.rfind('>');
		std::string inner = name.substr(innerStart + 1, innerEnd - innerStart - 1);
		auto pi = lookupPrim(inner);
		if (pi.prim != WgslPrimitive::Unknown) t.primitive = pi.prim;
		else t.userTypeName = inner;
		return t;
	}

	auto pi = lookupPrim(name);
	if (pi.prim != WgslPrimitive::Unknown)
		t.primitive = pi.prim;
	else
		t.userTypeName = name;
	return t;
}

void Parser::parseStruct()
{
	advance(); // consume 'struct'
	if (cur().kind != TokenKind::Identifier)
	{
		diag("expected struct name");
		skipToSemicolonOrBrace();
		return;
	}
	StructLayout s;
	s.name = cur().text;
	advance();

	if (!check(TokenKind::Punct, "{"))
	{
		diag("expected '{' in struct body");
		skipToSemicolonOrBrace();
		return;
	}
	advance(); // consume '{'

	while (cur().kind != TokenKind::EndOfFile && !check(TokenKind::Punct, "}"))
	{
		// Capture per-field WGSL attributes (location for vertex/fragment IO,
		// builtin which we just record and ignore, and align/size which we don't
		// model yet).
		std::optional<uint32_t> location;
		while (check(TokenKind::Attribute))
		{
			std::string attrName = cur().text;
			advance();
			std::vector<std::string> args;
			std::string buf;
			if (check(TokenKind::Punct, "("))
			{
				advance();
				int depth = 1;
				while (cur().kind != TokenKind::EndOfFile && depth > 0)
				{
					if (check(TokenKind::Punct, "("))
					{
						++depth;
						buf += cur().text;
					}
					else if (check(TokenKind::Punct, ")"))
					{
						--depth;
						if (depth > 0) buf += cur().text;
					}
					else if (check(TokenKind::Punct, ",") && depth == 1)
					{
						args.push_back(buf);
						buf.clear();
					}
					else
					{
						buf += cur().text;
					}
					advance();
				}
				if (!buf.empty()) args.push_back(buf);
			}
			if (attrName == "location" && !args.empty())
			{
				uint32_t loc = 0;
				std::from_chars(args[0].data(), args[0].data() + args[0].size(), loc);
				location = loc;
			}
		}

		if (cur().kind != TokenKind::Identifier)
		{
			advance();
			continue;
		}
		StructField f;
		f.name = cur().text;
		f.location = location;
		advance();
		if (!check(TokenKind::Punct, ":"))
		{
			diag("expected ':' in struct field");
			break;
		}
		advance();
		f.type = parseType();
		s.fields.push_back(std::move(f));

		if (check(TokenKind::Punct, ",")) advance();
		else if (check(TokenKind::Punct, ";")) advance();
	}
	if (check(TokenKind::Punct, "}"))
		advance();

	m_structIndex[s.name] = m_result.reflection.structs.size();
	m_result.reflection.structs.push_back(std::move(s));
}

void Parser::parseVarDecl(std::vector<AnnotationArgs> annotations, const std::vector<Token> & /*unused*/)
{
	// Extract @group / @binding from WGSL attributes already collected.
	std::optional<uint32_t> group;
	std::optional<uint32_t> binding;
	std::string             addressSpace;          // "uniform", "storage", ...
	std::string             accessMode;            // "read", "read_write"

	for (const auto &wa : m_pendingWgslAttrs)
	{
		if (wa.name == "group" && !wa.args.empty())
		{
			uint32_t v = 0;
			std::from_chars(wa.args[0].data(), wa.args[0].data() + wa.args[0].size(), v);
			group = v;
		}
		else if (wa.name == "binding" && !wa.args.empty())
		{
			uint32_t v = 0;
			std::from_chars(wa.args[0].data(), wa.args[0].data() + wa.args[0].size(), v);
			binding = v;
		}
	}
	m_pendingWgslAttrs.clear();

	advance(); // consume 'var'

	// Address space syntax: var<uniform> or var<storage, read>
	if (check(TokenKind::Punct, "<"))
	{
		advance();
		if (cur().kind == TokenKind::Keyword || cur().kind == TokenKind::Identifier)
		{
			addressSpace = cur().text;
			advance();
		}
		if (check(TokenKind::Punct, ","))
		{
			advance();
			if (cur().kind == TokenKind::Keyword || cur().kind == TokenKind::Identifier)
			{
				accessMode = cur().text;
				advance();
			}
		}
		if (check(TokenKind::Punct, ">")) advance();
	}

	if (cur().kind != TokenKind::Identifier)
	{
		skipToSemicolonOrBrace();
		return;
	}
	std::string varName = cur().text;
	advance();
	if (!check(TokenKind::Punct, ":"))
	{
		skipToSemicolonOrBrace();
		return;
	}
	advance();
	WgslType type = parseType();

	// Skip optional initializer up to ';'.
	skipToSemicolonOrBrace();

	if (!group || !binding)
		return;

	// Map address space + type to BindingKind.
	Binding b;
	b.bindingIndex = *binding;
	b.wgslName     = varName;
	b.kind         = BindingKind::Unknown;
	if (addressSpace == "uniform")
	{
		b.kind = BindingKind::UniformBuffer;
	}
	else if (addressSpace == "storage")
	{
		b.kind = (accessMode == "read_write" || accessMode == "write")
				 ? BindingKind::StorageBufferRW
				 : BindingKind::StorageBufferRO;
	}
	else if (!type.userTypeName.empty() || type.primitive == WgslPrimitive::Unknown)
	{
		// Free var<...> with no address space - inspect type name.
		const std::string &n = type.userTypeName;
		if (n == "sampler")               b.kind = BindingKind::Sampler;
		else if (n == "sampler_comparison") b.kind = BindingKind::SamplerComparison;
		else if (n.rfind("texture_", 0) == 0)
		{
			b.kind = BindingKind::Texture;
			// Pull view dim / sample type from the name.
			TextureBinding tex;
			if      (n.rfind("texture_2d_array", 0) == 0)   tex.viewDim = TextureViewDim::D2Array;
			else if (n.rfind("texture_2d", 0) == 0)         tex.viewDim = TextureViewDim::D2;
			else if (n.rfind("texture_3d", 0) == 0)         tex.viewDim = TextureViewDim::D3;
			else if (n.rfind("texture_cube_array", 0) == 0) tex.viewDim = TextureViewDim::CubeArray;
			else if (n.rfind("texture_cube", 0) == 0)       tex.viewDim = TextureViewDim::Cube;
			else if (n.rfind("texture_depth_2d_array", 0) == 0) { tex.viewDim = TextureViewDim::D2Array; tex.sampleType = "depth"; }
			else if (n.rfind("texture_depth_2d", 0) == 0)       { tex.viewDim = TextureViewDim::D2; tex.sampleType = "depth"; }
			else if (n.rfind("texture_depth_cube_array", 0) == 0){ tex.viewDim = TextureViewDim::CubeArray; tex.sampleType = "depth"; }
			else if (n.rfind("texture_depth_cube", 0) == 0)      { tex.viewDim = TextureViewDim::Cube; tex.sampleType = "depth"; }
			else                                              tex.viewDim = TextureViewDim::D2;

			// Inner sample type if not depth: look for `<f32>`, `<i32>`, `<u32>`.
			auto lt = n.find('<');
			auto gt = n.rfind('>');
			if (tex.sampleType.empty() && lt != std::string::npos && gt != std::string::npos)
				tex.sampleType = n.substr(lt + 1, gt - lt - 1);
			else if (tex.sampleType.empty())
				tex.sampleType = "f32";
			b.texture = tex;
		}
	}

	// Storage / uniform buffer: capture the struct name so finalize() can patch
	// the layout in after all structs are seen.
	if ((b.kind == BindingKind::UniformBuffer || b.kind == BindingKind::StorageBufferRO || b.kind == BindingKind::StorageBufferRW)
		&& !type.userTypeName.empty())
	{
		b.structLayout.name = type.userTypeName;
	}

	// Locate or create the BindGroupLayout for this group index.
	auto &bgs = m_result.reflection.bindGroups;
	BindGroupLayout *bg = nullptr;
	for (auto &x : bgs)
		if (x.groupIndex == *group) { bg = &x; break; }
	if (!bg)
	{
		BindGroupLayout newBg;
		newBg.groupIndex = *group;
		newBg.name       = "Group" + std::to_string(*group);
		bgs.push_back(newBg);
		bg = &bgs.back();
	}

	// Apply pending bind_group annotation (covers all bindings in this group).
	for (const auto &ann : annotations)
	{
		if (ann.name != "bind_group") continue;
		auto it = ann.named.find("name");
		if (it != ann.named.end()) bg->name = it->second;
		it = ann.named.find("reuse");
		if (it != ann.named.end()) bg->reuse = parseReuse(it->second);
		it = ann.named.find("role");
		if (it != ann.named.end()) bg->role = parseRole(it->second);
	}

	// Known-struct fallback if the group still has the default name.
	if (bg->name.rfind("Group", 0) == 0 && !b.structLayout.name.empty())
	{
		if (auto *ks = findKnownStruct(b.structLayout.name))
		{
			bg->name  = ks->groupName;
			bg->reuse = ks->reuse;
			bg->role  = ks->role;
		}
	}

	bg->bindings.push_back(std::move(b));
}

void Parser::parseFn(std::vector<AnnotationArgs> annotations)
{
	// Determine stage from the collected WGSL attributes.
	ShaderStage             stage    = ShaderStage::Vertex;
	bool                    isEntry  = false;
	std::array<uint32_t, 3> workgrp  = {1, 1, 1};

	for (const auto &wa : m_pendingWgslAttrs)
	{
		if (wa.name == "vertex")    { stage = ShaderStage::Vertex;   isEntry = true; }
		else if (wa.name == "fragment") { stage = ShaderStage::Fragment; isEntry = true; }
		else if (wa.name == "compute")  { stage = ShaderStage::Compute;  isEntry = true; }
		else if (wa.name == "workgroup_size")
		{
			for (size_t i = 0; i < wa.args.size() && i < 3; ++i)
			{
				uint32_t v = 0;
				std::from_chars(wa.args[i].data(), wa.args[i].data() + wa.args[i].size(), v);
				workgrp[i] = v;
			}
		}
	}
	m_pendingWgslAttrs.clear();

	advance(); // consume 'fn'
	if (cur().kind != TokenKind::Identifier)
	{
		skipToSemicolonOrBrace();
		return;
	}
	std::string fnName = cur().text;
	advance();

	// Skip parameter list - vertex input @location attributes inside are still
	// captured if we want, but a tiny subset is enough for now.
	if (check(TokenKind::Punct, "("))
	{
		advance();
		int depth = 1;
		while (cur().kind != TokenKind::EndOfFile && depth > 0)
		{
			if (check(TokenKind::Punct, "(")) ++depth;
			else if (check(TokenKind::Punct, ")")) --depth;
			advance();
		}
	}

	// Parse the optional return type. Two shapes for fragment entries:
	//   fn fs_main(...) -> @location(0) vec4f { ... }      single-output
	//   fn fs_main(...) -> FragmentOutput { ... }           struct-output
	std::optional<uint32_t> directReturnLocation;
	WgslType                directReturnType;
	std::string             structReturnTypeName;
	if (check(TokenKind::Punct, "-"))
	{
		advance();
		if (check(TokenKind::Punct, ">")) advance();

		// Optional @location(N) on direct return.
		while (check(TokenKind::Attribute))
		{
			std::string attrName = cur().text;
			advance();
			std::vector<std::string> args;
			std::string buf;
			if (check(TokenKind::Punct, "("))
			{
				advance();
				int depth = 1;
				while (cur().kind != TokenKind::EndOfFile && depth > 0)
				{
					if (check(TokenKind::Punct, "("))
					{
						++depth;
						buf += cur().text;
					}
					else if (check(TokenKind::Punct, ")"))
					{
						--depth;
						if (depth > 0) buf += cur().text;
					}
					else if (check(TokenKind::Punct, ",") && depth == 1)
					{
						args.push_back(buf);
						buf.clear();
					}
					else
					{
						buf += cur().text;
					}
					advance();
				}
				if (!buf.empty()) args.push_back(buf);
			}
			if (attrName == "location" && !args.empty())
			{
				uint32_t loc = 0;
				std::from_chars(args[0].data(), args[0].data() + args[0].size(), loc);
				directReturnLocation = loc;
			}
		}

		// Return type token. Could be a primitive (vec4f, vec4<f32>, …) or a
		// struct name. parseType handles both - if it returns a userTypeName we
		// look that up later in the struct table.
		if (cur().kind == TokenKind::Identifier || cur().kind == TokenKind::Keyword)
		{
			directReturnType = parseType();
			if (directReturnType.primitive == WgslPrimitive::Unknown
				&& !directReturnType.userTypeName.empty())
			{
				structReturnTypeName = directReturnType.userTypeName;
			}
		}
	}

	// Skip anything between the return type and the function body.
	while (cur().kind != TokenKind::EndOfFile && !check(TokenKind::Punct, "{"))
		advance();
	skipFunctionBody();

	if (!isEntry)
		return;

	EntryPoint ep;
	ep.stage = stage;
	ep.name  = std::move(fnName);
	ep.workgroupSize = workgrp;
	m_result.reflection.entryPoints.push_back(std::move(ep));

	// Fragment outputs: derive ComponentKind + count from the return type.
	// Bit depth / sRGB / normalization aren't WGSL-expressible and stay engine-side.
	auto deriveFragmentOutput = [](const WgslType &t, std::optional<uint32_t> loc) -> std::optional<FragmentOutputDesc>
	{
		if (t.primitive == WgslPrimitive::Unknown) return std::nullopt;
		FragmentOutputDesc d;
		d.location = loc.value_or(0);
		switch (t.primitive)
		{
			case WgslPrimitive::F32:   d.componentKind = ComponentKind::Float; d.componentCount = 1; break;
			case WgslPrimitive::Vec2F: d.componentKind = ComponentKind::Float; d.componentCount = 2; break;
			case WgslPrimitive::Vec3F: d.componentKind = ComponentKind::Float; d.componentCount = 3; break;
			case WgslPrimitive::Vec4F: d.componentKind = ComponentKind::Float; d.componentCount = 4; break;
			case WgslPrimitive::I32:   d.componentKind = ComponentKind::Sint;  d.componentCount = 1; break;
			case WgslPrimitive::Vec2I: d.componentKind = ComponentKind::Sint;  d.componentCount = 2; break;
			case WgslPrimitive::Vec3I: d.componentKind = ComponentKind::Sint;  d.componentCount = 3; break;
			case WgslPrimitive::Vec4I: d.componentKind = ComponentKind::Sint;  d.componentCount = 4; break;
			case WgslPrimitive::U32:   d.componentKind = ComponentKind::Uint;  d.componentCount = 1; break;
			case WgslPrimitive::Vec2U: d.componentKind = ComponentKind::Uint;  d.componentCount = 2; break;
			case WgslPrimitive::Vec3U: d.componentKind = ComponentKind::Uint;  d.componentCount = 3; break;
			case WgslPrimitive::Vec4U: d.componentKind = ComponentKind::Uint;  d.componentCount = 4; break;
			default: return std::nullopt;
		}
		return d;
	};

	if (stage == ShaderStage::Fragment)
	{
		if (!structReturnTypeName.empty())
		{
			// Struct return - one FragmentOutputDesc per @location field.
			auto it = m_structIndex.find(structReturnTypeName);
			if (it != m_structIndex.end())
			{
				const auto &s = m_result.reflection.structs[it->second];
				for (const auto &f : s.fields)
				{
					if (!f.location) continue;
					if (auto d = deriveFragmentOutput(f.type, f.location))
						m_result.reflection.fragmentOutputs.push_back(*d);
				}
			}
		}
		else if (directReturnType.primitive != WgslPrimitive::Unknown)
		{
			if (auto d = deriveFragmentOutput(directReturnType, directReturnLocation))
				m_result.reflection.fragmentOutputs.push_back(*d);
		}
	}

	// Pipeline hints carried in WGSL comments (depth/cull). color_target is
	// intentionally dropped - target formats live with whoever creates the
	// target texture, not the shader author.
	for (const auto &ann : annotations)
	{
		if (ann.name == "depth")
		{
			auto it = ann.named.find("compare");
			if (it != ann.named.end()) m_result.reflection.hints.depthCompare = it->second;
			it = ann.named.find("write");
			if (it != ann.named.end())
				m_result.reflection.hints.depthWrite = (it->second == "true");
		}
		else if (ann.name == "cull")
		{
			auto it = ann.named.find("mode");
			if (it != ann.named.end()) m_result.reflection.hints.cullMode = it->second;
		}
	}
}

} // namespace

ReflectResult reflectWgsl(std::string_view source, std::string path)
{
	WgslLexer lexer(source);
	auto tokens = lexer.tokenize();

	ReflectResult result;
	result.reflection.path = std::move(path);

	Parser parser(tokens, result);
	parser.run();

	// Sort bind groups by group index for stable iteration.
	std::sort(
		result.reflection.bindGroups.begin(),
		result.reflection.bindGroups.end(),
		[](const BindGroupLayout &a, const BindGroupLayout &b) { return a.groupIndex < b.groupIndex; }
	);
	for (auto &bg : result.reflection.bindGroups)
	{
		std::sort(
			bg.bindings.begin(),
			bg.bindings.end(),
			[](const Binding &a, const Binding &b) { return a.bindingIndex < b.bindingIndex; }
		);
	}
	std::sort(
		result.reflection.fragmentOutputs.begin(),
		result.reflection.fragmentOutputs.end(),
		[](const FragmentOutputDesc &a, const FragmentOutputDesc &b) { return a.location < b.location; }
	);

	// Canonical-layout validation applies only to render pipelines. Compute
	// pipelines own their own bind-group layouts (no Frame/Scene/Material/Object
	// roles), so a compute shader putting LightsBuffer at @group(0) is not a
	// convention violation. Detect by looking for a Compute entry point.
	bool isComputeOnly = !result.reflection.entryPoints.empty();
	for (const auto &ep : result.reflection.entryPoints)
	{
		if (ep.stage != ShaderStage::Compute) { isComputeOnly = false; break; }
	}
	if (isComputeOnly) return result;

	// Canonical-layout validation. SKILL.md fixes the four engine roles at known
	// indices and reserves the [0..ENGINE_GROUPS_END) range; custom groups must
	// start at ENGINE_GROUPS_END. Misplacements are surfaced as diagnostics so
	// the offending shader can be migrated without papering over the drift.
	auto expectedIndex = [](BindGroupRole r) -> std::optional<uint32_t> {
		switch (r) {
			case BindGroupRole::Frame:    return CANONICAL_GROUP_FRAME;
			case BindGroupRole::Scene:    return CANONICAL_GROUP_SCENE;
			case BindGroupRole::Material: return CANONICAL_GROUP_MATERIAL;
			case BindGroupRole::Object:   return CANONICAL_GROUP_OBJECT;
			default:                      return std::nullopt;
		}
	};
	for (const auto &bg : result.reflection.bindGroups)
	{
		if (auto want = expectedIndex(bg.role); want && *want != bg.groupIndex)
		{
			// Only flag misplacement when the group lives inside the engine
			// range [0..ENGINE_GROUPS_END). A bind group at a custom index
			// (>= ENGINE_GROUPS_END) whose role was inferred from a struct
			// name (e.g. skybox declares its own `EnvironmentUniforms` at
			// @group(4)) is a known-struct false positive — the shader
			// intentionally repurposes a familiar struct for a custom binding.
			if (bg.groupIndex < ENGINE_GROUPS_END)
			{
				Diagnostic d;
				d.message = "bind group '" + bg.name + "' should be at @group("
					+ std::to_string(*want) + ") per the engine convention but is at @group("
					+ std::to_string(bg.groupIndex) + ")";
				result.diagnostics.push_back(std::move(d));
			}
		}
		if (bg.role == BindGroupRole::Custom && bg.groupIndex < ENGINE_GROUPS_END)
		{
			Diagnostic d;
			d.message = "custom bind group '" + bg.name + "' at @group("
				+ std::to_string(bg.groupIndex) + ") collides with the engine-reserved range [0.."
				+ std::to_string(ENGINE_GROUPS_END - 1) + "]; start custom groups at @group("
				+ std::to_string(ENGINE_GROUPS_END) + ")";
			result.diagnostics.push_back(std::move(d));
		}
	}

	return result;
}

} // namespace engine::rendering::reflection
