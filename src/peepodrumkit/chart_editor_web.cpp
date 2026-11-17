// NOTE: Web (Emscripten) build only. C functions exported to JavaScript so that a host page can embed the editor:
//		 pass in a chart + audio, read the current chart back as TJA text, query unsaved changes etc.
//		 The friendlier JS wrapper around these lives in "web/pdk_api.js" (Module.pdk)
#ifdef __EMSCRIPTEN__

#include "chart_editor.h"
#include <emscripten.h>
#include <cstdlib>
#include <cstring>

namespace PeepoDrumKit
{
	static ChartEditor* WebChartEditorInstance = nullptr;

	void WebRegisterChartEditorInstance(ChartEditor* instance)
	{
		WebChartEditorInstance = instance;
	}

	void ChartEditor::WebOpenChartFile(std::string_view filePath, b8 discardUnsavedChanges)
	{
		if (discardUnsavedChanges)
			StartAsyncImportingChartFile(filePath);
		else
			CheckOpenSaveConfirmationPopupThenCall([this, pathCopy = std::string(filePath)] { StartAsyncImportingChartFile(pathCopy); });
	}

	void ChartEditor::WebLoadAudioFile(std::string_view filePath)
	{
		SetAndStartLoadingChartSongFileName(filePath, context.Undo);
	}

	std::string ChartEditor::WebGetChartAsTJAText() const
	{
		TJA::ParsedTJA tja;
		ConvertChartProjectToTJA(context.Chart, tja);
		std::string tjaText;
		TJA::ConvertParsedToText(tja, tjaText, TJA::Encoding::UTF8);
		return tjaText;
	}

	b8 ChartEditor::WebHasUnsavedChanges() const
	{
		return context.Undo.HasPendingChanges;
	}

	void ChartEditor::WebMarkAsSaved()
	{
		context.Undo.ClearChangesWereMade();
	}

	void ChartEditor::WebNewChart()
	{
		CreateNewChart(context);
	}

	void ChartEditor::WebSaveChart()
	{
		TrySaveChartOrOpenSaveAsDialog(context);
	}

	b8 ChartEditor::WebIsLoading() const
	{
		return importChartFuture.valid() || loadSongFuture.valid();
	}
}

using namespace PeepoDrumKit;

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int pdk_web_is_ready(void)
	{
		return (WebChartEditorInstance != nullptr) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE int pdk_web_open_chart(const char* filePathUTF8, int discardUnsavedChanges)
	{
		if (WebChartEditorInstance == nullptr || filePathUTF8 == nullptr)
			return 0;
		WebChartEditorInstance->WebOpenChartFile(filePathUTF8, discardUnsavedChanges != 0);
		return 1;
	}

	EMSCRIPTEN_KEEPALIVE int pdk_web_load_audio(const char* filePathUTF8)
	{
		if (WebChartEditorInstance == nullptr || filePathUTF8 == nullptr)
			return 0;
		WebChartEditorInstance->WebLoadAudioFile(filePathUTF8);
		return 1;
	}

	// NOTE: Returns a malloc()-ed null terminated UTF-8 string (without BOM) that the caller has to free()
	EMSCRIPTEN_KEEPALIVE char* pdk_web_get_tja(void)
	{
		if (WebChartEditorInstance == nullptr)
			return nullptr;

		std::string tjaText = WebChartEditorInstance->WebGetChartAsTJAText();
		if (UTF8::HasBOM(tjaText))
			tjaText = std::string(UTF8::TrimBOM(tjaText));

		char* out = static_cast<char*>(::malloc(tjaText.size() + 1));
		if (out == nullptr)
			return nullptr;
		::memcpy(out, tjaText.data(), tjaText.size());
		out[tjaText.size()] = '\0';
		return out;
	}

	EMSCRIPTEN_KEEPALIVE int pdk_web_has_unsaved_changes(void)
	{
		return (WebChartEditorInstance != nullptr && WebChartEditorInstance->WebHasUnsavedChanges()) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE void pdk_web_mark_saved(void)
	{
		if (WebChartEditorInstance != nullptr)
			WebChartEditorInstance->WebMarkAsSaved();
	}

	EMSCRIPTEN_KEEPALIVE void pdk_web_new_chart(void)
	{
		if (WebChartEditorInstance != nullptr)
			WebChartEditorInstance->WebNewChart();
	}

	// NOTE: Same as "File > Save", ends up in Module.pdk.onSave() (or a download if no handler is set)
	EMSCRIPTEN_KEEPALIVE void pdk_web_save_chart(void)
	{
		if (WebChartEditorInstance != nullptr)
			WebChartEditorInstance->WebSaveChart();
	}

	// NOTE: See "WebEmbeddedMode" in chart_editor.cpp. Set by the page before the first frame (pdkOptions.embedded)
	EMSCRIPTEN_KEEPALIVE void pdk_web_set_embedded(int embedded)
	{
		WebSetEmbeddedMode(embedded != 0);
	}

	// NOTE: GUI language chosen by the host page ("en", "jp", "zh-tw", ...), optionally hiding the editor's own language menu
	EMSCRIPTEN_KEEPALIVE void pdk_web_set_language(const char* languageIdUTF8, int hideLanguageMenu)
	{
		if (languageIdUTF8 != nullptr)
			WebSetLanguage(languageIdUTF8, hideLanguageMenu != 0);
	}

	EMSCRIPTEN_KEEPALIVE int pdk_web_is_loading(void)
	{
		return (WebChartEditorInstance != nullptr && WebChartEditorInstance->WebIsLoading()) ? 1 : 0;
	}
}

#endif // __EMSCRIPTEN__
