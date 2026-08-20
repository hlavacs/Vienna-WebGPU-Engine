#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/rendering/shaders/EngineStructDescriptors.h"
#include "engine/rendering/shaders/EnginePreprocessor.h"
#include "engine/rendering/shaders/GpuStructTraits.h"
#include "engine/rendering/shaders/ShaderCodegen.h"
#include "engine/rendering/shaders/StructDescriptor.h"

namespace engine::rendering::shaders::core
{

/**
 * @brief Authoritative record of one engine-side binding the codegen owns.
 *
 * One entry per @group(N) @binding(M) slot the engine emits. Populated by
 * the `REGISTER_ENGINE_*` macros at static-init time; consumed by
 * `regenerateAll()` (codegen + path resolution) and by `ShaderValidator`
 * (drift detection).
 */
struct EngineBinding
{
	const StructDescriptor *descriptor;     ///< Non-owning. Static lifetime.
	EngineBindGroup         group;
	uint32_t                bindingIndex;
	std::string             wgslVarName;    ///< e.g. "u_frame".
	std::string             generatedFile;  ///< Relative to resources/shaders/core/, e.g. "frame_uniforms.wgsl".
	GenBindingKind          kind;
};

/**
 * @brief Struct-only emit: the codegen writes just the WGSL struct definition
 *        into `core/<file>` with no `@group/@binding` declaration.
 *
 * Used for structs that anchor a bind group at a non-canonical @binding(N)
 * slot (Scene's ShadowUniform @4, EnvironmentUniforms @5, LightStruct as
 * array element @0), where the bind-group declaration is still hand-written
 * in the consuming shader but the struct *layout* still flows from the C++
 * descriptor.
 */
struct EngineStructOnly
{
	const StructDescriptor *descriptor;
	std::string             generatedFile;
};

/**
 * @brief Storage-buffer wrapper: header struct followed by a runtime-sized
 *        array of an element struct.
 *
 * Lets the codegen emit WGSL like
 *
 *   struct LightsBuffer {
 *       count: u32,
 *       _pad1: f32,
 *       _pad2: f32,
 *       _pad3: f32,
 *       lights: array<LightStruct>,
 *   }
 *
 * from C++ descriptors instead of hand-written WGSL. The header is a regular
 * aggregate `StructDescriptor`; the runtime array is appended after the last
 * header field. The consuming shader binds the wrapper as
 * `var<storage, read> u_lights: LightsBuffer;`, gets `u_lights.count` for
 * the size and `u_lights.lights[i]` for each entry.
 */
struct EngineBufferWrapper
{
	const StructDescriptor *header;           ///< Header struct (anchors size + alignment).
	const StructDescriptor *element;          ///< Runtime-array element struct.
	std::string             arrayFieldName;   ///< WGSL field name for the trailing array (e.g. "lights").
	std::string             generatedFile;    ///< File under `resources/shaders/core/`.
	/// Canonical placement in the engine bind-group convention. Populated by
	/// the registration macro; consumed by the validator so any shader that
	/// declares this wrapper at a different @group/@binding fails loud.
	EngineBindGroup         group           = EngineBindGroup::Scene;
	uint32_t                bindingIndex    = 0;
	std::string             wgslVarName;     ///< e.g. "u_lights".
};

/**
 * @brief Central registry of engine-owned GPU bindings.
 *
 * - **Registration** is by macro, static-init time. No manual function calls.
 * - **`regenerateAll()`** groups entries by emitted file, runs the codegen for
 *   each, writes to `resources/shaders/core/<file>.wgsl` via
 *   `ShaderCodegen::writeIfChanged` (idempotent — no mtime touch on no-op).
 * - **`validatorView()`** flattens entries into the shape `ShaderValidator`
 *   already understands, so the validator continues to ask "is `FrameUniforms`
 *   supposed to be at @group(0)?" without knowing the registry exists.
 *
 * Storage is a private static (construct-on-first-use) so registration is
 * safe across translation-unit init order.
 */
class EngineCoreRegistry
{
  public:
	static void                              registerBinding(EngineBinding spec);
	static const std::vector<EngineBinding> &entries();

	static void                                 registerStructOnly(EngineStructOnly spec);
	static const std::vector<EngineStructOnly> &structOnlyEntries();

	static void                                   registerBufferWrapper(EngineBufferWrapper spec);
	static const std::vector<EngineBufferWrapper> &bufferWrapperEntries();

	/// Emit every registered binding into `resources/shaders/core/`. Returns
	/// the count of files that actually changed (zero on idempotent re-run).
	static uint32_t regenerateAll();

	/// Validator-facing view. Stable interface; rebuilt on every
	/// `regenerateAll()` call.
	static const std::vector<GeneratedBindingRecord> &validatorView();
};

/// Auto-derive the WGSL variable name from a C++ struct name.
/// Rule: strip a trailing `Uniforms` / `Buffer` suffix (if present),
/// snake_case the rest, prepend `u_`. `FrameUniforms` → `u_frame`,
/// `LightsBuffer` → `u_lights`. Free function, exposed for tests.
[[nodiscard]] std::string deriveVarName(const char *structName);

/// Auto-derive the generated file name from a C++ struct name.
/// Rule: snake_case the whole struct name and append `.wgsl`.
/// `FrameUniforms` → `frame_uniforms.wgsl`. Free function, exposed for tests.
[[nodiscard]] std::string deriveFileName(const char *structName);

} // namespace engine::rendering::shaders::core

// ---- Registration macros ---------------------------------------------------

