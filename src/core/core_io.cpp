#include "core_io.h"
#include "core_string.h"
#include <vector>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <functional>
#include <SDL3/SDL.h>

#if defined(_MSC_VER)
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// NOTE: Web build virtual file system layout:
//		 "/app"     -> read only preloaded resources (assets, locales)
//		 "/persist" -> settings, backed by IndexedDB (also the working directory)
//		 "/work"    -> in-memory charts and audio files that were opened via the browser or passed in by the host page
namespace WebFS
{
	static constexpr std::string_view ResourceDirectory = "/app";
	static constexpr std::string_view WorkDirectoryPrefix = "/work/";
}
#endif

namespace Path
{
	static constexpr std::string_view Win32RootDirectoryPrefix = "\\";
	static constexpr std::string_view Win32CurrentDirectoryPrefix = ".\\";
	static constexpr std::string_view Win32ParentDirectoryPrefix = "..\\";

	std::string_view GetExtension(std::string_view filePath)
	{
		const size_t lastDirectoryIndex = filePath.find_last_of(DirectorySeparators);
		const std::string_view directoryTrimmed = (lastDirectoryIndex == std::string_view::npos) ? filePath : filePath.substr(lastDirectoryIndex);

		const size_t lastExtensionIndex = directoryTrimmed.find_last_of(ExtensionSeparator);
		return (lastExtensionIndex == std::string_view::npos) ? "" : directoryTrimmed.substr(lastExtensionIndex);
	}

	std::string_view TrimExtension(std::string_view filePath)
	{
		const std::string_view filePathExtension = GetExtension(filePath);
		return filePath.substr(0, filePath.size() - filePathExtension.size());
	}

	b8 HasExtension(std::string_view filePath, std::string_view extension)
	{
		const std::string_view filePathExtension = GetExtension(filePath);
		return ASCII::MatchesInsensitive(filePathExtension, extension);
	}

	b8 HasAnyExtension(std::string_view filePath, std::string_view packedExtensions)
	{
		const std::string_view filePathExtension = GetExtension(filePath);
		if (filePathExtension.empty() || packedExtensions.empty())
			return false;

		b8 anyExtensionMatches = false;
		ASCII::ForEachInCharSeparatedList(packedExtensions, ';', [&](std::string_view extension)
										  {
			if (extension.empty()) { assert(!"Accidental invalid packedExtensions format (?)"); return; }
			if (ASCII::MatchesInsensitive(filePathExtension, extension))
				anyExtensionMatches = true; });
		return anyExtensionMatches;
	}

	std::string_view GetFileName(std::string_view filePath, b8 includeExtension)
	{
		const size_t lastDirectoryIndex = filePath.find_last_of(DirectorySeparators);
		const std::string_view fileName = (lastDirectoryIndex == std::string_view::npos) ? filePath : filePath.substr(lastDirectoryIndex + 1);
		return (includeExtension) ? fileName : TrimExtension(fileName);
	}

	std::string_view GetDirectoryName(std::string_view filePath)
	{
		const size_t lastDirectoryIndex = filePath.find_last_of(DirectorySeparators);
		if (lastDirectoryIndex == std::string_view::npos)
			return "";
		if (lastDirectoryIndex == 0)
			return filePath.substr(0, 1);
		return filePath.substr(0, lastDirectoryIndex);
	}

	b8 IsRelative(std::string_view filePath)
	{
		return std::filesystem::path(filePath).is_relative();
	}

	b8 IsDirectory(std::string_view filePath)
	{
		return std::filesystem::is_directory(filePath);
	}

	std::string TryMakeAbsolute(std::string_view relativePath, std::string_view baseFileOrDirectory)
	{
		if (relativePath.empty() || baseFileOrDirectory.empty() || !IsRelative(relativePath))
			return std::string(relativePath);

		// TODO: Also resolve "../" etc. I guess..?
		std::string baseDirectory{IsDirectory(baseFileOrDirectory) ? baseFileOrDirectory : GetDirectoryName(baseFileOrDirectory)};
		return baseDirectory.append("/").append(relativePath);
	}

