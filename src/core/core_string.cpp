#include "core_string.h"
#include <charconv>
#include <cassert>
#include <vector>
#ifndef __EMSCRIPTEN__
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/ucnv.h>

// ICU-based helpers: convert between std::wstring (platform wchar_t) and UTF-8
static std::string WideToUTF8(std::wstring_view input)
{
	if (input.empty())
		return {};

	UErrorCode status = U_ZERO_ERROR;

	if (sizeof(wchar_t) == 2)
	{
		const UChar* u16 = reinterpret_cast<const UChar*>(input.data());
		int32_t required = 0;
		u_strToUTF8(nullptr, 0, &required, u16, static_cast<int32_t>(input.size()), &status);
		if (!(status == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(status)))
			return {};
		status = U_ZERO_ERROR;
		std::string out;
		out.resize(required);
		u_strToUTF8(out.data(), required, nullptr, u16, static_cast<int32_t>(input.size()), &status);
		if (U_FAILURE(status))
			return {};
		return out;
	}
	else
	{
		// wchar_t is 4 bytes (UTF-32)
		const UChar32* u32 = reinterpret_cast<const UChar32*>(input.data());
		int32_t u16Len = 0;
		u_strFromUTF32(nullptr, 0, &u16Len, u32, static_cast<int32_t>(input.size()), &status);
		if (!(status == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(status)))
			return {};
		status = U_ZERO_ERROR;
		std::vector<UChar> u16buf(u16Len);
		u_strFromUTF32(u16buf.data(), u16Len, nullptr, u32, static_cast<int32_t>(input.size()), &status);
		if (U_FAILURE(status))
			return {};

		int32_t utf8Len = 0;
		u_strToUTF8(nullptr, 0, &utf8Len, u16buf.data(), u16Len, &status);
		if (!(status == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(status)))
			return {};
		status = U_ZERO_ERROR;
		std::string out;
		out.resize(utf8Len);
		u_strToUTF8(out.data(), utf8Len, nullptr, u16buf.data(), u16Len, &status);
		if (U_FAILURE(status))
			return {};
		return out;
	}
}

static std::wstring UTF8ToWide(std::string_view utf8)
{
	if (utf8.empty())
		return {};

	UErrorCode status = U_ZERO_ERROR;
	int32_t u16Len = 0;
	u_strFromUTF8(nullptr, 0, &u16Len, utf8.data(), static_cast<int32_t>(utf8.size()), &status);
	if (!(status == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(status)))
		return {};
	status = U_ZERO_ERROR;
	std::vector<UChar> u16buf(u16Len);
	u_strFromUTF8(u16buf.data(), u16Len, nullptr, utf8.data(), static_cast<int32_t>(utf8.size()), &status);
	if (U_FAILURE(status))
		return {};

	if (sizeof(wchar_t) == 2)
	{
		std::wstring out;
		out.resize(u16Len);
		for (int32_t i = 0; i < u16Len; ++i)
			out[i] = static_cast<wchar_t>(u16buf[i]);
		return out;
	}
	else
	{
		// convert UTF-16 buffer to UTF-32
		int32_t u32Len = 0;
		u_strToUTF32(nullptr, 0, &u32Len, u16buf.data(), u16Len, &status);
		if (!(status == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(status)))
			return {};
		status = U_ZERO_ERROR;
		std::vector<UChar32> u32buf(u32Len);
		u_strToUTF32(u32buf.data(), u32Len, nullptr, u16buf.data(), u16Len, &status);
		if (U_FAILURE(status))
			return {};

		std::wstring out;
		out.resize(u32Len);
		for (int32_t i = 0; i < u32Len; ++i)
			out[i] = static_cast<wchar_t>(u32buf[i]);
		return out;
	}
}

#else // __EMSCRIPTEN__
#include <emscripten.h>

// NOTE: The web build doesn't link ICU (its data tables alone would add ~30 MB to the download).
//		 UTF-8 <-> wide conversions are done by hand and Shift-JIS is delegated to the browser's TextDecoder
static std::string WideToUTF8(std::wstring_view input)
{
	std::string out;
	out.reserve(input.size() * 3);
	for (size_t i = 0; i < input.size(); i++)
	{
		uint32_t c = static_cast<uint32_t>(input[i]);
		if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && (i + 1) < input.size())
		{
			const uint32_t low = static_cast<uint32_t>(input[i + 1]);
			if (low >= 0xDC00 && low <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00); i++; }
		}
		if (c < 0x80) { out += static_cast<char>(c); }
		else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
		else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
	}
	return out;
}

