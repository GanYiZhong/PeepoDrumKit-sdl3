// NOTE: Web (Emscripten) application host: SDL3 + WebGL2 (OpenGL ES 3.0) instead of SDL_GPU, which has no browser backend.
//		 The desktop host lives in "imgui_application_host.cpp"
#ifdef __EMSCRIPTEN__

#include "imgui_application_host.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>

#define SDL_MAIN_HANDLED // We defined our own main()
#include <SDL3/SDL_main.h>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <filesystem>
#include <iostream>

#include "core_io.h"
#include "core_string.h"

EmbeddedIconsList GetEmbeddedIconsList()
{
	return EmbeddedIconsList{0};
}

namespace ApplicationHost
{
	static constexpr ImVec4 clearColor = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);

	static struct
	{
		StartupParam StartupParam = {};
		UserCallbacks UserCallbacks = {};
		SDL_Window* SDL_WindowHandle = nullptr;
		SDL_GLContext GLContext = nullptr;
		std::vector<std::string> PendingDroppedFilePaths;
		ivec2 LastCanvasCssSize = {};
	} WebAppState;

	// NOTE: Only the default font is part of the preloaded data package, the others (~16 MB each) are fetched on demand
	EM_JS(void, PeepoWebFetchFontFile, (const char* fileNameUTF8, const char* outPathUTF8), {
		var fileName = UTF8ToString(fileNameUTF8);
		var outPath = UTF8ToString(outPathUTF8);
		Module.pdkInternal.fetchFontFile(fileName, outPath);
	});

	static void LoadFont(void)
	{
		auto& io = ImGui::GetIO();
		std::string fontFilePath = Directory::GetResourceDirectory() + "/assets/" + FontMainFileNameCurrent;

		if (!File::Exists(fontFilePath))
		{
			std::cout << "Font file not preloaded, fetching: " << FontMainFileNameCurrent << std::endl;
			PeepoWebFetchFontFile(FontMainFileNameCurrent.c_str(), fontFilePath.c_str());
			// NOTE: Keep using the current font until "pdk_web_font_file_fetched()" is called
			if (FontMain != nullptr)
				return;
			fontFilePath = Directory::GetResourceDirectory() + "/assets/" + FontMainFileNameDefault;
		}

		if (FontMain != nullptr)
		{
			io.Fonts->RemoveFont(FontMain);
			FontMain = nullptr;
		}

		FontMain = io.Fonts->AddFontFromFileTTF(fontFilePath.c_str(), 16.0f);
		if (FontMain == nullptr)
			std::cout << "Failed to load font file at: " << fontFilePath << std::endl;
		else
			std::cout << "Loaded font file at: " << fontFilePath << std::endl;
	}

	static void BeforeRender(void)
	{
		if (FontMainFileNameTarget != FontMainFileNameCurrent)
		{
			FontMainFileNameCurrent = FontMainFileNameTarget;
			LoadFont();
		}
		if (GlobalState.SetBorderlessFullscreenNextFrame.has_value())
		{
			b8 wantBorderlessFullscreen = *GlobalState.SetBorderlessFullscreenNextFrame;
			if (wantBorderlessFullscreen != GlobalState.IsBorderlessFullscreen)
			{
				SDL_SetWindowFullscreen(WebAppState.SDL_WindowHandle, static_cast<bool>(wantBorderlessFullscreen));
				GlobalState.IsBorderlessFullscreen = wantBorderlessFullscreen;
			}
			GlobalState.SetBorderlessFullscreenNextFrame.reset();
		}
		if (!GlobalState.SetWindowTitleNextFrame.empty())
		{
			if (GlobalState.SetWindowTitleNextFrame != GlobalState.WindowTitle)
			{
				GlobalState.WindowTitle = GlobalState.SetWindowTitleNextFrame;
				EM_ASM({ Module.pdkInternal.onWindowTitle(UTF8ToString($0)); }, GlobalState.WindowTitle.c_str());
			}
			GlobalState.SetWindowTitleNextFrame.clear();
		}
	}

	// NOTE: The canvas is sized by the host page via CSS (it fills its parent element),
	//		 so keep the SDL window size in sync with whatever layout size the canvas currently has
	static void SyncWindowSizeToCanvasCssSize(void)
	{
		double cssWidth = 0.0, cssHeight = 0.0;
		if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS)
			return;

		const ivec2 cssSize = ivec2(static_cast<i32>(cssWidth), static_cast<i32>(cssHeight));
		if (cssSize.x <= 0 || cssSize.y <= 0 || cssSize == WebAppState.LastCanvasCssSize)
			return;

		WebAppState.LastCanvasCssSize = cssSize;
		SDL_SetWindowSize(WebAppState.SDL_WindowHandle, cssSize.x, cssSize.y);
		GlobalState.WindowSize = cssSize;
	}

	static SDL_AppResult SDLCALL SDL_AppInit(void** appstate, int argc, char* argv[])
	{
		(void)appstate; (void)argc; (void)argv;

		SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR, "#canvas");
		SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");

		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			std::cout << "Error: SDL_Init(): " << SDL_GetError() << std::endl;
			return SDL_APP_FAILURE;
		}

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

		auto windowTitle = std::string(WebAppState.StartupParam.WindowTitle.empty() ? "Peepo Drum Kit" : WebAppState.StartupParam.WindowTitle);

		double cssWidth = 1280.0, cssHeight = 720.0;
		emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
		if (cssWidth < 1.0 || cssHeight < 1.0) { cssWidth = 1280.0; cssHeight = 720.0; }

		SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		auto window = SDL_CreateWindow(windowTitle.c_str(), static_cast<int>(cssWidth), static_cast<int>(cssHeight), window_flags);
		if (window == nullptr)
		{
			std::cout << "Error: SDL_CreateWindow(): " << SDL_GetError() << std::endl;
			return SDL_APP_FAILURE;
		}

		GlobalState.NativeWindowHandle = window;
		GlobalState.SDL_GPUDeviceHandle = nullptr;
		GlobalState.WindowSize = ivec2(static_cast<i32>(cssWidth), static_cast<i32>(cssHeight));
		WebAppState.SDL_WindowHandle = window;
		WebAppState.LastCanvasCssSize = GlobalState.WindowSize;

		WebAppState.GLContext = SDL_GL_CreateContext(window);
		if (WebAppState.GLContext == nullptr)
		{
			std::cout << "Error: SDL_GL_CreateContext() (WebGL2 is required): " << SDL_GetError() << std::endl;
			return SDL_APP_FAILURE;
		}
		SDL_GL_MakeCurrent(window, WebAppState.GLContext);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// NOTE: Relative to the working directory, which for the web build is the IndexedDB backed "/persist"
		io.IniFilename = "settings_imgui.ini";

		ImGui::StyleColorsDark();

		// NOTE: In a browser the window coordinates are already in CSS pixels and high DPI is handled entirely
		//		 by io.DisplayFramebufferScale (= devicePixelRatio), same as on macOS. So no extra style scaling here
		auto& style = ImGui::GetStyle();
		style.FontScaleDpi = 1.0f;

		ImGui_ImplSDL3_InitForOpenGL(window, WebAppState.GLContext);
		ImGui_ImplOpenGL3_Init("#version 300 es");

		WebAppState.UserCallbacks.OnStartup();

		std::cout << "Initialization complete." << std::endl;
		EM_ASM({ Module.pdkInternal.onReady(); });

		return SDL_APP_CONTINUE;
	}

	static SDL_AppResult SDLCALL SDL_AppIterate(void* appstate)
	{
		(void)appstate;

		SyncWindowSizeToCanvasCssSize();

		// NOTE: The browser can leave fullscreen on its own (Esc), so the flag shown in the menu has to follow the real state
		if (!GlobalState.SetBorderlessFullscreenNextFrame.has_value())
		{
			EmscriptenFullscreenChangeEvent fullscreenStatus = {};
			if (emscripten_get_fullscreen_status(&fullscreenStatus) == EMSCRIPTEN_RESULT_SUCCESS)
				GlobalState.IsBorderlessFullscreen = (fullscreenStatus.isFullscreen != 0);
		}

		if (!WebAppState.PendingDroppedFilePaths.empty())
		{
			for (auto& path : WebAppState.PendingDroppedFilePaths)
				GlobalState.FilePathsDroppedThisFrame.push_back(std::move(path));
			WebAppState.PendingDroppedFilePaths.clear();
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		{
			BeforeRender();
			if (FontMain != nullptr)
				ImGui::PushFont(FontMain);
			WebAppState.UserCallbacks.OnUpdate();
			if (FontMain != nullptr)
				ImGui::PopFont();
		}

		if (!GlobalState.FilePathsDroppedThisFrame.empty())
			GlobalState.FilePathsDroppedThisFrame.clear();

		ImGui::Render();
		auto& io = ImGui::GetIO();
		glViewport(0, 0, static_cast<GLsizei>(io.DisplaySize.x * io.DisplayFramebufferScale.x), static_cast<GLsizei>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(WebAppState.SDL_WindowHandle);

		// NOTE: There is no meaningful "exit" for a browser tab, the request is simply ignored
		if (GlobalState.RequestExitNextFrame)
			GlobalState.RequestExitNextFrame.reset();

		return SDL_APP_CONTINUE;
	}

	static SDL_AppResult SDLCALL SDL_AppEvent(void* appstate, SDL_Event* event)
	{
		(void)appstate;

		ImGui_ImplSDL3_ProcessEvent(event);
		if (event->type == SDL_EVENT_DROP_FILE && event->drop.data != nullptr)
			WebAppState.PendingDroppedFilePaths.emplace_back(event->drop.data);

		return SDL_APP_CONTINUE;
	}

	static void SDLCALL SDL_AppQuit(void* appstate, SDL_AppResult result)
	{
		(void)appstate; (void)result;

		WebAppState.UserCallbacks.OnShutdown();

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();

		SDL_GL_DestroyContext(WebAppState.GLContext);
		SDL_DestroyWindow(WebAppState.SDL_WindowHandle);
	}

	i32 EnterProgramLoop(const StartupParam& startupParam, UserCallbacks userCallbacks)
	{
		WebAppState.StartupParam = startupParam;
		WebAppState.UserCallbacks = userCallbacks;

		// NOTE: On Emscripten this registers a requestAnimationFrame() driven main loop (emscripten_set_main_loop) and does not return
		return SDL_EnterAppMainCallbacks(0, nullptr, SDL_AppInit, SDL_AppIterate, SDL_AppEvent, SDL_AppQuit);
	}

	void WebQueueDroppedFilePath(std::string_view filePath)
	{
		WebAppState.PendingDroppedFilePaths.emplace_back(filePath);
	}
}

extern "C"
{
	// NOTE: Called from JS once an on demand font file has been written to the virtual file system
	EMSCRIPTEN_KEEPALIVE void pdk_web_font_file_fetched(void)
	{
		// NOTE: Force "BeforeRender()" to (try to) load the target font again
		FontMainFileNameCurrent.clear();
	}

	// NOTE: Same as dropping a file onto the desktop window, the path refers to the Emscripten virtual file system
	EMSCRIPTEN_KEEPALIVE void pdk_web_drop_file(const char* filePathUTF8)
	{
		if (filePathUTF8 != nullptr)
			ApplicationHost::WebQueueDroppedFilePath(filePathUTF8);
	}
}

#endif // __EMSCRIPTEN__