	std::string TryMakeRelative(std::string_view absolutePath, std::string_view baseFileOrDirectory)
	{
		try
		{
			std::filesystem::path basePath = std::filesystem::path{baseFileOrDirectory};
			if (!IsDirectory(baseFileOrDirectory))
				basePath = basePath.parent_path();

			std::filesystem::path absoluteFsPath = std::filesystem::path{absolutePath};
			std::filesystem::path relativePath = std::filesystem::relative(absoluteFsPath, basePath);
			return relativePath.string();
		}
		catch (const std::filesystem::filesystem_error &)
		{
			return "";
		}
	}

	std::string CopyAndNormalize(std::string_view filePath)
	{
		std::string normalizedCopy{filePath};
		std::replace(normalizedCopy.begin(), normalizedCopy.end(), DirectorySeparatorWin32, DirectorySeparator);
		return normalizedCopy;
	}

	std::string &NormalizeInPlace(std::string &inOutFilePath)
	{
		std::replace(inOutFilePath.begin(), inOutFilePath.end(), DirectorySeparatorWin32, DirectorySeparator);
		return inOutFilePath;
	}

	std::string CopyAndNormalizeWin32(std::string_view filePath)
	{
		std::string normalizedCopy{filePath};
		std::replace(normalizedCopy.begin(), normalizedCopy.end(), DirectorySeparator, DirectorySeparatorWin32);
		return normalizedCopy;
	}

	std::string &NormalizeInPlaceWin32(std::string &inOutFilePath)
	{
		std::replace(inOutFilePath.begin(), inOutFilePath.end(), DirectorySeparator, DirectorySeparatorWin32);
		return inOutFilePath;
	}
}

namespace File
{
	UniqueFileContent ReadAllBytes(std::string_view filePath)
	{
		if (filePath.empty())
			return UniqueFileContent{};

		size_t dataSize = 0;
		auto data = SDL_LoadFile(filePath.data(), &dataSize);

		if (data == nullptr) {
			std::cout << "SDL_LoadFile failed for '" << filePath << "': " << SDL_GetError() << std::endl;
			return UniqueFileContent{};
		}

		std::unique_ptr<u8[]> contentBuffer = std::make_unique<u8[]>(dataSize);
		std::memcpy(contentBuffer.get(), data, dataSize);
		SDL_free(data);

		return UniqueFileContent{std::move(contentBuffer), dataSize};
	}

	b8 WriteAllBytes(std::string_view filePath, const void *fileContent, size_t fileSize)
	{
		if (filePath.empty() || fileContent == nullptr)
			return false;

#ifdef __EMSCRIPTEN__
		const std::string filePathNullTerminated { filePath };
		if (!SDL_SaveFile(filePathNullTerminated.c_str(), fileContent, fileSize))
			return false;

		// NOTE: Files "saved" by the user only exist in memory, so hand them over to the page (which either passes them to the host or starts a download)
		if (filePath.starts_with(WebFS::WorkDirectoryPrefix))
			EM_ASM({ Module.pdkInternal.onWorkFileWritten(UTF8ToString($0)); }, filePathNullTerminated.c_str());
		return true;
#else
		return SDL_SaveFile(filePath.data(), fileContent, fileSize);
#endif
	}

	b8 WriteAllBytes(std::string_view filePath, const UniqueFileContent &uniqueFileContent)
	{
		return WriteAllBytes(filePath, uniqueFileContent.Content.get(), uniqueFileContent.Size);
	}

	b8 WriteAllBytes(std::string_view filePath, const std::string_view textFileContent)
	{
		return WriteAllBytes(filePath, textFileContent.data(), textFileContent.size());
	}

	b8 Exists(std::string_view filePath)
	{
		// const DWORD attributes = ::GetFileAttributesW(UTF8::WideArg(filePath).c_str());
		// return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
		return std::filesystem::exists(filePath);
	}

	b8 Copy(std::string_view source, std::string_view destination, b8 overwriteExisting)
	{
		// return ::CopyFileW(UTF8::WideArg(source).c_str(), UTF8::WideArg(destination).c_str(), !overwriteExisting);
		std::filesystem::copy_options options = overwriteExisting ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none;
		std::error_code ec;
		std::filesystem::copy_file(source, destination, options, ec);
		return !ec;
	}
}