static std::wstring UTF8ToWide(std::string_view utf8)
{
	std::wstring out;
	out.reserve(utf8.size());
	for (size_t i = 0; i < utf8.size();)
	{
		const uint8_t b0 = static_cast<uint8_t>(utf8[i]);
		uint32_t c = 0xFFFD; size_t length = 1;
		if (b0 < 0x80) { c = b0; }
		else if ((b0 & 0xE0) == 0xC0) { length = 2; c = (b0 & 0x1F); }
		else if ((b0 & 0xF0) == 0xE0) { length = 3; c = (b0 & 0x0F); }
		else if ((b0 & 0xF8) == 0xF0) { length = 4; c = (b0 & 0x07); }
		if (i + length > utf8.size()) { out += static_cast<wchar_t>(0xFFFD); break; }
		for (size_t j = 1; j < length; j++) c = (c << 6) | (static_cast<uint8_t>(utf8[i + j]) & 0x3F);
		i += length;
		if (sizeof(wchar_t) == 2 && c >= 0x10000)
		{
			c -= 0x10000;
			out += static_cast<wchar_t>(0xD800 + (c >> 10));
			out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
		}
		else
		{
			out += static_cast<wchar_t>(c);
		}
	}
	return out;
}

// NOTE: Returns a malloc()-ed null terminated UTF-8 string that has to be free()-d by the caller
EM_JS(char*, PeepoWebDecodeShiftJIS, (const char* inData, int inSize), {
	var text = "";
	try { text = new TextDecoder("shift_jis").decode(HEAPU8.subarray(inData, inData + inSize)); }
	catch (e) { console.error("[PeepoDrumKit] Shift-JIS decoding failed", e); }
	return stringToNewUTF8(text);
});

#endif // __EMSCRIPTEN__

namespace UTF8
{
	std::string Narrow(std::wstring_view utf16Input)
	{
		return WideToUTF8(utf16Input);
	}

	std::wstring Widen(std::string_view utf8Input)
	{
		return UTF8ToWide(utf8Input);
	}

	std::string FromShiftJIS(std::string_view shiftJISInput)
	{
#ifdef __EMSCRIPTEN__
		if (shiftJISInput.empty())
			return {};
		char* decoded = PeepoWebDecodeShiftJIS(shiftJISInput.data(), static_cast<int>(shiftJISInput.size()));
		std::string result = (decoded != nullptr) ? std::string(decoded) : std::string();
		::free(decoded);
		return result;
#else
		// Convert Shift_JIS bytes -> wide -> UTF-8
		return UTF8::Narrow(ShiftJIS::Widen(shiftJISInput));
#endif
	}

	WideArg::WideArg(std::string_view utf8Input)
	{
		// Convert UTF-8 to platform wstring then copy into stack/heap buffer
		std::wstring converted = UTF8::Widen(utf8Input);
		convertedLength = static_cast<int>(converted.size());
		if (convertedLength <= 0)
		{
			stackBuffer[0] = L'\0';
			return;
		}

		if (convertedLength < static_cast<int>(std::size(stackBuffer)))
		{
			::memcpy(stackBuffer, converted.data(), sizeof(wchar_t) * convertedLength);
			stackBuffer[convertedLength] = L'\0';
		}
		else
		{
			heapBuffer = std::unique_ptr<wchar_t[]>(new wchar_t[convertedLength + 1]);
			::memcpy(heapBuffer.get(), converted.data(), sizeof(wchar_t) * convertedLength);
			heapBuffer[convertedLength] = L'\0';
		}
	}

	const wchar_t* WideArg::c_str() const
	{
		return (convertedLength < static_cast<int>(std::size(stackBuffer))) ? stackBuffer : heapBuffer.get();
	}
}

#ifndef __EMSCRIPTEN__
namespace ShiftJIS
{
	// NOTE: According to https://docs.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
	//		 932 | shift_jis | ANSI/OEM Japanese; Japanese (Shift-JIS)
	// ICU encoding name for Shift-JIS
	static constexpr const char* ICU_SHIFT_JIS = "Shift_JIS";

