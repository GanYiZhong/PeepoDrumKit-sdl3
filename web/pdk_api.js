// Peepo Drum Kit web build: JavaScript glue + host page integration API.
// Linked into peepodrumkit.js via --pre-js, so this runs inside the Emscripten module scope before the runtime starts.
//
// Public API (see web/README.md):  Module.pdk
// Internal hooks called from C++:  Module.pdkInternal

(function () {
  var WORK_DIR = "/work";
  var PERSIST_DIR = "/persist";
  var AUDIO_EXTENSIONS = ["ogg", "mp3", "wav", "flac"];
  var IMAGE_EXTENSIONS = ["png", "jpg", "jpeg"];
  var options = Module.pdkOptions || {};

  var readyResolve;
  var readyPromise = new Promise(function (resolve) { readyResolve = resolve; });

  function extensionOf(name) {
    var dot = name.lastIndexOf(".");
    return dot < 0 ? "" : name.substring(dot + 1).toLowerCase();
  }

  function sanitizeFileName(name) {
    name = String(name || "").replace(/\\/g, "/");
    name = name.substring(name.lastIndexOf("/") + 1).replace(/[:*?"<>|]/g, "_");
    return name || "untitled";
  }

  function toUint8Array(data) {
    if (data instanceof Uint8Array) return data;
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    throw new TypeError("expected a Uint8Array or ArrayBuffer");
  }

  var BOM = [0xEF, 0xBB, 0xBF];
  function hasBOM(bytes) { return bytes.length >= 3 && bytes[0] === BOM[0] && bytes[1] === BOM[1] && bytes[2] === BOM[2]; }
  function withBOM(bytes) {
    var out = new Uint8Array(bytes.length + 3);
    out.set(BOM, 0); out.set(bytes, 3);
    return out;
  }

  // The editor treats a TJA without a UTF-8 BOM as Shift-JIS. Many modern charts are BOM-less UTF-8 though,
  // so add the BOM whenever the bytes are valid UTF-8 (pure ASCII is valid in both and unaffected).
  function normalizeTJABytes(tja) {
    if (typeof tja === "string") return withBOM(new TextEncoder().encode(tja));
    var bytes = toUint8Array(tja);
    if (hasBOM(bytes)) return bytes;
    try { new TextDecoder("utf-8", { fatal: true }).decode(bytes); return withBOM(bytes); }
    catch (e) { return bytes; }
  }

  function decodeTJABytes(bytes) {
    if (hasBOM(bytes)) return new TextDecoder("utf-8").decode(bytes.subarray(3));
    try { return new TextDecoder("utf-8", { fatal: true }).decode(bytes); }
    catch (e) { return new TextDecoder("shift_jis").decode(bytes); }
  }

  function findWaveName(tjaText) {
    var match = /^\s*WAVE\s*:\s*(.+?)\s*$/im.exec(tjaText);
    return match ? match[1] : "";
  }

  function writeWorkFile(name, bytes) {
    try { FS.mkdir(WORK_DIR); } catch (e) { /* already exists */ }
    var path = WORK_DIR + "/" + sanitizeFileName(name);
    FS.writeFile(path, bytes);
    return path;
  }

  function callWithString(functionName, string, extraArgs) {
    return Module.ccall(functionName, "number", ["string"].concat((extraArgs || []).map(function () { return "number"; })), [string].concat(extraArgs || []));
  }

  function downloadBytes(bytes, fileName, mimeType) {
    var url = URL.createObjectURL(new Blob([bytes], { type: mimeType || "application/octet-stream" }));
    var a = document.createElement("a");
    a.href = url; a.download = fileName; a.style.display = "none";
    document.body.appendChild(a);
    a.click();
    setTimeout(function () { document.body.removeChild(a); URL.revokeObjectURL(url); }, 1000);
  }

  function syncPersistentStorage() {
    try { FS.syncfs(false, function (error) { if (error) console.warn("[PeepoDrumKit] failed to persist settings", error); }); }
    catch (e) { /* IDBFS unavailable (e.g. private browsing) */ }
  }

  // The exported C functions only exist once the WebAssembly module has been instantiated
  function isEditorRunning() {
    return typeof Module._pdk_web_is_ready === "function" && Module._pdk_web_is_ready() === 1;
  }

  function saveSettings() {
    if (!isEditorRunning()) return;
    Module._pdk_web_save_settings();
    syncPersistentStorage();
  }

  // Copies picked / dropped browser File objects into the virtual file system, returns [{ name, path }]
  function importBrowserFiles(fileList) {
    return Promise.all(Array.prototype.map.call(fileList, function (file) {
      return file.arrayBuffer().then(function (buffer) {
        var bytes = new Uint8Array(buffer);
        if (extensionOf(file.name) === "tja") bytes = normalizeTJABytes(bytes);
        return { name: file.name, path: writeWorkFile(file.name, bytes) };
      });
    }));
  }

  function pickPrimaryFile(files, preferredExtensions) {
    for (var i = 0; i < preferredExtensions.length; i++)
      for (var j = 0; j < files.length; j++)
        if (extensionOf(files[j].name) === preferredExtensions[i]) return files[j];
    return files[0];
  }

  // ------------------------------------------------------------------------------------------------------------------
  // Resolving a chart's WAVE: reference
  // A browser can't read the files next to a picked .tja, so the song only loads automatically when it was picked /
  // dropped / passed in together with the chart. If it wasn't, a small banner offers to pick it afterwards.
  // ------------------------------------------------------------------------------------------------------------------
  function pathExists(path) { try { return FS.analyzePath(path).exists; } catch (e) { return false; } }
  function baseNameKey(path) {
    path = String(path).replace(/\\/g, "/");
    return path.substring(path.lastIndexOf("/") + 1).normalize("NFC").toLowerCase();
  }

  var missingAudioBanner = null;
  function hideMissingAudioBanner() {
    if (missingAudioBanner && missingAudioBanner.parentNode) missingAudioBanner.parentNode.removeChild(missingAudioBanner);
    missingAudioBanner = null;
  }

  // Runs the callback once the editor has finished loading the chart (relative paths are resolved against the loaded chart)
  function whenNotLoading(callback) {
    var attempts = 0;
    (function poll() {
      if (!Module._pdk_web_is_loading() || ++attempts > 600) callback();
      else setTimeout(poll, 50);
    })();
  }

  function loadAudioKeepingSavedState(path) {
    whenNotLoading(function () {
      var wasUnsaved = Module._pdk_web_has_unsaved_changes() === 1;
      callWithString("pdk_web_load_audio", path);
      if (!wasUnsaved) Module._pdk_web_mark_saved();
    });
  }

  function showMissingAudioBanner(waveName, expectedPath) {
    hideMissingAudioBanner();
    if (typeof Module.pdk.onMissingAudio === "function") Module.pdk.onMissingAudio(waveName);
    if (options.missingAudioPrompt === false) return;

    var chinese = /^zh/i.test(navigator.language || "");
    var canvas = Module.canvas || document.getElementById("canvas");
    var rect = canvas ? canvas.getBoundingClientRect() : { top: 0, left: 0, width: window.innerWidth };

    var banner = document.createElement("div");
    banner.style.cssText = "position:fixed;z-index:1000;display:flex;gap:10px;align-items:center;max-width:90%;padding:8px 12px;" +
      "background:#2b2b2b;color:#eee;border:1px solid #b58a2a;border-radius:6px;font:13px/1.4 system-ui,sans-serif;box-shadow:0 4px 16px rgba(0,0,0,.5);";
    banner.style.top = (rect.top + 40) + "px";
    banner.style.left = (rect.left + rect.width / 2) + "px";
    banner.style.transform = "translateX(-50%)";

    var text = document.createElement("span");
    text.textContent = chinese
      ? "找不到音檔「" + waveName + "」（瀏覽器無法自動讀取譜面旁邊的檔案）"
      : "Audio file \"" + waveName + "\" not found (a browser can't read the files next to the chart)";
    var pick = document.createElement("button");
    pick.textContent = chinese ? "選擇音檔" : "Choose audio file";
    var close = document.createElement("button");
    close.textContent = "×";
    close.setAttribute("aria-label", chinese ? "關閉" : "Dismiss");
    [pick, close].forEach(function (button) {
      button.style.cssText = "cursor:pointer;padding:4px 10px;border-radius:4px;border:1px solid #666;background:#3a3a3a;color:#eee;font:inherit;white-space:nowrap;";
    });

    pick.addEventListener("click", function () {
      var input = document.createElement("input");
      input.type = "file";
      input.accept = AUDIO_EXTENSIONS.map(function (e) { return "." + e; }).join(",");
      input.addEventListener("change", function () {
        var file = input.files && input.files[0];
        if (!file) return;
        file.arrayBuffer().then(function (buffer) {
          var bytes = new Uint8Array(buffer);
          hideMissingAudioBanner();
          if (baseNameKey(file.name) === baseNameKey(waveName)) {
            // The file the chart asked for: put it where the chart expects it, the chart itself stays unchanged
            FS.mkdirTree(expectedPath.substring(0, expectedPath.lastIndexOf("/")));
            FS.writeFile(expectedPath, bytes);
            // (passed exactly as written in the chart so that its WAVE: header is left untouched)
            loadAudioKeepingSavedState(waveName);
          } else {
            // A different file: the chart's WAVE: header is updated to it (and the chart marked as changed)
            var pickedPath = writeWorkFile(file.name, bytes);
            whenNotLoading(function () { callWithString("pdk_web_load_audio", pickedPath); });
          }
          if (canvas) canvas.focus();
        });
      });
      input.click();
    });
    close.addEventListener("click", function () { hideMissingAudioBanner(); if (canvas) canvas.focus(); });

    banner.appendChild(text); banner.appendChild(pick); banner.appendChild(close);
    document.body.appendChild(banner);
    missingAudioBanner = banner;
  }

  // Called after a chart file has been written to /work and before the editor starts loading it
  function resolveChartAudio(tjaPath) {
    hideMissingAudioBanner();
    var waveName = findWaveName(decodeTJABytes(FS.readFile(tjaPath)));
    if (!waveName) return;

    var expectedPath = WORK_DIR + "/" + waveName.replace(/\\/g, "/");
    if (pathExists(expectedPath)) return;

    // The virtual file system is case sensitive and flat, unlike the folder the chart came from:
    // accept "Song.OGG" for "song.ogg" and "song.ogg" for "audio/song.ogg" by linking it to the expected path
    var wanted = baseNameKey(waveName);
    var candidates = FS.readdir(WORK_DIR).filter(function (name) { return name !== "." && name !== ".." && baseNameKey(name) === wanted; });
    if (candidates.length > 0) {
      try {
        FS.mkdirTree(expectedPath.substring(0, expectedPath.lastIndexOf("/")));
        FS.symlink(WORK_DIR + "/" + candidates[0], expectedPath);
        return;
      } catch (e) { console.warn("[PeepoDrumKit] could not link the audio file", e); }
    }
    showMissingAudioBanner(waveName, expectedPath);
  }

  function afterBrowserFilesImported(primary) {
    var extension = extensionOf(primary.name);
    if (extension === "tja") resolveChartAudio(primary.path);
    else if (AUDIO_EXTENSIONS.indexOf(extension) >= 0) hideMissingAudioBanner();
  }

  // ------------------------------------------------------------------------------------------------------------------
  // Hooks called from C++
  // ------------------------------------------------------------------------------------------------------------------
  Module.pdkInternal = {
    onReady: function () {
      readyResolve(Module.pdk);
      if (typeof Module.pdk.onReady === "function") Module.pdk.onReady(Module.pdk);
    },

    onWindowTitle: function (title) {
      if (typeof Module.pdk.onTitleChange === "function") Module.pdk.onTitleChange(title);
      else if (options.setDocumentTitle !== false) document.title = title;
    },

    // packedExtensions: "tja" or "flac;ogg;mp3;wav" (empty = any file)
    openFileDialog: function (packedExtensions) {
      var wanted = packedExtensions.split(";").map(function (e) { return e.replace(/^\*?\./, "").toLowerCase(); }).filter(Boolean);
      var accept = wanted.slice();
      // Let the user pick the song (and jacket) together with the chart so that the WAVE: reference can be resolved
      if (wanted.indexOf("tja") >= 0) accept = accept.concat(AUDIO_EXTENSIONS, IMAGE_EXTENSIONS);

      var input = document.createElement("input");
      input.type = "file";
      input.multiple = true;
      input.style.display = "none";
      if (accept.length > 0) input.accept = accept.map(function (e) { return "." + e; }).join(",");

      var finished = false;
      function finish(path) {
        if (finished) return;
        finished = true;
        if (input.parentNode) input.parentNode.removeChild(input);
        Module.ccall("pdk_web_file_dialog_result", null, [path === null ? "number" : "string"], [path === null ? 0 : path]);
      }
      input.addEventListener("change", function () {
        if (!input.files || input.files.length === 0) { finish(null); return; }
        importBrowserFiles(input.files).then(function (files) {
          var primary = pickPrimaryFile(files, wanted);
          afterBrowserFilesImported(primary);
          finish(primary.path);
        }, function (error) { console.error("[PeepoDrumKit] failed to read the selected files", error); finish(null); });
      });
      input.addEventListener("cancel", function () { finish(null); });
      document.body.appendChild(input);
      input.click();
    },

    // The editor wrote a file into /work (File > Save / Save As). Charts go to the host page, or become a download
    onWorkFileWritten: function (path) {
      var bytes = FS.readFile(path);
      var fileName = path.substring(path.lastIndexOf("/") + 1);
      if (extensionOf(fileName) === "tja") {
        var text = decodeTJABytes(bytes);
        if (typeof Module.pdk.onSave === "function") { Module.pdk.onSave(text, fileName); return; }
        downloadBytes(bytes, fileName, "text/plain;charset=utf-8");
      } else {
        downloadBytes(bytes, fileName);
      }
    },

    fetchFontFile: function (fileName, outPath) {
      var internal = Module.pdkInternal;
      internal.pendingFonts = internal.pendingFonts || {};
      if (internal.pendingFonts[fileName]) return;
      internal.pendingFonts[fileName] = true;
      var url = Module.locateFile ? Module.locateFile("assets/" + fileName, "") : "assets/" + fileName;
      fetch(url).then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.arrayBuffer();
      }).then(function (buffer) {
        FS.writeFile(outPath, new Uint8Array(buffer));
        Module._pdk_web_font_file_fetched();
      }).catch(function (error) {
        // Left marked as pending on purpose so that a missing font isn't requested again every frame
        console.warn("[PeepoDrumKit] failed to fetch font '" + fileName + "'", error);
      });
    },
  };

  // ------------------------------------------------------------------------------------------------------------------
  // Public host integration API
  // ------------------------------------------------------------------------------------------------------------------
  Module.pdk = {
    /** Promise that resolves (with this object) once the editor has started and the functions below can be used. */
    ready: readyPromise,

    /** Optional callbacks, assign functions to them:
     *    onReady(pdk)                called once after startup
     *    onSave(tjaText, fileName)   the user saved the chart (Ctrl+S / File > Save). If unset the chart is downloaded instead
     *    onMissingAudio(waveName)    a chart was opened without the audio file its WAVE: header refers to
     *    onTitleChange(title)        the editor wants to change the window title (contains the chart name and a '*' if unsaved) */
    onReady: null,
    onSave: null,
    onMissingAudio: null,
    onTitleChange: null,

    /** Opens a chart, optionally together with its song.
     *    chart.tja                    string (UTF-8 text) or Uint8Array / ArrayBuffer (file bytes, UTF-8 or Shift-JIS)
     *    chart.tjaName                file name shown in the title bar and used when saving (default "chart.tja")
     *    chart.audio                  Uint8Array / ArrayBuffer of an .ogg / .mp3 / .wav / .flac file (optional)
     *    chart.audioName              file name of the audio, defaults to the chart's WAVE: header
     *    chart.files                  { "name.ext": bytes, ... } extra files next to the chart, e.g. a jacket image (optional)
     *    chart.discardUnsavedChanges  default true; false shows the editor's own "save changes?" popup first
     *  Returns the virtual path of the chart. Loading itself is asynchronous, see isLoading(). */
    openChart: function (chart) {
      if (!chart || chart.tja == null) throw new TypeError("openChart: 'tja' is required");
      var tjaBytes = normalizeTJABytes(chart.tja);
      var tjaText = typeof chart.tja === "string" ? chart.tja : decodeTJABytes(tjaBytes);
      var tjaName = sanitizeFileName(chart.tjaName || "chart.tja");
      if (extensionOf(tjaName) !== "tja") tjaName += ".tja";

      if (chart.files) Object.keys(chart.files).forEach(function (name) { writeWorkFile(name, toUint8Array(chart.files[name])); });
      if (chart.audio != null) writeWorkFile(chart.audioName || findWaveName(tjaText) || "song.ogg", toUint8Array(chart.audio));

      var tjaPath = writeWorkFile(tjaName, tjaBytes);
      resolveChartAudio(tjaPath);
      callWithString("pdk_web_open_chart", tjaPath, [chart.discardUnsavedChanges === false ? 0 : 1]);

      // The chart's own WAVE: header wins when the names match; otherwise point the (now loading) chart at the given audio
      // (skipped when the editor may still be showing its "save changes?" popup, the audio file is available next to the chart either way)
      if (chart.discardUnsavedChanges !== false && chart.audio != null && chart.audioName && sanitizeFileName(chart.audioName) !== sanitizeFileName(findWaveName(tjaText))) {
        var audioPath = WORK_DIR + "/" + sanitizeFileName(chart.audioName);
        var attempts = 0;
        var timer = setInterval(function () {
          if (!Module._pdk_web_is_loading() || ++attempts > 600) {
            clearInterval(timer);
            callWithString("pdk_web_load_audio", audioPath);
          }
        }, 50);
      }
      return tjaPath;
    },

    /** Replaces the song of the current chart (also updates its WAVE: header). */
    loadAudio: function (bytes, fileName) {
      return callWithString("pdk_web_load_audio", writeWorkFile(fileName || "song.ogg", toUint8Array(bytes))) === 1;
    },

    /** Returns the current chart as TJA text (UTF-8, no BOM), including unsaved edits. */
    getTJA: function () {
      var pointer = Module._pdk_web_get_tja();
      if (!pointer) return "";
      var text = Module.UTF8ToString(pointer);
      Module._free(pointer);
      return text;
    },

    /** Same as File > Save: results in onSave(tjaText, fileName) being called (or a download if there is no handler). */
    save: function () { Module._pdk_web_save_chart(); },

    newChart: function () { Module._pdk_web_new_chart(); },
    hasUnsavedChanges: function () { return Module._pdk_web_has_unsaved_changes() === 1; },
    /** Clears the "unsaved changes" state, e.g. after the host stored the result of getTJA() itself. */
    markSaved: function () { Module._pdk_web_mark_saved(); },
    /** True while a chart or song is still being parsed / decoded. */
    isLoading: function () { return Module._pdk_web_is_loading() === 1; },

    /** Writes the settings files and flushes them to IndexedDB. Called automatically every 30 s and when the page is hidden. */
    saveSettings: saveSettings,
  };

  // ------------------------------------------------------------------------------------------------------------------
  // Startup: virtual file system, persistence, drag and drop
  // ------------------------------------------------------------------------------------------------------------------
  Module.preRun = Module.preRun || [];
  if (typeof Module.preRun === "function") Module.preRun = [Module.preRun];
  Module.preRun.push(function () {
    FS.mkdir(WORK_DIR);
    FS.mkdir(PERSIST_DIR);
    if (options.persistSettings === false) return;
    try {
      FS.mount(IDBFS, {}, PERSIST_DIR);
      addRunDependency("pdk-idbfs");
      FS.syncfs(true, function (error) {
        if (error) console.warn("[PeepoDrumKit] settings could not be restored", error);
        removeRunDependency("pdk-idbfs");
      });
    } catch (e) {
      console.warn("[PeepoDrumKit] persistent settings are unavailable", e);
    }
  });

  if (options.persistSettings !== false) {
    setInterval(saveSettings, 30000);
    document.addEventListener("visibilitychange", function () { if (document.visibilityState === "hidden") saveSettings(); });
    window.addEventListener("pagehide", saveSettings);
  }

  if (options.warnOnUnload !== false) {
    window.addEventListener("beforeunload", function (event) {
      if (isEditorRunning() && Module._pdk_web_has_unsaved_changes()) {
        event.preventDefault();
        event.returnValue = "";
      }
    });
  }

  // Files dropped onto the canvas behave like files dropped onto the desktop window
  if (options.handleFileDrop !== false) {
    var dropTarget = Module.canvas || document.getElementById("canvas");
    if (dropTarget) {
      dropTarget.addEventListener("dragover", function (event) { event.preventDefault(); });
      dropTarget.addEventListener("drop", function (event) {
        // SDL registers its own (per file, non BOM aware) drop handling on the canvas; this one replaces it
        event.preventDefault();
        event.stopImmediatePropagation();
        var dropped = event.dataTransfer && event.dataTransfer.files;
        if (!dropped || dropped.length === 0) return;
        importBrowserFiles(dropped).then(function (files) {
          var primary = pickPrimaryFile(files, ["tja"].concat(AUDIO_EXTENSIONS, IMAGE_EXTENSIONS));
          afterBrowserFilesImported(primary);
          callWithString("pdk_web_drop_file", primary.path);
        });
      }, true);
    }
  }
})();