namespace CommandLine
{
	static std::atomic<bool> commandLineSet = false;
	static std::vector<std::string> commandLineArguments;
	
	std::vector<std::string>& GetCommandLineUTF8()
	{
		return commandLineArguments;
	}

	void SetCommandLineSTD(int argc, const char **argv)
	{
		if (commandLineSet.load())
			return;
		commandLineSet = true;

		commandLineArguments.reserve(argc);
		for (int i = 0; i < argc; ++i)
		{
			commandLineArguments.emplace_back(argv[i]);
		}
	}
	
	void SetCommandLineSTD(std::vector<std::string> argv)
	{
		if (commandLineSet.load())
			return;
		commandLineSet = true;

		commandLineArguments = std::move(argv);
	}
}

namespace Directory
{
	b8 Create(std::string_view directoryPath)
	{
		if (directoryPath.empty())
			return false;

		std::error_code ec;
		std::filesystem::create_directories(directoryPath, ec);
		return !ec;
	}

	b8 Exists(std::string_view directoryPath)
	{
		if (directoryPath.empty())
			return false;

		std::error_code ec;
		return std::filesystem::is_directory(directoryPath, ec);
	}

	std::string GetExecutablePath()
	{
#if defined(_MSC_VER)
		wchar_t path[FILENAME_MAX] = {0};
		GetModuleFileNameW(nullptr, path, FILENAME_MAX);
		return std::filesystem::path(path).string();
#else
		auto args = CommandLine::GetCommandLineUTF8();
		return args.empty() ? "" : std::string(args[0]);
#endif
	}

	std::string GetExecutableDirectory()
	{
#ifdef __EMSCRIPTEN__
		return std::string(WebFS::ResourceDirectory);
#else
		return std::filesystem::path(GetExecutablePath()).parent_path().string();
#endif
	}
	
	std::string GetResourceDirectory()
	{
#ifdef SDL_PLATFORM_MACOS
		return std::filesystem::path(GetExecutableDirectory()).parent_path().append("Resources").string();
#else
		return GetExecutableDirectory();
#endif
	}

	std::string GetWorkingDirectory()
	{
		return std::filesystem::current_path().string();
	}

	void SetWorkingDirectory(std::string_view directoryPath)
	{
		std::filesystem::current_path(directoryPath);
	}
}

namespace Shell
{
	void OpenInExplorer(std::string_view filePath)
	{
		if (filePath.empty())
			return;

#ifdef __EMSCRIPTEN__
		// NOTE: There is no file explorer for the in-memory virtual file system
		return;
#endif

		std::string pathString{ filePath };

		if (Path::IsRelative(filePath))
		{
			std::error_code ec;
			std::filesystem::path absolutePath = std::filesystem::absolute(pathString, ec);
			if (!ec)
				pathString = absolutePath.string();
		}

#if defined(_WIN32)
		SDL_OpenURL(pathString.c_str());
#else
		std::string url = "file://" + pathString;
		SDL_OpenURL(url.c_str());
#endif
	}