	std::string Narrow(std::wstring_view utf16Input)
	{
		// Convert wide -> UTF-8, then UTF-8 -> Shift_JIS via ICU
		const std::string utf8 = UTF8::Narrow(utf16Input);
		if (utf8.empty())
			return {};

		UErrorCode status = U_ZERO_ERROR;
		int32_t destCapacity = static_cast<int32_t>(utf8.size()) * 4 + 16;
		std::string out;
		out.resize(destCapacity);
		int32_t actual = ucnv_convert(ICU_SHIFT_JIS, "UTF-8", out.data(), destCapacity, utf8.data(), static_cast<int32_t>(utf8.size()), &status);
		if (U_FAILURE(status))
			return {};
		out.resize(actual);
		return out;
	}

	std::wstring Widen(std::string_view utf8Input)
	{
		// Convert Shift_JIS -> UTF-8 via ICU, then UTF-8 -> wide
		if (utf8Input.empty())
			return {};
		UErrorCode status = U_ZERO_ERROR;
		int32_t destCapacity = static_cast<int32_t>(utf8Input.size()) * 4 + 16;
		std::string tmp;
		tmp.resize(destCapacity);
		int32_t actual = ucnv_convert("UTF-8", ICU_SHIFT_JIS, tmp.data(), destCapacity, utf8Input.data(), static_cast<int32_t>(utf8Input.size()), &status);
		if (U_FAILURE(status))
			return {};
		tmp.resize(actual);
		return UTF8::Widen(tmp);
	}

	std::string FromUTF8(std::string_view utf8Input)
	{
		// Convert UTF-8 to Shift_JIS
		if (utf8Input.empty())
			return {};
		UErrorCode status = U_ZERO_ERROR;
		int32_t destCapacity = static_cast<int32_t>(utf8Input.size()) * 4 + 16;
		std::string out;
		out.resize(destCapacity);
		int32_t actual = ucnv_convert(ICU_SHIFT_JIS, "UTF-8", out.data(), destCapacity, utf8Input.data(), static_cast<int32_t>(utf8Input.size()), &status);
		if (U_FAILURE(status))
			return {};
		out.resize(actual);
		return out;
	}
}
#else // __EMSCRIPTEN__
namespace ShiftJIS
{
	// NOTE: Browsers can only *decode* Shift-JIS. Encoding isn't needed by the editor (charts are always saved as UTF-8)
	std::string Narrow(std::wstring_view utf16Input) { assert(!"Shift-JIS encoding is not supported in the web build"); return {}; }
	std::wstring Widen(std::string_view shiftJISInput) { return UTF8::Widen(UTF8::FromShiftJIS(shiftJISInput)); }
	std::string FromUTF8(std::string_view utf8Input) { assert(!"Shift-JIS encoding is not supported in the web build"); return {}; }
}
#endif // __EMSCRIPTEN__

namespace ASCII
{
	template <typename T>
	b8 TryParsePrimitive(std::string_view string, T& out)
	{
		if constexpr (std::is_integral_v<T>) {
			const std::from_chars_result result = std::from_chars(string.data(), string.data() + string.size(), out);
			const b8 hasNoError = (result.ec == std::errc{});
			const b8 parsedFully = (result.ptr == string.data() + string.size());

			return hasNoError && parsedFully;
		}
		else { // clang does not support from_chars with floating point types; fall back to C library functions
			std::string tmp(string.data(), string.data() + string.size());
			char* endPtr = nullptr;
			errno = 0;
			T value;
			if constexpr (std::is_same_v<T, float>)
				value = std::strtof(tmp.c_str(), &endPtr);
			else if constexpr (std::is_same_v<T, double>)
				value = std::strtod(tmp.c_str(), &endPtr);
			else if constexpr (std::is_same_v<T, long double>)
				value = std::strtold(tmp.c_str(), &endPtr);
			else
				static_assert(dependent_false<T>, "Unsupported non-integral type");

			if (endPtr != tmp.c_str() + tmp.size() || errno == ERANGE)
				return false;
			out = value;
			return true;
		}
	}

	b8 TryParse(std::string_view string, u32& out) { return TryParsePrimitive(string, out); }
	b8 TryParse(std::string_view string, i32& out) { return TryParsePrimitive(string, out); }
	b8 TryParse(std::string_view string, u64& out) { return TryParsePrimitive(string, out); }
	b8 TryParse(std::string_view string, i64& out) { return TryParsePrimitive(string, out); }
	b8 TryParse(std::string_view string, f32& out) { return TryParsePrimitive(string, out); }
	b8 TryParse(std::string_view string, f64& out) { return TryParsePrimitive(string, out); }
	b8 TryParse(std::string_view string, Complex& out) {
		std::istringstream in(std::string{string});
		in >> out;
		return static_cast<bool>(in); // allow eof, reject fail & bad
	}
}
