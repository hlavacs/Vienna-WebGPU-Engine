#include "engine/rendering/shaders/ShaderCodegen.h"

#include <cassert>
#include <fstream>
#include <sstream>

namespace engine::rendering::shaders
{

namespace
{

void appendPadding(uint32_t paddingBytes, uint32_t fieldIndex, std::string &out)
{
	// std140 padding: every gap is a multiple of 4 because all our types
	// align to 4 or higher. Emit one f32 per 4-byte slot so the byte layout
	// is unambiguous from the WGSL alone.
	const uint32_t slots = paddingBytes / 4;
	for (uint32_t i = 0; i < slots; ++i)
	{
		out.append("\t_pad_");
		out.append(std::to_string(fieldIndex));
		out.push_back('_');
		out.append(std::to_string(i));
		out.append(": f32,\n");
	}
}

std::string_view bindingStorageQualifier(GenBindingKind k)
{
	switch (k)
	{
		case GenBindingKind::UniformBuffer:   return "var<uniform>";
		case GenBindingKind::StorageBufferRO: return "var<storage, read>";
		case GenBindingKind::StorageBufferRW: return "var<storage, read_write>";
	}
	return "var<uniform>";
}

} // namespace

void ShaderCodegen::emitStruct(const StructDescriptor &d, std::string &out)
{
	out.append("struct ");
	out.append(d.name);
	out.append(" {\n");

	uint32_t cursor = 0;
	for (size_t i = 0; i < d.fields.size(); ++i)
	{
		const auto &f = d.fields[i];
		if (f.offsetBytes > cursor)
		{
			appendPadding(f.offsetBytes - cursor, static_cast<uint32_t>(i), out);
		}
		out.push_back('\t');
		out.append(f.name);
		out.append(": ");
		out.append(wgslTypeName(f.type));
		out.append(",\n");
		cursor = f.offsetBytes + wgslTypeSize(f.type);
	}
	if (d.sizeBytes > cursor)
	{
		appendPadding(d.sizeBytes - cursor, static_cast<uint32_t>(d.fields.size()), out);
	}
	out.append("}\n");
}

void ShaderCodegen::emitStructWithRuntimeArray(const StructDescriptor &header,
                                                const std::string      &arrayFieldName,
                                                const std::string      &elementWgslName,
                                                std::string            &out)
{
	out.append("struct ");
	out.append(header.name);
	out.append(" {\n");

	uint32_t cursor = 0;
	for (size_t i = 0; i < header.fields.size(); ++i)
	{
		const auto &f = header.fields[i];
		if (f.offsetBytes > cursor)
		{
			appendPadding(f.offsetBytes - cursor, static_cast<uint32_t>(i), out);
		}
		out.push_back('\t');
		out.append(f.name);
		out.append(": ");
		out.append(wgslTypeName(f.type));
		out.append(",\n");
		cursor = f.offsetBytes + wgslTypeSize(f.type);
	}
	if (header.sizeBytes > cursor)
	{
		appendPadding(header.sizeBytes - cursor, static_cast<uint32_t>(header.fields.size()), out);
	}

	// Runtime array tail.
	out.push_back('\t');
	out.append(arrayFieldName);
	out.append(": array<");
	out.append(elementWgslName);
	out.append(">,\n");

	out.append("}\n");
}

void ShaderCodegen::emitBindingDecl(uint32_t groupIndex, const GenBindingSpec &b, std::string &out)
{
	assert(b.structRef && "Buffer bindings require a struct descriptor");
	out.append("@group(");
	out.append(std::to_string(groupIndex));
	out.append(") @binding(");
	out.append(std::to_string(b.bindingIndex));
	out.append(")\n");
	out.append(bindingStorageQualifier(b.kind));
	out.push_back(' ');
	out.append(b.wgslVarName);
	out.append(": ");
	out.append(b.structRef->name);
	out.append(";\n");
}

void ShaderCodegen::emitEngineGroup(const GenEngineGroupSpec &g, std::string &out)
{
	const uint32_t groupIndex = canonicalGroupIndex(g.group);

	// Emit each unique struct referenced by the bindings, in binding order.
	// A group may reference the same struct twice (rare) - dedupe by name.
	std::vector<const StructDescriptor *> emitted;
	for (const auto &b : g.bindings)
	{
		if (!b.structRef) continue;
		bool already = false;
		for (const auto *s : emitted)
		{
			if (s->name == b.structRef->name) { already = true; break; }
		}
		if (!already)
		{
			emitStruct(*b.structRef, out);
			out.push_back('\n');
			emitted.push_back(b.structRef);
		}
	}

	for (const auto &b : g.bindings)
	{
		emitBindingDecl(groupIndex, b, out);
	}
}

std::string ShaderCodegen::emitGeneratedFile(std::string_view sourceLabel, std::string body)
{
	std::string out;
	out.reserve(body.size() + 128);
	out.append("// AUTO-GENERATED from ");
	out.append(sourceLabel);
	out.append(". Do not edit.\n");
	out.append("// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.\n");
	out.push_back('\n');
	out.append(body);
	if (out.empty() || out.back() != '\n') out.push_back('\n');
	return out;
}

std::string ShaderCodegen::emitGeneratedEngineGroupFile(std::string_view sourceLabel, const GenEngineGroupSpec &g)
{
	std::string body;
	emitEngineGroup(g, body);
	return emitGeneratedFile(sourceLabel, std::move(body));
}

bool ShaderCodegen::writeIfChanged(const std::filesystem::path &path, const std::string &contents)
{
	// Skip the write when bytes are already identical. File-watcher hot reload
	// keys off mtime, so an idempotent regenerate would otherwise reload every
	// shader on engine start for no reason.
	std::ifstream in(path, std::ios::binary);
	if (in)
	{
		std::ostringstream ss;
		ss << in.rdbuf();
		if (ss.str() == contents) return false;
	}
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	return true;
}

} // namespace engine::rendering::shaders
