# WebGPU v24 Migration — Known Gotchas

Reference for the bump from the old pinned **wgpu-v0.19.4.1** to the unified
eliemichel WebGPU-distribution (**wgpu-native v24.0.0.2**, Dawn, emdawnwebgpu) +
Dear ImGui **1.92.9b-docking**. Code comments stay short and point here.

## Strings — `WGPUStringView`
All string fields (`label`, shader `code`, `entryPoint`, …) are now
`WGPUStringView { const char* data; size_t length }`, not `const char*`. Wrap with
`wgpu::StringView(...)`. It is **non-owning**: the backing string must outlive the
create call, so build a descriptor label from a temporary via a named local
(a temporary's `.c_str()` would dangle).

## Limits
`WGPUSupportedLimits` / `WGPURequiredLimits` are gone; there is a single flat
`wgpu::Limits` (no nested `.limits`). `getLimits()` returns a `Status`.
`maxInterStageShaderComponents` was removed — use `maxInterStageShaderVariables`.

- `maxColorAttachmentBytesPerSample` / `maxColorAttachments` are enforced now
  (spec default 32 bytes/sample). **wgpu-native reports them as 0** from
  `adapter.getLimits`, and requesting a higher value via `requiredLimits` does
  not raise them — the device stays at 32. The deferred G-buffer packs 40
  bytes/sample, so on wgpu-native it currently exceeds the limit. Open item:
  needs a native-limits path or a G-buffer format that fits in 32.

## Bind-group layout entries — `BindingNotUsed` vs `Undefined`
The enums shifted: `*BindingType`/`TextureSampleType` now have
`BindingNotUsed = 0` and `Undefined = 1` (was `Undefined = 0`). A sub-layout
counts as *used* unless its type is `BindingNotUsed (0)`.

- **Do not use `wgpu::Default` on a `BindGroupLayoutEntry`.** The wrapper's
  `setDefault()` writes `Undefined (1)` to all four sub-layouts, so every entry
  reads as "buffer+sampler+texture+storageTexture present" and wgpu mis-types it
  (a buffer binding reads as a Texture). Zero-init (`{}` / `vector(n)`) leaves
  them at `BindingNotUsed`; set only the one active sub-layout. (This is why the
  reflection path, which uses `entry{}`, always worked.)
- Type dispatch must test `!= BindingNotUsed`, not `!= Undefined`, or it treats
  every binding as a buffer (auto-creates buffers for texture/sampler slots).

## Zero sizes are invalid
Creating a zero-size buffer, or a bind-group entry with `size == 0`, aborts
wgpu-native (old build tolerated it). Runtime-sized storage bindings report
`minBindingSize == 0`; use a placeholder buffer size and bind the buffer's actual
size, not `minBindingSize`.

## Flag typedefs
`WGPUBufferUsageFlags` / `WGPUTextureUsageFlags` → `WGPUBufferUsage` /
`WGPUTextureUsage` (all `WGPUFlags` = `uint64_t`).

## Async buffer mapping
`Buffer::mapAsync(mode, offset, size, callbackInfo)` takes a
`BufferMapCallbackInfo` (captureless callback + `userdata`) and returns a
`Future`; there is no callback-holder to own. Status is `MapAsyncStatus`. We use
`CallbackMode::AllowSpontaneous` so the callback fires during normal frame work
without an explicit poll (matches old behavior; verify async readback at runtime).

## Copies, shaders, misc renames
- `ImageCopyTexture`/`ImageCopyBuffer` → `TexelCopyTextureInfo`/`TexelCopyBufferInfo`; `TextureDataLayout` → `TexelCopyBufferLayout`.
- `ShaderModuleWGSLDescriptor` → `ShaderSourceWGSL`; `ShaderModuleDescriptor.hintCount`/`hints` removed.
- `DepthStencilState.depthWriteEnabled` is `WGPUOptionalBool`, not `bool`.
- Uncaptured errors: set `DeviceDescriptor.uncapturedErrorCallbackInfo` (captureless fn ptr); `Device::setUncapturedErrorCallback` removed.
- Surface format: `Surface::getPreferredFormat` removed → `getCapabilities(adapter, &caps)`, use `caps.formats[0]`, then `caps.freeMembers()`.

## Native features
`encoder.writeTimestamp()` (timestamps recorded directly on a command encoder,
used by FrameProfiler) needs the native feature
`WGPUNativeFeature_TimestampQueryInsideEncoders` in addition to `TimestampQuery`;
wgpu-native only. Enabled under `WEBGPU_BACKEND_WGPU`, else GPU timing is disabled.

## sdl3webgpu / ImGui
- Win32 surface uses `WGPUSurfaceSourceWindowsHWND` + `WGPUStringView` label (matches the X11/Wayland/Cocoa blocks).
- ImGui 1.92 replaced the 4-arg `ImGui_ImplWGPU_Init` with an `ImGui_ImplWGPU_InitInfo` struct, and the imgui CMake target needs one of `IMGUI_IMPL_WEBGPU_BACKEND_{WGPU,DAWN}` (mapped from `WEBGPU_BACKEND`).
