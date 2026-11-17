#include "chart_editor_i18n.h"
#include "core/core_io.h"

namespace PeepoDrumKit::i18n
{
	static std::unordered_map<u32, std::string> HashStringMap;
	// NOTE: Translations of strings that aren't part of the X-macro key list, looked up by (the hash of) their English text.
	//		 Filled from the optional [Text] section of a locale file, see Tr()
	static std::unordered_map<u32, std::string> TextStringMap;
	static std::shared_mutex HashStringMapMutex;
	std::string SelectedFontName = "NotoSansCJKjp-Regular.otf";
	std::vector<LocaleEntry> LocaleEntries;

	static void InitBuiltinLocaleWithoutLock()
	{
		FontMainFileNameTarget = FontMainFileNameDefault;
		HashStringMap.clear();
		TextStringMap.clear();

#define X(key, en) HashStringMap[Hash(key)] = std::string(en);
		PEEPODRUMKIT_UI_STRINGS_X_MACRO_LIST_EN
#undef X
	}

	void InitBuiltinLocale()
	{
		HashStringMapMutex.lock();
		InitBuiltinLocaleWithoutLock();
		HashStringMapMutex.unlock();
	}

	cstr HashToString(u32 inHash)
	{
		{
			HashStringMapMutex.lock_shared();
			defer { HashStringMapMutex.unlock_shared(); };
			// if (HashStringMap.empty()) InitBuiltinLocale();
			auto it = HashStringMap.find(inHash);
			if (it != HashStringMap.end()) {
				cstr result = it->second.c_str();
				return result;
			}
		}

#if PEEPO_DEBUG
		assert(!"Missing string entry"); return nullptr;
#endif
		return "(undefined)";
	}

	void ExportBuiltinLocaleFiles()
	{
		std::filesystem::create_directories("locales");
		{
			std::fstream localeFile(Directory::GetResourceDirectory() + "/locales/en.ini", std::ios::out | std::ios::trunc);

			localeFile << "[Info]" << std::endl;
			localeFile << "Name = English" << std::endl;
			localeFile << "Lang = en" << std::endl;
			localeFile << "Font = NotoSansCJKjp-Regular.otf" << std::endl << std::endl;

			localeFile << "[Translations]" << std::endl;
#define X(key, en) \
			(localeFile << key << " = " << en << std::endl);
			PEEPODRUMKIT_UI_STRINGS_X_MACRO_LIST_EN
#undef X
		}
	}

	void RefreshLocales()
	{
		HashStringMapMutex.lock();
		LocaleEntries.clear();
        LocaleEntries.push_back(LocaleEntry {
			std::string("en"),
			std::string("English")
		});
		
		auto localesDirPath = Directory::GetResourceDirectory() + "/locales";

		if (!std::filesystem::exists(localesDirPath))
		{
			HashStringMapMutex.unlock();
			return;
		}

		std::filesystem::directory_iterator dirIter(localesDirPath);
		for (const auto& entry : dirIter)
		{
			if (entry.is_regular_file())
			{
				std::fstream localeFile(entry.path(), std::ios::in);
				std::stringstream strBuffer;
				strBuffer << localeFile.rdbuf();
				std::string content = strBuffer.str();
				localeFile.close();

				using namespace PeepoDrumKit::Ini;

				std::string_view sectionName;
				LocaleEntry localeEntry {
					std::string(),
					std::string()
				};

				IniParser iniParser;

				auto sectionFunc = [&](const IniParser::SectionIt& section) {};

				auto keyValueFunc = [&](const IniParser::KeyValueIt& keyValue) {
					if (iniParser.CurrentSection != "Info") return;

					if (keyValue.Key == "Name")
					{
						localeEntry.name = std::string(keyValue.Value);
					}
					else if (keyValue.Key == "Lang")
					{
						localeEntry.id = std::string(keyValue.Value);
					}
				};

				iniParser.ForEachIniKeyValueLine(content, sectionFunc, keyValueFunc);

				if (localeEntry.id != "en")
					LocaleEntries.push_back(localeEntry);
			}
		}
		HashStringMapMutex.unlock();
	}

	cstr Tr(cstr englishText)
	{
		if (englishText == nullptr || englishText[0] == '\0')
			return englishText;

		HashStringMapMutex.lock_shared();
		defer { HashStringMapMutex.unlock_shared(); };
		auto it = TextStringMap.find(Hash(englishText));
		return (it != TextStringMap.end()) ? it->second.c_str() : englishText;
	}

	// NOTE: [Text] keys and values are single ini lines, so line breaks are written as a literal backslash + 'n'
	static std::string UnescapeTextLine(std::string_view in)
	{
		std::string out;
		out.reserve(in.size());
		for (size_t i = 0; i < in.size(); i++)
		{
			if (in[i] == '\\' && (i + 1) < in.size() && in[i + 1] == 'n') { out += '\n'; i++; }
			else out += in[i];
		}
		return out;
	}

	void ReloadLocaleFile(cstr languageId)
	{
		HashStringMapMutex.lock();
		std::cout << "Reloading locale to id " << languageId << std::endl;
		InitBuiltinLocaleWithoutLock();
		std::string localeFilePath = Directory::GetResourceDirectory() + "/locales/" + std::string(languageId) + ".ini";
		std::fstream localeFile(localeFilePath, std::ios::in);
		if (!localeFile.is_open())
		{
			localeFile.close();
			localeFile.open(localeFilePath, std::ios::in);
		}
		std::stringstream strBuffer;
		strBuffer << localeFile.rdbuf();
		std::string content = strBuffer.str();
		localeFile.close();

		using namespace PeepoDrumKit::Ini;

		std::string_view sectionName;
		IniParser iniParser;

		auto sectionFunc = [&](const IniParser::SectionIt& section) {};

		auto keyValueFunc = [&](const IniParser::KeyValueIt& keyValue) {
			if (iniParser.CurrentSection == "Info")
			{
				if (keyValue.Key == "Font")
				{
					FontMainFileNameTarget = keyValue.Value;
				}
				return;
			}
			if (iniParser.CurrentSection == "Text")
			{
				if (!keyValue.Key.empty() && !keyValue.Value.empty())
					TextStringMap[Hash(UnescapeTextLine(keyValue.Key))] = UnescapeTextLine(keyValue.Value);
				return;
			}
			if (iniParser.CurrentSection != "Translations") return;
			if (u32 hash = Hash(keyValue.Key); IsValidHash(hash)) {
				HashStringMap[hash] = keyValue.ValueUntrimmed;
			}
		};

		iniParser.ForEachIniKeyValueLine(content, sectionFunc, keyValueFunc);
		
		HashStringMapMutex.unlock();
	}
}