/**
 * @brief Register a uniform-buffer binding for engine struct @p T at the
 *        canonical group @p GroupId, with the WGSL var name and generated
 *        file name explicitly supplied.
 *
 * @param T          Fully-qualified C++ struct type (must already have a
 *                   `GpuStructTraits<T>` spec, typically via `ENGINE_GPU_STRUCT`).
 * @param GroupId    One of `Frame`/`Scene`/`Material`/`Object` (the unqualified
 *                   `EngineBindGroup` enumerator).
 * @param VarName    String literal — the `var<uniform> X: T;` name.
 * @param FileName   String literal — the file under `resources/shaders/core/`
 *                   to emit into.
 */
#define REGISTER_ENGINE_UNIFORM_AS(T, GroupId, VarName, FileName)                                    \
	namespace                                                                                        \
	{                                                                                                \
	const int ENGINE_PP_CONCAT(s_engineCoreReg_, __LINE__) = []() {                                  \
		::engine::rendering::shaders::core::EngineCoreRegistry::registerBinding({                    \
			&::engine::rendering::shaders::gpuStructDescriptorOf<T>(),                               \
			::engine::rendering::shaders::EngineBindGroup::GroupId,                                  \
			0,                                                                                       \
			(VarName),                                                                               \
			(FileName),                                                                              \
			::engine::rendering::shaders::GenBindingKind::UniformBuffer,                             \
		});                                                                                          \
		return 0;                                                                                    \
	}();                                                                                             \
	}

/**
 * @brief Same as @ref REGISTER_ENGINE_UNIFORM_AS but with @p VarName and
 *        @p FileName auto-derived from the struct's unqualified name.
 *
 *   REGISTER_ENGINE_UNIFORM(engine::rendering::FrameUniforms, Frame)
 *
 * Conventions (see `deriveVarName` / `deriveFileName`):
 *   `FrameUniforms`  → var `u_frame`,   file `frame_uniforms.wgsl`
 *   `ObjectUniforms` → var `u_object`,  file `object_uniforms.wgsl`
 *   `LightsBuffer`   → var `u_lights`,  file `lights_buffer.wgsl`
 *
 * Use `REGISTER_ENGINE_UNIFORM_AS` when the convention doesn't fit
 * (`PBRProperties` → `u_material`, etc.).
 */
#define REGISTER_ENGINE_UNIFORM(T, GroupId)                                                          \
	REGISTER_ENGINE_UNIFORM_AS(T, GroupId,                                                           \
		::engine::rendering::shaders::core::deriveVarName(ENGINE_PP_STRINGIFY(T)),                   \
		::engine::rendering::shaders::core::deriveFileName(ENGINE_PP_STRINGIFY(T)))

/**
 * @brief Emit just the WGSL struct definition for @p T into
 *        `resources/shaders/core/<FileName>`.
 *
 * No `@group/@binding` declaration is generated. The struct can then be used
 * by hand-written shader code at any @binding(N) the shader author picks —
 * appropriate for Scene's multi-binding layout where structs sit at
 * non-canonical slots.
 *
 *   REGISTER_ENGINE_STRUCT(engine::rendering::ShadowUniform, "shadow_uniform.wgsl")
 */
#define REGISTER_ENGINE_STRUCT(T, FileName)                                                          \
	namespace                                                                                        \
	{                                                                                                \
	const int ENGINE_PP_CONCAT(s_engineCoreStructOnly_, __LINE__) = []() {                           \
		::engine::rendering::shaders::core::EngineCoreRegistry::registerStructOnly({                 \
			&::engine::rendering::shaders::gpuStructDescriptorOf<T>(),                               \
			(FileName),                                                                              \
		});                                                                                          \
		return 0;                                                                                    \
	}();                                                                                             \
	}

/**
 * @brief Register a storage buffer wrapper: header struct + runtime array.
 *
 *   REGISTER_ENGINE_BUFFER_WRAPPER(
 *       engine::rendering::LightsBuffer,   // header (with count + alignment pads)
 *       engine::rendering::LightStruct,    // element struct
 *       "lights",                          // WGSL field name for the array
 *       "lights_buffer.wgsl",              // output file under core/
 *       Scene, 0, "u_lights")              // canonical bind-group placement
 *
 * Both @p HeaderT and @p ElementT must already have a `GpuStructTraits<T>`
 * specialisation (typically via `ENGINE_GPU_STRUCT`). The trailing
 * @p GroupId / @p BindingIndex / @p VarName let the validator confirm every
 * shader binding this wrapper places it at the canonical slot.
 */
#define REGISTER_ENGINE_BUFFER_WRAPPER(HeaderT, ElementT, ArrayFieldName, FileName,                  \
                                       GroupId, BindingIndex, VarName)                               \
	namespace                                                                                        \
	{                                                                                                \
	const int ENGINE_PP_CONCAT(s_engineCoreBufferWrapper_, __LINE__) = []() {                        \
		::engine::rendering::shaders::core::EngineCoreRegistry::registerBufferWrapper({              \
			&::engine::rendering::shaders::gpuStructDescriptorOf<HeaderT>(),                         \
			&::engine::rendering::shaders::gpuStructDescriptorOf<ElementT>(),                        \
			(ArrayFieldName),                                                                        \
			(FileName),                                                                              \
			::engine::rendering::shaders::EngineBindGroup::GroupId,                                  \
			static_cast<uint32_t>(BindingIndex),                                                     \
			(VarName),                                                                               \
		});                                                                                          \
		return 0;                                                                                    \
	}();                                                                                             \
	}