	MessageBoxResult ShowMessageBox(std::string_view message, std::string_view title, MessageBoxButtons buttons, MessageBoxIcon icon, void *parentWindowHandle)
	{
		// UINT flags = 0;
		// switch (buttons)
		// {
		// case MessageBoxButtons::AbortRetryIgnore: flags |= MB_ABORTRETRYIGNORE; break;
		// case MessageBoxButtons::CancelTryContinue: flags |= MB_CANCELTRYCONTINUE; break;
		// case MessageBoxButtons::OK: flags |= MB_OK; break;
		// case MessageBoxButtons::OKCancel: flags |= MB_OKCANCEL; break;
		// case MessageBoxButtons::RetryCancel: flags |= MB_RETRYCANCEL; break;
		// case MessageBoxButtons::YesNo: flags |= MB_YESNO; break;
		// case MessageBoxButtons::YesNoCancel: flags |= MB_YESNOCANCEL; break;
		// default: break;
		// }
		// switch (icon)
		// {
		// case MessageBoxIcon::Asterisk: flags |= MB_ICONASTERISK; break;
		// case MessageBoxIcon::Error: flags |= MB_ICONERROR; break;
		// case MessageBoxIcon::Exclamation: flags |= MB_ICONEXCLAMATION; break;
		// case MessageBoxIcon::Hand: flags |= MB_ICONHAND; break;
		// case MessageBoxIcon::Information: flags |= MB_ICONINFORMATION; break;
		// case MessageBoxIcon::None: break;
		// case MessageBoxIcon::Question: flags |= MB_ICONQUESTION; break;
		// case MessageBoxIcon::Stop: flags |= MB_ICONSTOP; break;
		// case MessageBoxIcon::Warning: flags |= MB_ICONWARNING; break;
		// default: break;
		// }

		// const WORD languageID = 0;
		// const int result = ::MessageBoxExW(reinterpret_cast<HWND>(parentWindowHandle), UTF8::WideArg(message).c_str(), title.empty() ? nullptr : UTF8::WideArg(title).c_str(), flags, languageID);
		// switch (result)
		// {
		// case IDABORT: return MessageBoxResult::Abort;
		// case IDCANCEL: return MessageBoxResult::Cancel;
		// case IDCONTINUE: return MessageBoxResult::Continue;
		// case IDIGNORE: return MessageBoxResult::Ignore;
		// case IDNO: return MessageBoxResult::No;
		// case IDOK: return MessageBoxResult::OK;
		// case IDRETRY: return MessageBoxResult::Retry;
		// case IDTRYAGAIN: return MessageBoxResult::TryAgain;
		// case IDYES: return MessageBoxResult::Yes;
		// default: return MessageBoxResult::None;
		// }
		return MessageBoxResult::None;
	}
}

namespace Shell
{
	enum class DialogType : u8
	{
		Save,
		Open
	};
	enum class DialogPickType : u8
	{
		File,
		Folder
	};

#ifdef __EMSCRIPTEN__
	// NOTE: Browser replacement for native file dialogs:
	//		 Open -> <input type="file"> whose files get copied into "/work"
	//		 Save -> immediately "picks" a path inside "/work", writing to it then notifies the page (see File::WriteAllBytes())
	static FileDialog* WebPendingOpenDialog = nullptr;
	static std::string WebDialogOutFilePath;

	static b8 CreateAndShowFileDialog(FileDialog &dialog, DialogType dialogType, DialogPickType pickType)
	{
		if (pickType == DialogPickType::Folder)
		{
			printf("Folder dialogs are not supported in the web build.\n");
			return false;
		}

		if (dialogType == DialogType::Save)
		{
			std::string fileName { dialog.InFileName.empty() ? std::string_view("Untitled") : dialog.InFileName };
			for (char& c : fileName) { if (c == '/' || c == '\\' || c == ':') c = '_'; }
			if (!dialog.InDefaultExtension.empty() && !Path::HasExtension(fileName, dialog.InDefaultExtension))
			{
				if (dialog.InDefaultExtension.front() != '.')
					fileName += '.';
				fileName += dialog.InDefaultExtension;
			}

			Directory::Create("/work");
			WebDialogOutFilePath = std::string(WebFS::WorkDirectoryPrefix) + fileName;
			dialog.OutFilePath = WebDialogOutFilePath.c_str();
			if (dialog.onCallback)
				dialog.onCallback(FileDialogResult::OK);
			return true;
		}

		// NOTE: Packed as "ext;ext;ext" for the JS side, empty meaning any
		std::string packedExtensions;
		for (const auto &filter : dialog.InFilters)
		{
			if (filter.Spec == AllFilesFilterSpec)
				continue;
			if (!packedExtensions.empty())
				packedExtensions += ';';
			packedExtensions += filter.Spec;
		}

		WebPendingOpenDialog = &dialog;
		EM_ASM({ Module.pdkInternal.openFileDialog(UTF8ToString($0)); }, packedExtensions.c_str());
		return true;
	}

