# Peepo Drum Kit — web (WebAssembly) build

An experimental Emscripten build of [Peepo Drum Kit](https://github.com/samyuu/PeepoDrumKit) (by **samyuu**; via the forks of
[0auBSQ](https://github.com/0auBSQ/PeepoDrumKit), [Steve-xmh](https://github.com/Steve-xmh/PeepoDrumKit) (SDL3 port) and
[IepIweidieng](https://github.com/IepIweidieng/PeepoDrumKit-sdl3)) that runs the editor inside a browser tab, with a small
JavaScript API so that it can be embedded into another page.

It is the same C++ code as the desktop SDL3 build; only the platform layer differs:

| Area | Desktop (unchanged) | Web build |
|---|---|---|
| Rendering | SDL3 + SDL_GPU (`imgui_impl_sdlgpu3`) | SDL3 + WebGL 2 (`imgui_impl_opengl3`), `src/imgui/imgui_application_host_web.cpp` |
| Textures (SVG sprites rasterized by ThorVG) | `SDL_GPUTexture` | plain GL textures, `src/imgui/imgui_custom_draw.cpp` |
| Audio output | WASAPI / CoreAudio / libsoundio | SDL3 audio stream → Web Audio, `src/audio/audio_backend_sdl3.cpp` |
| Main loop | `SDL_EnterAppMainCallbacks` | same call, SDL drives it through `emscripten_set_main_loop` (requestAnimationFrame) |
| Background loading (`std::async`) | threads | deferred and run on the main thread (no threads at all) |
| Text encodings | ICU (icu4c) | hand written UTF-8 ⇄ wide, Shift-JIS decoding via the browser's `TextDecoder` |
| File dialogs | native (SDL3) | `<input type="file">` upload / download or a host callback, `src/core/core_io.cpp` |
| Settings | `.ini` files next to the executable | same files in `/persist`, stored in IndexedDB |

Everything web specific is behind `#ifdef __EMSCRIPTEN__`; `xmake.lua` is untouched.

## Building

Requirements: [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (developed with 7.0.0), Python 3.8+ and git.
No xmake, CMake or ICU is needed for the web build.

```bash
# once: install and activate the Emscripten SDK (any location)
git clone https://github.com/emscripten-core/emsdk
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh          # Windows: emsdk_env.bat (cmd) or .\emsdk_env.ps1 (PowerShell)
```

```bash
# in this repository
python web/build.py            # release build -> web/dist
python web/build.py --serve    # build, then serve web/dist on http://localhost:8080
```

`web/build.py` finds `em++` on the `PATH`, through the `EMSDK` environment variable, or in an `emsdk` checkout next to this repository.
On its first run it also fetches the dependencies the desktop build gets from xmake: Dear ImGui `v1.92.0-docking`, ThorVG `v1.0-pre10`
and stb into `web/deps/`, the `libs/dr_libs` git submodule, and SDL3 through Emscripten's own port (`--use-port=sdl3`).

Other options: `--debug` (`-O0 -g`, assertions), `--clean`, `--fonts all` (see below), `--out DIR`, `-j N`.

Output in `web/dist/` (about 21 MB, of which 16.5 MB is the CJK font; ~15 MB when served gzip-compressed):

| File | |
|---|---|
| `index.html` | the page (`web/shell.html`), also the iframe entry point |
| `peepodrumkit.js`, `peepodrumkit.wasm` | the program |
| `peepodrumkit.data` | preloaded `assets/` (graphics, sounds, Japanese font) and `locales/` |
| `assets/NotoSansCJK{sc,tc}-Regular.otf` | fetched only when a language that needs them is selected (`--fonts all` preloads them instead, +33 MB) |
| `license_txt/` | third-party license texts, keep them with any copy of the build |

Any static file server works (`.wasm` should be served as `application/wasm`). **No COOP / COEP headers are required**: the build does not
use threads or `SharedArrayBuffer`.

## Using it

Open the page, then use the editor like the desktop version:

- **File → Open** (Ctrl+O) opens the browser's file picker. Select the `.tja` **together with its audio file** (and jacket): the files are
  copied into an in-memory folder so that the chart's `WAVE:` reference can be resolved. Dropping files onto the editor works the same way.
  A browser page can't read the files next to the chart on its own, so if only the `.tja` was picked a banner
  ("Audio file … not found — Choose audio file") lets you add the song afterwards. File name case and sub folders in `WAVE:` don't matter.
- **File → Save / Save As** (Ctrl+S) downloads the chart as a UTF-8 `.tja` (or hands it to the host page, see below).
- Click the editor once before using keyboard shortcuts; keys only go to the editor while its canvas has focus.
- Two defaults differ from the desktop build: an opened chart starts on its **highest difficulty** (Ura > Oni > Hard > …) rather than
  the first course in the file, and the **Game Preview** tab is in front at startup instead of the usage guide.

## Embedding API

`peepodrumkit.js` is a classic Emscripten module. The page must contain a `<canvas id="canvas">` whose CSS size decides the size of the
editor (it follows layout changes), and define `Module.canvas` before loading the script; see `web/shell.html` for a complete page.
All functions live on `Module.pdk` (`web/pdk_api.js`):

```html
<div style="width: 100%; height: 720px"><canvas id="canvas" tabindex="0" style="width: 100%; height: 100%"></canvas></div>
<script>
  var Module = { canvas: document.getElementById("canvas"), pdkOptions: { warnOnUnload: false } };
</script>
<script src="peepodrumkit.js"></script>
<script>
  Module.pdk.ready.then(async (pdk) => {
    // 1. host -> editor: a TJA (text or bytes) plus the audio file's bytes
    const audio = await (await fetch("/songs/123/song.ogg")).arrayBuffer();
    pdk.openChart({ tja: tjaText, tjaName: "song.tja", audio, audioName: "song.ogg" });

    // 2. editor -> host: whenever the user saves (Ctrl+S / File > Save)
    pdk.onSave = (tjaText, fileName) => uploadToMyServer(tjaText);

    // ... or pull the current state (including unsaved edits) at any time
    const currentTja = pdk.getTJA();
  });
</script>
```

| Member | |
|---|---|
| `pdk.ready` | `Promise` resolving once the editor has started. Don't call anything else before that. |
| `pdk.openChart({ tja, tjaName?, audio?, audioName?, files?, discardUnsavedChanges? })` | Opens a chart. `tja` is a string or `Uint8Array`/`ArrayBuffer` (UTF-8 or Shift-JIS file bytes). `audio` is the bytes of an `.ogg`/`.mp3`/`.wav`/`.flac` file; `audioName` defaults to the chart's `WAVE:` header, and if it differs the chart's `WAVE:` is updated to it. `files` is an optional `{ name: bytes }` map of extra files placed next to the chart. Loading is asynchronous, poll `pdk.isLoading()`. |
| `pdk.loadAudio(bytes, fileName)` | Replaces the current chart's song (also sets `WAVE:`). |
| `pdk.getTJA()` | The current chart as TJA text (UTF-8, no BOM), including unsaved edits. |
| `pdk.save()` | Same as File → Save. |
| `pdk.onSave = (tjaText, fileName) => {}` | Called when the chart is saved. If unset, saving downloads the file instead. |
| `pdk.hasUnsavedChanges()` / `pdk.markSaved()` | Query / clear the editor's "unsaved changes" state. |
| `pdk.newChart()`, `pdk.isLoading()`, `pdk.saveSettings()` | |
| `pdk.onReady`, `pdk.onTitleChange = (title) => {}` | Optional callbacks. Without `onTitleChange` the editor sets `document.title`. |
| `pdk.onMissingAudio = (waveName) => {}` | A chart was opened without the audio file its `WAVE:` header refers to (answer with `pdk.loadAudio`). |
| `Module.pdkOptions` | Set before loading the script: `persistSettings`, `warnOnUnload`, `handleFileDrop`, `setDocumentTitle`, `missingAudioPrompt` (all default `true`). |

The same things are exported as plain C functions (`_pdk_web_open_chart`, `_pdk_web_get_tja`, …, see
`src/peepodrumkit/chart_editor_web.cpp`) for anyone who prefers `ccall`.

### iframe + postMessage

When `index.html` runs inside an `<iframe>` it also speaks a small `postMessage` protocol, which keeps the editor's keyboard handling and
globals isolated from the host page. Messages are only accepted from, and sent to, the page's own origin, or the origin passed as
`index.html?parentOrigin=https://host.example`.

| Direction | Message |
|---|---|
| editor → host | `{ type: "pdk:ready" }`, `{ type: "pdk:save", tja, fileName }`, `{ type: "pdk:tja", requestId, tja, hasUnsavedChanges }`, `{ type: "pdk:opened", requestId }`, `{ type: "pdk:error", requestId, message }` |
| host → editor | `{ type: "pdk:openChart", requestId?, chart: { tja, tjaName, audio, audioName } }`, `{ type: "pdk:loadAudio", audio, audioName }`, `{ type: "pdk:getTJA", requestId }`, `{ type: "pdk:markSaved" }`, `{ type: "pdk:newChart" }` |

## Known limitations

Compared to the desktop build:

- **Single threaded.** Charts, audio and sprites that the desktop build loads on background threads are loaded on the main thread, so the
  page freezes for a moment while a song is decoded (a few seconds for a long MP3/OGG).
- **Audio latency.** Output goes through SDL3 → Web Audio (a `ScriptProcessorNode` on the main thread, since `AudioWorklet` would need
  threads). The buffer is at least 1024 frames (~45 ms total); the *buffer size* and *exclusive mode* settings have no effect, there is no
  device selection, and audio can crackle while the tab is busy or in the background. Browsers only start audio after the first click or key press.
- **Files live in memory.** There is no access to the real file system: opening = upload into memory, saving = download (or `onSave`).
  "Save" can't overwrite the original file, the `.tja.bak` backup the desktop build writes next to the chart is not downloaded,
  "Open chart directory" does nothing, and the *recent files* list is meaningless after a reload.
- **Shift-JIS is decode-only** (via `TextDecoder`); like the desktop build, charts are always saved as UTF-8. BOM-less UTF-8 charts are
  detected and opened as UTF-8 (the desktop build treats them as Shift-JIS).
- **Browser-reserved shortcuts** such as Ctrl+N, Ctrl+T and Ctrl+W can't be intercepted; use the menu (or rebind them in the settings) instead.
- **File → Exit** does nothing. Fullscreen uses the browser's fullscreen mode. There are no extra OS windows (docking inside the canvas works).
- **WebGL 2 is required.** Only tested in Chromium so far; Firefox and Safari are untested (Safari may refuse to open the file picker
  from the menu, dropping files onto the editor should still work). Clipboard and IME text input are untested.
- **Download size**: ~21 MB uncompressed, most of it the CJK font.
- It is based on the SDL3 branch, which is behind the Windows-only [IepIweidieng/PeepoDrumKit](https://github.com/IepIweidieng/PeepoDrumKit) (v1.2.0) in features.

## Layout

```
web/build.py        build script (fetches deps, compiles, links, copies the page)
web/pdk_api.js      JS glue + Module.pdk API (linked in with --pre-js)
web/shell.html      the page, becomes dist/index.html
web/deps/           fetched third-party sources (git ignored)
web/build/          object cache (git ignored)
web/dist/           output (git ignored)
```
