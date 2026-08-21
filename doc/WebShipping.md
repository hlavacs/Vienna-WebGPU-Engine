# Shipping to the Web (emdawnwebgpu)

How to build the engine for the browser and put it on an actual webpage. The web
backend is **emdawnwebgpu** (Dawn's Emscripten port, vendored in
`external/webgpu/emdawnwebgpu/pkg/`); the implementation at runtime is the browser's
own WebGPU. Requires **emsdk >= 4.0.10** (the vendored pkg is tested against 5.0.6):

```
git clone https://github.com/emscripten-core/emsdk %USERPROFILE%\emsdk
%USERPROFILE%\emsdk\emsdk install 5.0.6 && %USERPROFILE%\emsdk\emsdk activate 5.0.6
```

## Build

From a shell where `emsdk_env` has run (and cmake/ninja are on PATH):

```
scripts\build-example.bat main_demo Release EMDAWN
```

Output in `examples/build/main_demo/Emscripten/Release/`:

| File | Size (main_demo) | What it is |
|---|---|---|
| `MainDemo.html` | ~6 KB | the page (from `src/shell_web.html`) |
| `MainDemo.js` | ~250 KB | emscripten loader + emdawnwebgpu JS glue |
| `MainDemo.wasm` | ~4.4 MB | the engine |
| `MainDemo.data` | ~58 MB | `resources/` + the example's `assets/`, preloaded into MEMFS |

`src/shell_web.html` is the default shell (loading bar, WebGPU-support check, engine
log panel, fullscreen button); pass `SHELL_FILE` to `add_engine_executable` for a
custom one. `src/shell_minimal.html` is the bare emscripten reference. The shell and
the demo landing page (`src/web_index.html`) share their styling via
`src/web_theme.css`. The shell's log panel is batched: engine output is buffered and
flushed to the DOM once per second (2000-line cap), so per-frame logging can never
stall the page - wasm builds also raise the spdlog level to `info` so trace/debug
spam never crosses the JS boundary in the first place.

## Test locally

```
python -m http.server 8080 --directory examples\build\main_demo\Emscripten\Release
```

then open `http://localhost:8080/MainDemo.html`. WebGPU works on `localhost` without
TLS; any other host needs HTTPS.

## Hosting on a real webpage

Any static host works (GitHub Pages, Netlify, nginx, S3+CDN). Upload the four files.
This repo automates it: `.github/workflows/web-demo.yml` builds every example
(matrix, minus runtime_player which ships no assets) on tagged commits or manual
dispatch and publishes them to GitHub Pages behind `src/web_index.html`.

- **HTTPS is mandatory** - browsers expose WebGPU only in secure contexts.
- **MIME types**: `.wasm` must be served as `application/wasm`; `.data` as
  `application/octet-stream`. Most hosts do this out of the box.
- **Compression matters**: the 58 MB `.data` compresses well (textures/models);
  enable gzip or brotli on the host. For long-term hosting, add far-future cache
  headers to `.wasm`/`.data` and version the filenames when updating.
- **No special isolation headers needed**: the build is single-threaded (no
  `-pthread`), so COOP/COEP / SharedArrayBuffer requirements do not apply.
- **Embedding in an existing page**: simplest is an `<iframe src=".../MainDemo.html">`.
  For native embedding, copy the `<canvas id="canvas">`, the `Module` script block
  from `shell_web.html`, and a `<script src="MainDemo.js">` tag into your page.
- Browser support: Chrome/Edge (stable), Firefox 141+, Safari 26+; see caniuse.com/webgpu.

## Web-platform constraints baked into the engine

Found while porting; all are guarded with `__EMSCRIPTEN__` (or generic guards):

- `PathProvider` roots at MEMFS `/`; `--preload-file` maps engine `resources/` to
  `/resources` and the example's `assets/` to `/assets` (see `add_engine_executable`).
- **No threads** without `-pthread` (which would force COOP/COEP hosting):
  `SceneManager` scene init runs inline (its std::async was awaited immediately
  anyway) and the physics thread is disabled with a warning in wasm builds.
- **No `encoder.writeTimestamp`** in the browser API (only pass-level
  timestampWrites): the FrameProfiler's GPU timing is disabled in wasm builds.
- **No explicit present**: the browser presents at the end of the rAF callback;
  `Renderer` skips `surface.present()` under emscripten.
- Zero-size window events (canvas before CSS layout; also native minimize) are
  ignored in `GameEngine::onWindowResize` - WebGPU forbids 0x0 textures.
- `-sASYNCIFY` is required (the wrapper's blocking requestAdapter/requestDevice
  helpers); `-sASSERTIONS` is Debug-only.
- The legacy `WEBGPU_BACKEND=EMSCRIPTEN` flavor (`-sUSE_WEBGPU`) predates the v29
  header generation and no longer compiles against this engine - use EMDAWN.

## Scene editor in the browser

The scene editor ships as a web demo too, with browser replacements for every native
file dialog (`tinyfiledialogs` has no wasm backend - calling it aborts with
`null function`):

- **Open Project** uses the browser's directory picker (`webkitdirectory`): the whole
  project folder is copied into MEMFS under `/projects/<name>/` and opened from there.
  A second menu entry, **Open Project Zip...**, accepts a `.zip` of the project folder
  instead (unpacked with the vendored miniz).
- **Save / export** goes the other way: the project directory is zipped in MEMFS and
  handed to the browser as a download. Nothing is ever written to a server - open and
  save are entirely client-side.
- Single-file pickers (import asset, open scene) use `<input type="file">`; message
  boxes and text prompts map to `confirm()`/`prompt()` with graceful fallbacks where
  the embedding context blocks them.
- All of this lives in `examples/scene_editor/SceneEditorUI.cpp` behind
  `__EMSCRIPTEN__` guards (`EM_ASYNC_JS`/`EM_JS` interop).