	void WebOnFileDialogResult(const char* filePathUTF8)
	{
		FileDialog* dialog = WebPendingOpenDialog;
		WebPendingOpenDialog = nullptr;
		if (dialog == nullptr)
			return;

		WebDialogOutFilePath = (filePathUTF8 != nullptr) ? filePathUTF8 : "";
		dialog->OutFilePath = WebDialogOutFilePath.c_str();
		if (dialog->onCallback)
			dialog->onCallback(WebDialogOutFilePath.empty() ? FileDialogResult::Cancel : FileDialogResult::OK);
	}
#else // __EMSCRIPTEN__
	static void SDL_DialogCallback(void *userdata, const char *const *filelist, int filter)
	{
		auto dialog = reinterpret_cast<FileDialog *>(userdata);
		if (dialog == nullptr)
			return;
		if (filelist == nullptr)
		{
			printf("Error opening file dialog: %s\n", SDL_GetError());
			dialog->OutFilePath = "";
			dialog->onCallback(FileDialogResult::Error);
		}
		else if (*filelist == nullptr)
		{
			dialog->OutFilePath = "";
			dialog->onCallback(FileDialogResult::Cancel);
		}
		else
		{
			dialog->OutFilePath = filelist[0];
			dialog->onCallback(FileDialogResult::OK);
		}
	}

	static b8 CreateAndShowFileDialog(FileDialog &dialog, DialogType dialogType, DialogPickType pickType)
	{
		if (!SDL_IsMainThread())
		{
			printf("File dialog can only be opened from the main thread.\n");
			return false;
		}

		auto props = SDL_CreateProperties();
		if (!dialog.InTitle.empty())
			SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, dialog.InTitle.data());
		if (!dialog.InFileName.empty())
			SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, dialog.InFileName.data());
		if (dialog.InParentWindowHandle != nullptr)
			SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, dialog.InParentWindowHandle);

		if (!dialog.InDefaultExtension.empty())
		{
			std::string defaultExtWithDot = ".";
			defaultExtWithDot.append(dialog.InDefaultExtension);
			SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, defaultExtWithDot.c_str());
		}

		std::vector<SDL_DialogFileFilter> sdlFilters;
		if (!dialog.InFilters.empty())
		{
			sdlFilters.reserve(dialog.InFilters.size());
			for (const auto &filter : dialog.InFilters)
			{
				SDL_DialogFileFilter sdlFilter;
				sdlFilter.name = filter.Name.data();
				sdlFilter.pattern = filter.Spec.data();
				sdlFilters.push_back(sdlFilter);
			}
			SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, sdlFilters.data());
			SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(sdlFilters.size()));
		}

		SDL_FileDialogType sdlDialogType;
		switch (dialogType)
		{
		case DialogType::Open:
			sdlDialogType = (pickType == DialogPickType::File) ? SDL_FILEDIALOG_OPENFILE : SDL_FILEDIALOG_OPENFOLDER;
			break;
		case DialogType::Save:
			sdlDialogType = SDL_FILEDIALOG_SAVEFILE;
			break;
		default:
			return false;
		}

		SDL_ShowFileDialogWithProperties(sdlDialogType, SDL_DialogCallback, &dialog, props);

		SDL_DestroyProperties(props);

		return true;
	}
#endif // __EMSCRIPTEN__

	b8 FileDialog::OpenRead() { return CreateAndShowFileDialog(*this, DialogType::Open, DialogPickType::File); }
	b8 FileDialog::OpenSave() { return CreateAndShowFileDialog(*this, DialogType::Save, DialogPickType::File); }
	b8 FileDialog::OpenSelectFolder() { return CreateAndShowFileDialog(*this, DialogType::Open, DialogPickType::Folder); }
}

#ifdef __EMSCRIPTEN__
extern "C"
{
	// NOTE: Called from JS after the files picked via an <input type="file"> have been written into "/work" (or with null on cancel)
	EMSCRIPTEN_KEEPALIVE void pdk_web_file_dialog_result(const char* filePathUTF8)
	{
		Shell::WebOnFileDialogResult(filePathUTF8);
	}
}
#endif // __EMSCRIPTEN__
