#include "engine/rendering/shaders/StructDescriptor.h"

#include <cassert>
#include <cstdio>

namespace engine::rendering::shaders::detail
{

void assertDescriptorMatches(std::size_t cppSize, const StructDescriptor &d)
{
	if (d.sizeBytes != static_cast<uint32_t>(cppSize))
	{
		std::fprintf(stderr,
		             "StructDescriptor[%s]: sizeBytes=%u != sizeof(T)=%zu\n",
		             d.name.c_str(), d.sizeBytes, cppSize);
		assert(false && "Descriptor size mismatch");
	}
	if (d.sizeBytes % 16 != 0)
	{
		// std140 UBO requirement. Most cases fail by adding a vec3 with no
		// trailing scalar - either fold a f32 into it or pad the C++ struct
		// to 16-byte boundaries explicitly.
		std::fprintf(stderr,
		             "StructDescriptor[%s]: sizeBytes=%u not a multiple of 16 (UBO requires 16-byte multiple)\n",
		             d.name.c_str(), d.sizeBytes);
		assert(false && "Descriptor size not 16-aligned");
	}

	uint32_t cursor = 0;
	for (size_t i = 0; i < d.fields.size(); ++i)
	{
		const auto &f      = d.fields[i];
		const uint32_t a   = wgslTypeAlign(f.type);
		const uint32_t s   = wgslTypeSize(f.type);

		if (f.offsetBytes < cursor)
		{
			std::fprintf(stderr,
			             "StructDescriptor[%s]: field '%s' offset=%u overlaps previous (cursor=%u)\n",
			             d.name.c_str(), f.name.c_str(), f.offsetBytes, cursor);
			assert(false && "Descriptor field overlap");
		}
		if (f.offsetBytes % a != 0)
		{
			std::fprintf(stderr,
			             "StructDescriptor[%s]: field '%s' offset=%u not aligned to %u\n",
			             d.name.c_str(), f.name.c_str(), f.offsetBytes, a);
			assert(false && "Descriptor field misalignment");
		}
		if (f.offsetBytes + s > d.sizeBytes)
		{
			std::fprintf(stderr,
			             "StructDescriptor[%s]: field '%s' (offset=%u size=%u) overruns struct size %u\n",
			             d.name.c_str(), f.name.c_str(), f.offsetBytes, s, d.sizeBytes);
			assert(false && "Descriptor field overruns struct");
		}
		cursor = f.offsetBytes + s;
	}
}

} // namespace engine::rendering::shaders::detail
